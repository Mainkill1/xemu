#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Validate evidence admission; this utility does not measure performance."""
import argparse
import json
from pathlib import Path
import re
import sys


def digest(value, length=64):
    return isinstance(value, str) and re.fullmatch(
        "[0-9a-f]{" + str(length) + "}", value
    )


def positive_uint(value):
    return type(value) is int and 0 < value < 2**64


def known(value):
    return (
        isinstance(value, str) and bool(value.strip()) and value != "unknown"
    )


def enum_values(*values):
    return lambda value: isinstance(value, str) and value in values


def boolean(value):
    return type(value) is bool


EXECUTION_FIELDS = {
    "cpu": {
        "route": enum_values("tcg", "kvm", "hvf", "whpx"),
        "hard_fpu": boolean,
        "mttcg": boolean,
    },
    "voice": {
        "resampler": enum_values("sinc", "linear"),
        "workers": lambda v: type(v) is int and 1 <= v <= 16,
        "library": known,
    },
    "dsp": {
        "gp_engine": enum_values("jit", "interpreter"),
        "ep_engine": enum_values("jit", "interpreter"),
        "gp_realtime": boolean,
        "ep_realtime": boolean,
    },
    "presentation": {
        "transport": enum_values("shared", "host-copy"),
        "gl_renderer": known,
        "gl_vendor": known,
    },
    "perturbations": {
        "vsync_interval": lambda v: type(v) is int and v in [-1, 0, 1]
    },
    "renderer": {"surface_scale": lambda v: type(v) is int and 1 <= v <= 10},
}


def validate(contract, runs):
    try:
        return validate_records(contract, runs)
    except (
        ValueError,
        KeyError,
        TypeError,
        AttributeError,
        IndexError,
    ) as error:
        return {
            "eligible": False,
            "errors": [str(error)],
            "performance_verdict": "not_evaluated",
        }


def validate_records(contract, runs):
    errors = []
    if contract.get("schema") != "xemu-shortcut-comparison/v1":
        errors.append("Unsupported comparison schema")
    kind = contract.get("kind")
    if kind not in ["setting_ab", "code_patch_ab"]:
        errors.append("Unknown comparison kind")
    identities = [contract.get(key, {}) for key in ["reference", "candidate"]]
    for identity in identities:
        if not digest(identity.get("executable_sha256")) or not digest(
            identity.get("commit"), 40
        ):
            errors.append("Incomplete declared executable/commit identity")
    if kind == "setting_ab" and identities[0] != identities[1]:
        errors.append("Setting A/B requires identical executable identity")
    changes = contract.get("policy_changes", {})
    effective = contract.get("effective_changes", {})
    execution_changes = contract.get("execution_changes", {})
    if not isinstance(execution_changes, dict):
        errors.append("Invalid execution changes")
        execution_changes = {}
    required = contract.get("required_counters", [])
    if not isinstance(changes, dict) or (
        kind == "setting_ab" and len(changes) != 1
    ):
        errors.append("Setting A/B requires one declared policy change")
        changes = {}
    for key, pair in changes.items():
        if not isinstance(pair, list) or len(pair) != 2 or pair[0] == pair[1]:
            errors.append("Invalid policy change: " + key)
    if not isinstance(effective, dict) or set(effective) != set(changes):
        errors.append("Missing declared effective policy changes")
        effective = {}
    for key, pair in effective.items():
        if not isinstance(pair, list) or len(pair) != 2 or pair[0] == pair[1]:
            errors.append("Invalid effective policy change: " + key)
    for key, pair in execution_changes.items():
        if (
            not isinstance(pair, list)
            or len(pair) != 2
            or pair[0] == pair[1]
            or key.count(".") != 1
        ):
            errors.append("Invalid execution change: " + key)
    if (
        not isinstance(required, list)
        or not required
        or not all(isinstance(k, str) for k in required)
    ):
        errors.append("Required target-path counters are missing")
        required = []
    if len(runs) != 8:
        errors.append("Need eight completed ABBA/BAAB records")
        return {
            "eligible": False,
            "errors": errors,
            "performance_verdict": "not_evaluated",
        }

    stable = [
        "platform",
        "build_type",
        "actual_backend",
        "requested_backend",
        "requested_gpu",
        "gpu",
        "comparison_config_sha256",
        "workload",
        "input_sha256",
        "configured_window",
        "counter_units",
    ]
    seen = set()
    runner_ids = set()
    side_profiles = {}
    for index, run in enumerate(runs):
        label = f"Run {index + 1}: "
        side = "ABBABAAB"[index]
        if (
            run.get("schema") != "xemu-shortcut-evidence/v1"
            or run.get("complete") is not True
        ):
            errors.append(label + "incomplete/unsupported evidence")
        identity = identities[side == "B"]
        if any(
            run.get(k) != identity.get(k)
            for k in ["executable_sha256", "commit"]
        ):
            errors.append(label + "undeclared executable identity")
        session = run.get("session_id")
        if not isinstance(session, str) or not session or session in seen:
            errors.append(label + "missing/duplicate session")
        seen.add(session)
        if (
            run.get("order") != ("ABBA" if index < 4 else "BAAB")
            or run.get("position") != index % 4 + 1
        ):
            errors.append(label + "invalid order or position")
        if run.get("order_group") != runs[0].get("order_group") or not run.get(
            "order_group"
        ):
            errors.append(label + "different order group")
        for field in stable:
            if field not in run or run[field] != runs[0].get(field):
                errors.append(label + "mismatched/missing " + field)
        for field in [
            "platform",
            "build_type",
            "requested_backend",
            "requested_gpu",
            "workload",
        ]:
            if not known(run.get(field)):
                errors.append(label + "unknown immutable context " + field)
        if run.get("requested_backend") not in ["opengl", "vulkan"]:
            errors.append(label + "unknown requested backend context")
        for field in [
            "base_config_sha256",
            "comparison_config_sha256",
            "input_sha256",
        ]:
            if not digest(run.get(field)):
                errors.append(label + "invalid " + field)
        if run.get("actual_backend") not in ["vulkan", "opengl"]:
            errors.append(label + "unknown actual backend")
        for flag in [
            "source_reset",
            "collection_error",
            "gpu_changed",
            "overflowed",
            "profile_changed",
        ]:
            if run.get(flag, False) is not False:
                errors.append(label + flag)
        if run.get("gpu", {}).get("available") is not True:
            errors.append(label + "unknown GPU")
        gpu = run.get("gpu", {})
        if (
            not known(gpu.get("name"))
            or any(
                not digest(gpu.get(k), 32)
                for k in ["device_uuid", "driver_uuid"]
            )
            or any(
                type(gpu.get(k)) is not int or not 0 <= gpu[k] < 2**32
                for k in [
                    "vendor_id",
                    "device_id",
                    "driver_version",
                    "api_version",
                    "type",
                ]
            )
        ):
            errors.append(label + "incomplete GPU identity")
        profile = run.get("execution", {})
        for component, fields in EXECUTION_FIELDS.items():
            values = profile.get(component, {})
            if any(
                not validator(values.get(key))
                for key, validator in fields.items()
            ):
                errors.append(
                    label + "unknown execution component " + component
                )
        for component, fields in profile.items():
            for key, value in fields.items():
                path = component + "." + key
                pair = execution_changes.get(path)
                if pair is not None:
                    if value != pair[side == "B"]:
                        errors.append(
                            label + "undeclared execution value " + path
                        )
                elif value != runs[0].get("execution", {}).get(
                    component, {}
                ).get(key):
                    errors.append(label + "mismatched execution " + path)
        for path in execution_changes:
            component, key = path.split(".")
            if key not in profile.get(component, {}):
                errors.append(
                    label + "missing declared execution path " + path
                )
        if set(profile) != set(runs[0].get("execution", {})):
            errors.append(label + "mismatched execution components")
        for component, fields in runs[0].get("execution", {}).items():
            if set(fields) != set(profile.get(component, {})):
                errors.append(
                    label + "mismatched execution fields " + component
                )
        configured, actual = run.get("configured_window", {}), run.get(
            "actual_window", {}
        )
        first, count = configured.get("start_frame"), configured.get(
            "frame_count"
        )
        if (
            type(first) is not int
            or type(count) is not int
            or first < 0
            or count <= 0
            or first + count > 2**64 - 1
        ):
            errors.append(label + "invalid configured window")
        elif (
            actual.get("start_frame") != first
            or actual.get("end_frame") != first + count
        ):
            errors.append(label + "incomplete actual window")
        start_ns, end_ns = actual.get("start_monotonic_ns"), actual.get(
            "end_monotonic_ns"
        )
        if (
            type(actual.get("start_frame")) is not int
            or type(actual.get("end_frame")) is not int
            or not positive_uint(actual.get("progress_incarnation"))
            or not positive_uint(start_ns)
            or not positive_uint(end_ns)
            or end_ns <= start_ns
            or not positive_uint(run.get("wall_interval_ns"))
            or run["wall_interval_ns"] != end_ns - start_ns
            or not known(run.get("wall_interval_source"))
        ):
            errors.append(label + "invalid window timing")
        settings = run.get("settings", {})
        remainder = {
            key: value for key, value in settings.items() if key not in changes
        }
        if remainder != {
            key: value
            for key, value in runs[0].get("settings", {}).items()
            if key not in changes
        }:
            errors.append(label + "undeclared effective setting difference")
        for key, state in settings.items():
            if (
                type(state.get("restart_pending")) is not bool
                or type(state.get("available")) is not bool
                or not known(state.get("requested"))
                or not known(state.get("effective"))
            ):
                errors.append(label + "invalid setting state " + key)
            if state.get("restart_pending"):
                errors.append(label + "restart pending for " + key)
        for key, pair in changes.items():
            state = settings.get(key, {})
            if (
                len(pair) != 2
                or state.get("requested") != pair[side == "B"]
                or state.get("available") is not True
            ):
                errors.append(
                    label + "unsupported/mismatched requested policy " + key
                )
            expected = effective.get(key, [])
            if (
                len(expected) != 2
                or state.get("effective") != expected[side == "B"]
            ):
                errors.append(label + "mismatched effective policy " + key)
            side_key = side, key
            # Every repetition must resolve to the same effective state.
            if side_key in side_profiles and state != side_profiles[side_key]:
                errors.append(label + "changed effective policy " + key)
            side_profiles[side_key] = state
        for key in required:
            value = run.get("counters", {}).get(key)
            if type(value) is not int or not 0 < value <= 2**64 - 1:
                errors.append(
                    label + "target path unexercised/invalid: " + key
                )
            if not known(run.get("counter_units", {}).get(key)):
                errors.append(label + "missing counter units: " + key)
        runner = run.get("runner", {})
        roles = {"bootrom", "flashrom", "eeprom", "hdd", "dvd"}
        paths = run.get("input_paths", {})
        bindings = runner.get("resource_bindings", {})
        previous_bindings = (
            runs[0].get("runner", {}).get("resource_bindings", {})
        )
        if not isinstance(paths, dict) or set(paths) != roles:
            errors.append(label + "missing input path evidence")
            paths = {}
        if not isinstance(bindings, dict) or set(bindings) != roles:
            errors.append(label + "missing resource bindings")
            bindings = {}
        for role in roles:
            path = paths.get(role)
            binding = bindings.get(role, {})
            previous = previous_bindings.get(role, {})
            if not isinstance(binding, dict) or not isinstance(previous, dict):
                errors.append(label + "invalid resource binding: " + role)
                continue
            if not isinstance(path, str) or binding.get("path") != path:
                errors.append(label + "wrong resource path: " + role)
            if path:
                if binding.get("verified") is not True or not digest(
                    binding.get("sha256")
                ):
                    errors.append(label + "unverified resource: " + role)
            elif (
                binding.get("sha256") is not None
                or binding.get("verified") is not False
            ):
                errors.append(label + "invalid unbound resource: " + role)
            if (bool(path), binding.get("sha256")) != (
                bool(previous.get("path")),
                previous.get("sha256"),
            ):
                errors.append(label + "changed resource: " + role)
        runner_id = runner.get("run_id")
        if (
            not known(runner_id)
            or runner_id in runner_ids
            or runner.get("session_id") != session
            or any(
                runner.get(k) != run.get(k)
                for k in ["executable_sha256", "input_sha256"]
            )
        ):
            errors.append(label + "unbound/duplicate runner identity")
        runner_ids.add(runner_id)
        if (
            runner.get("comparison_eligible") is not True
            or runner.get("correctness") != "passed"
        ):
            errors.append(label + "runner did not qualify the attempt")
        requested, observed = runner.get("requested_tests"), runner.get(
            "actual_tests"
        )
        if (
            not requested
            or requested != observed
            or requested != runs[0].get("runner", {}).get("requested_tests")
        ):
            errors.append(label + "wrong/missing guest test records")
        for key in [
            "baseline_sha256",
            "procedure_revision",
            "cache_policy",
            "start_policy",
            "catalog_sha256",
            "guest_settings_sha256",
        ]:
            if not runner.get(key) or runner.get(key) != runs[0].get(
                "runner", {}
            ).get(key):
                errors.append(label + "mismatched/missing runner " + key)
        for key in [
            "baseline_sha256",
            "procedure_revision",
            "catalog_sha256",
            "guest_settings_sha256",
        ]:
            if not digest(runner.get(key)):
                errors.append(label + "invalid runner digest " + key)
    return {
        "eligible": not errors,
        "errors": errors,
        "performance_verdict": "not_evaluated",
    }


def unique_keys(items):
    result = {}
    for key, value in items:
        if key in result:
            raise ValueError("Duplicate JSON key: " + key)
        result[key] = value
    return result


def read_json(path):
    if path.stat().st_size > 4 * 1024 * 1024:
        raise ValueError("Evidence file exceeds 4 MiB")
    with path.open() as stream:
        return json.load(stream, object_pairs_hook=unique_keys)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    args = parser.parse_args()
    try:
        manifest = read_json(args.manifest)
        runs = []
        pairs = manifest.pop("runs", [])
        if len(pairs) != 8:
            raise ValueError("Need eight evidence/runner sidecar pairs")
        for pair in pairs:
            evidence = read_json(args.manifest.parent / pair["evidence"])
            evidence["runner"] = read_json(
                args.manifest.parent / pair["runner"]
            )
            runs.append(evidence)
        result = validate(manifest, runs)
    except (ValueError, OSError, KeyError, TypeError, AttributeError) as error:
        result = {
            "eligible": False,
            "errors": [str(error)],
            "performance_verdict": "not_evaluated",
        }
    print(json.dumps(result, indent=2))
    return 0 if result["eligible"] else 1


if __name__ == "__main__":
    sys.exit(main())

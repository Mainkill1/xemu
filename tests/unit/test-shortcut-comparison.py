#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Admission checks for balanced evidence, without acquiring native samples."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

path = (
    Path(__file__).resolve().parents[2]
    / "scripts/validate-shortcut-comparison.py"
)
spec = importlib.util.spec_from_file_location("comparison", path)
comparison = importlib.util.module_from_spec(spec)
spec.loader.exec_module(comparison)


def fixtures(kind="setting_ab"):
    a, b = "a" * 64, ("a" if kind == "setting_ab" else "b") * 64
    contract = {
        "schema": "xemu-shortcut-comparison/v1",
        "kind": kind,
        "reference": {"executable_sha256": a, "commit": "c" * 40},
        "candidate": {"executable_sha256": b, "commit": "c" * 40},
        "policy_changes": {"vk_clean_texture_stages": ["disabled", "auto"]},
        "effective_changes": {
            "vk_clean_texture_stages": ["disabled", "enabled"]
        },
        "required_counters": ["vk.texture_bind.clean_stage_eligible"],
    }
    runs = []
    for i, side in enumerate("ABBABAAB"):
        policy = contract["policy_changes"]["vk_clean_texture_stages"][
            side == "B"
        ]
        runs.append(
            {
                "schema": "xemu-shortcut-evidence/v1",
                "session_id": f"run-{i}",
                "order_group": "experiment",
                "order": "ABBA" if i < 4 else "BAAB",
                "position": i % 4 + 1,
                "commit": "c" * 40,
                "executable_sha256": a if side == "A" else b,
                "complete": True,
                "platform": "test-host",
                "build_type": "release",
                "actual_backend": "vulkan",
                "requested_backend": "vulkan",
                "requested_gpu": "auto",
                "gpu": {
                    "available": True,
                    "device_uuid": "d" * 32,
                    "driver_uuid": "5" * 32,
                    "name": "fixture",
                    "vendor_id": 1,
                    "device_id": 2,
                    "driver_version": 3,
                    "api_version": 4,
                    "type": 1,
                },
                "base_config_sha256": "e" * 64,
                "comparison_config_sha256": "9" * 64,
                "input_paths": {
                    "bootrom": "",
                    "flashrom": "",
                    "eeprom": "/run/fixed/eeprom.bin",
                    "hdd": "",
                    "dvd": "",
                },
                "workload": "fixed-work",
                "input_sha256": "f" * 64,
                "configured_window": {"start_frame": 100, "frame_count": 20},
                "actual_window": {
                    "start_frame": 100,
                    "end_frame": 120,
                    "progress_incarnation": 1,
                    "start_monotonic_ns": 1000,
                    "end_monotonic_ns": 2000,
                },
                "wall_interval_ns": 1000,
                "wall_interval_source": "vk.owner",
                "settings": {
                    "vk_clean_texture_stages": {
                        "requested": policy,
                        "effective": "disabled" if side == "A" else "enabled",
                        "available": True,
                        "restart_pending": False,
                    },
                },
                "execution": {
                    "cpu": {"route": "tcg", "hard_fpu": True, "mttcg": False},
                    "voice": {
                        "resampler": "sinc",
                        "workers": 8,
                        "library": "fixture",
                    },
                    "dsp": {
                        "gp_engine": "jit",
                        "ep_engine": "jit",
                        "gp_realtime": True,
                        "ep_realtime": True,
                    },
                    "presentation": {
                        "transport": "shared",
                        "gl_renderer": "fixture",
                        "gl_vendor": "fixture",
                    },
                    "perturbations": {"vsync_interval": 0},
                    "renderer": {"surface_scale": 1},
                },
                "counters": {"vk.texture_bind.clean_stage_eligible": 100},
                "counter_units": {
                    "vk.texture_bind.clean_stage_eligible": "stages"
                },
                "runner": {
                    "run_id": f"actual-run-{i}",
                    "session_id": f"run-{i}",
                    "executable_sha256": a if side == "A" else b,
                    "input_sha256": "f" * 64,
                    "baseline_sha256": "1" * 64,
                    "procedure_revision": "2" * 64,
                    "cache_policy": "controlled",
                    "start_policy": "fresh-process",
                    "comparison_eligible": True,
                    "correctness": "passed",
                    "requested_tests": ["texture-dma"],
                    "actual_tests": ["texture-dma"],
                    "catalog_sha256": "3" * 64,
                    "guest_settings_sha256": "4" * 64,
                    "resource_bindings": {
                        role: {
                            "path": (
                                "/run/fixed/eeprom.bin"
                                if role == "eeprom"
                                else ""
                            ),
                            "sha256": "6" * 64 if role == "eeprom" else None,
                            "verified": role == "eeprom",
                        }
                        for role in [
                            "bootrom",
                            "flashrom",
                            "eeprom",
                            "hdd",
                            "dvd",
                        ]
                    },
                },
            }
        )
    return contract, runs


class AdmissionTests(unittest.TestCase):
    def reject(self, mutate, message):
        contract, runs = fixtures()
        mutate(contract, runs)
        result = comparison.validate(contract, runs)
        self.assertFalse(result["eligible"])
        self.assertIn(message, " ".join(result["errors"]))

    def test_private_paths_with_verified_equal_inputs(self):
        contract, runs = fixtures()
        for i, run in enumerate(runs):
            run["base_config_sha256"] = f"{i + 1:064x}"
            path = f"/run/{i}/eeprom.bin"
            run["input_paths"]["eeprom"] = path
            run["runner"]["resource_bindings"]["eeprom"]["path"] = path
        self.assertTrue(comparison.validate(contract, runs)["eligible"])

    def test_private_paths_reject_changed_input(self):
        self.reject(
            lambda c, r: r[1]["runner"]["resource_bindings"]["eeprom"].update(
                sha256="7" * 64
            ),
            "resource",
        )

    def test_private_paths_reject_unverified_input(self):
        self.reject(
            lambda c, r: r[1]["runner"]["resource_bindings"]["eeprom"].update(
                verified=False
            ),
            "resource",
        )

    def test_private_paths_reject_wrong_binding(self):
        self.reject(
            lambda c, r: r[1]["runner"]["resource_bindings"]["eeprom"].update(
                path="/other/eeprom.bin"
            ),
            "resource",
        )

    def test_comparison_settings_difference(self):
        self.reject(
            lambda c, r: r[1].update(comparison_config_sha256="0" * 64),
            "comparison_config",
        )

    def test_missing_input_path_evidence(self):
        self.reject(lambda c, r: r[1].pop("input_paths"), "input")

    def test_setting_ab(self):
        self.assertTrue(comparison.validate(*fixtures())["eligible"])

    def test_code_patch_allows_declared_binary_difference(self):
        self.assertTrue(
            comparison.validate(*fixtures("code_patch_ab"))["eligible"]
        )

    def test_declared_execution_change(self):
        contract, runs = fixtures("code_patch_ab")
        contract["execution_changes"] = {"voice.workers": [8, 4]}
        for index, side in enumerate("ABBABAAB"):
            runs[index]["execution"]["voice"]["workers"] = (
                8 if side == "A" else 4
            )
        self.assertTrue(comparison.validate(contract, runs)["eligible"])

    def test_malformed_contract_is_rejected(self):
        contract, runs = fixtures()
        contract["policy_changes"]["vk_clean_texture_stages"] = None
        self.assertFalse(comparison.validate(contract, runs)["eligible"])

    def test_setting_ab_rejects_different_binary(self):
        self.reject(
            lambda c, r: c["candidate"].update(executable_sha256="b" * 64),
            "identical executable",
        )

    def test_undeclared_hard_fpu_difference(self):
        self.reject(
            lambda c, r: r[1]["execution"]["cpu"].update(hard_fpu=False),
            "execution",
        )

    def test_actual_workers_difference(self):
        self.reject(
            lambda c, r: r[1]["execution"]["voice"].update(workers=4),
            "execution",
        )

    def test_presentation_fallback(self):
        self.reject(
            lambda c, r: r[1]["execution"]["presentation"].update(
                transport="host-copy"
            ),
            "execution",
        )

    def test_restart_pending(self):
        self.reject(
            lambda c, r: r[1]["settings"]["vk_clean_texture_stages"].update(
                restart_pending=True
            ),
            "restart",
        )

    def test_target_unexercised(self):
        self.reject(
            lambda c, r: r[1]["counters"].update(
                {"vk.texture_bind.clean_stage_eligible": 0}
            ),
            "unexercised",
        )

    def test_wrong_order(self):
        self.reject(lambda c, r: r[4].update(order="ABBA"), "order")

    def test_missing_result(self):
        self.reject(lambda c, r: r.pop(), "eight")

    def test_duplicate_session(self):
        self.reject(
            lambda c, r: r[1].update(session_id=r[0]["session_id"]),
            "duplicate",
        )

    def test_unknown_execution_is_not_zero(self):
        self.reject(lambda c, r: r[1]["execution"].pop("voice"), "execution")

    def test_wrong_guest_test_records(self):
        self.reject(
            lambda c, r: r[1]["runner"].update(actual_tests=["different"]),
            "test records",
        )

    def test_uncontrolled_cache(self):
        self.reject(
            lambda c, r: r[1]["runner"].update(comparison_eligible=False),
            "runner",
        )

    def test_input_revision_mismatch(self):
        self.reject(
            lambda c, r: r[1].update(input_sha256="0" * 64), "input_sha256"
        )

    def test_missing_driver_identity(self):
        self.reject(
            lambda c, r: [x["gpu"].pop("driver_uuid") for x in r],
            "GPU identity",
        )

    def test_unchanged_effective_policy_is_rejected(self):
        self.reject(
            lambda c, r: [
                x["settings"]["vk_clean_texture_stages"].update(
                    effective="disabled"
                )
                for x in r
            ],
            "effective policy",
        )

    def test_missing_declared_execution_path(self):
        def remove(contract, runs):
            contract["execution_changes"] = {"voice.requested_workers": [8, 4]}

        self.reject(remove, "execution path")

    def test_malformed_runner_digest(self):
        self.reject(
            lambda c, r: [x["runner"].update(catalog_sha256="bad") for x in r],
            "runner digest",
        )

    def test_cli_reopens_independent_sidecars(self):
        contract, runs = fixtures()
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            contract["runs"] = []
            for i, run in enumerate(runs):
                evidence, runner = f"evidence-{i}.json", f"runner-{i}.json"
                (root / runner).write_text(json.dumps(run.pop("runner")))
                (root / evidence).write_text(json.dumps(run))
                contract["runs"].append(
                    {"evidence": evidence, "runner": runner}
                )
            manifest = root / "comparison.json"
            manifest.write_text(json.dumps(contract))
            result = subprocess.run(
                [sys.executable, str(path), str(manifest)],
                capture_output=True,
                text=True,
            )
            self.assertEqual(
                result.returncode, 0, result.stdout + result.stderr
            )
            self.assertTrue(json.loads(result.stdout)["eligible"])
            (root / "runner-7.json").write_text('{"correctness":"failed"}')
            result = subprocess.run(
                [sys.executable, str(path), str(manifest)],
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 1)
            self.assertFalse(json.loads(result.stdout)["eligible"])

    def test_unknown_backend(self):
        self.reject(
            lambda c, r: [x.update(actual_backend="unknown") for x in r],
            "backend",
        )

    def test_empty_actual_route(self):
        self.reject(
            lambda c, r: [x["execution"]["cpu"].update(route="") for x in r],
            "execution",
        )

    def test_missing_counter_units(self):
        self.reject(
            lambda c, r: [x.update(counter_units={}) for x in r], "units"
        )

    def test_signed_window_time(self):
        self.reject(
            lambda c, r: [
                x["actual_window"].update(
                    start_monotonic_ns=-2, end_monotonic_ns=-1
                )
                for x in r
            ],
            "window timing",
        )

    def test_missing_progress_incarnation(self):
        self.reject(
            lambda c, r: [
                x["actual_window"].update(progress_incarnation=0) for x in r
            ],
            "window timing",
        )

    def test_inconsistent_wall_interval(self):
        self.reject(
            lambda c, r: [x.update(wall_interval_ns=999) for x in r],
            "window timing",
        )

    def test_different_actual_workload(self):
        self.reject(
            lambda c, r: r[1]["runner"].update(
                requested_tests=["different"], actual_tests=["different"]
            ),
            "test records",
        )

    def test_reused_runner_record(self):
        self.reject(
            lambda c, r: r[1].update(runner=r[0]["runner"]), "runner identity"
        )

    def test_sidecar_bound_to_actual_executable(self):
        self.reject(
            lambda c, r: r[1]["runner"].update(executable_sha256="0" * 64),
            "runner identity",
        )

    def test_malformed_required_context(self):
        cases = [
            (
                lambda r: r["execution"]["renderer"].update(surface_scale=-1),
                "execution",
            ),
            (
                lambda r: r["execution"]["perturbations"].update(
                    vsync_interval={}
                ),
                "execution",
            ),
            (
                lambda r: r["execution"]["voice"].update(library=False),
                "execution",
            ),
            (
                lambda r: r["execution"]["presentation"].update(
                    transport="not_observed"
                ),
                "execution",
            ),
            (lambda r: r["gpu"].update(name=True), "GPU identity"),
            (
                lambda r: r["settings"]["vk_clean_texture_stages"].pop(
                    "restart_pending"
                ),
                "setting state",
            ),
        ]
        for mutate, message in cases:
            with self.subTest(message=message):
                self.reject(lambda c, runs: [mutate(r) for r in runs], message)
        for key in [
            "platform",
            "build_type",
            "requested_backend",
            "requested_gpu",
            "workload",
        ]:
            with self.subTest(field=key):
                self.reject(
                    lambda c, r: [x.update({key: None}) for x in r], "context"
                )


if __name__ == "__main__":
    unittest.main()

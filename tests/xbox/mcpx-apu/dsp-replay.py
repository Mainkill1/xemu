#!/usr/bin/env python3
"""Run narrow JIT checkpoint comparisons in a timed child process."""

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import signal
import subprocess
import tempfile

try:
    import resource
except ImportError:
    resource = None

from dsp_corpus import _open_regular

FRAME_FILES = (
    ("gp_initial", 41554), ("ep_initial", 41554),
    ("gp_expected", 41554), ("ep_expected", 41554),
    ("mixbins", 4096), ("prior_pcm", 1024), ("expected_pcm", 1024),
    ("gp_memory", 16 * 1024 * 1024), ("ep_memory", 16 * 1024 * 1024),
    ("gp_expected_memory", 16 * 1024 * 1024),
    ("ep_expected_memory", 16 * 1024 * 1024),
    ("events", 4 * 1024 * 1024 + 4096 * 24 + 8),
)
MAPPED_FILES = FRAME_FILES[:7] + (
    ("apu_registers", 0x80000), ("expected_apu_registers", 0x80000),
    ("ram", 128 * 1024 * 1024), ("expected_ram", 128 * 1024 * 1024),
    ("control", 16),
)


def file_hash(path, limit):
    digest = hashlib.sha256()
    total = 0
    with _open_regular(Path(path), limit) as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            total += len(block)
            if total > limit:
                raise ValueError("file grew beyond limit")
            digest.update(block)
    return digest.hexdigest()


def verify_core(args):
    dual = args.command == "verify-engines"
    sequence = args.command == "verify-sequence"
    frame = args.command in ("verify-frame", "verify-sequence")
    mapped_trace = args.command == "verify-mapped-trace"
    mapped_sequence = args.command in ("verify-mapped-sequence", "verify-mapped-trace")
    mapped = args.command in ("verify-mapped-frame", "verify-mapped-sequence", "verify-mapped-trace")
    scratch = args.command == "verify-scratch"
    report = {"status": "BASELINE_MATCH", "scope": (
              "synthetic-gp-ep-engine-comparison" if dual else
              "gp-ep-mapped-trace" if mapped_trace else
              "gp-ep-mapped-sequence" if mapped_sequence else
              "gp-ep-mapped-frame" if mapped else
              "gp-ep-sequence-gp-monitor" if sequence else
              "gp-ep-frame-gp-monitor" if frame else
              "jit-core-scratch-io" if scratch else "jit-core-no-external-io"),
              "reference_authority": "UNVERIFIED",
              "linked_dsp_library": {"status": "UNVERIFIED"},
              "requested": args.repeat, "completed": 0}
    if dual:
        report.update(engines=[args.reference, args.candidate], fixture=args.fixture,
                      frames=args.frames, value=args.value, signal=args.signal,
                      injected_fault=args.inject_fault,
                      injected_pcm_fault=args.inject_pcm_fault,
                      injected_request_fault=args.inject_request_fault,
                      injected_memory_fault=args.inject_memory_fault,
                      injected_register_fault=args.inject_register_fault,
                      injected_both_skip_dma=args.inject_both_skip_dma,
                      injected_both_pcm_silence=args.inject_both_pcm_silence,
                      injected_both_corrupt_event=args.inject_both_corrupt_event,
                      injected_both_corrupt_ram=args.inject_both_corrupt_ram,
                      injected_architectural_fault=args.inject_architectural_fault,
                      injected_initial_state_fault=args.inject_initial_state_fault,
                      injected_address_fault=args.inject_address_fault,
                      injected_stack_fault=args.inject_stack_fault,
                      injected_native_interrupt_fault=args.inject_native_interrupt_fault)
    elif frame:
        report["phase"] = args.phase
        report["cycles_added_per_active_dsp"] = 1000
        if sequence:
            report["frames"] = args.frames
    elif not mapped:
        report["instruction_limit"] = args.steps
    if mapped_sequence:
        report["frames"] = args.frames
    if os.name != "posix" or resource is None or not all(
            hasattr(resource, name) for name in
            ("RLIMIT_AS", "RLIMIT_CPU", "RLIMIT_FSIZE", "RLIMIT_CORE")):
        report["status"] = "UNSUPPORTED"
        print(json.dumps(report, sort_keys=True))
        return 1

    def limit_pair(kind, soft, hard):
        inherited_hard = resource.getrlimit(kind)[1]
        if inherited_hard != resource.RLIM_INFINITY:
            hard = min(hard, inherited_hard)
        return min(soft, hard), hard

    limits = {
        resource.RLIMIT_AS: limit_pair(resource.RLIMIT_AS,
                                     args.memory_mib * 1024 * 1024,
                                     args.memory_mib * 1024 * 1024),
        resource.RLIMIT_CPU: limit_pair(resource.RLIMIT_CPU,
                                      args.cpu_seconds, args.cpu_seconds + 1),
        resource.RLIMIT_FSIZE: limit_pair(resource.RLIMIT_FSIZE, 65536, 65536),
        resource.RLIMIT_CORE: (0, 0),
    }
    report["limits"] = {"memory_bytes": limits[resource.RLIMIT_AS][0],
                        "cpu_seconds": limits[resource.RLIMIT_CPU][0],
                        "output_bytes": limits[resource.RLIMIT_FSIZE][0],
                        "wall_seconds": args.timeout}

    def apply_limits():
        for kind, values in limits.items():
            resource.setrlimit(kind, values)
    paths = {"worker_sha256": (args.worker, 1024 * 1024 * 1024)}
    if mapped:
        mapped_limits = {"mixbins": args.frames * 4096,
                         "expected_pcm": args.frames * 1024} if mapped_sequence else {}
        paths.update({name + "_sha256": (getattr(args, name), mapped_limits.get(name, limit))
                      for name, limit in MAPPED_FILES})
        if mapped_trace:
            paths["events_sha256"] = (args.events, 4 * 1024 * 1024 + 4096 * 24 + 8)
            if args.gp_frame_states:
                for name in ("gp_frame_states", "ep_frame_states"):
                    paths[name + "_sha256"] = (getattr(args, name), args.frames * 41554)
    elif frame:
        sequence_limits = {"mixbins": args.frames * 4096,
                           "expected_pcm": args.frames * 128,
                           "events": 4 * 1024 * 1024 + 4096 * 24 + 8 + 12 * args.frames} if sequence else {}
        paths.update({name + "_sha256": (getattr(args, name), sequence_limits.get(name, limit))
                      for name, limit in FRAME_FILES})
    elif not dual:
        paths.update({"initial_sha256": (args.initial, 41554),
                      "expected_sha256": (args.expected, 41554)})
    if scratch:
        paths.update({"memory_sha256": (args.memory, 16 * 1024 * 1024),
                      "expected_memory_sha256": (args.expected_memory, 16 * 1024 * 1024),
                      "events_sha256": (args.events, 4 * 1024 * 1024 + 4096 * 24 + 8)})
    try:
        identities = {name: file_hash(path, limit)
                      for name, (path, limit) in paths.items()}
    except (OSError, ValueError):
        report["status"] = "INCOMPLETE"
        print(json.dumps(report, sort_keys=True))
        return 1
    report["identities"] = identities
    for _ in range(args.repeat):
        try:
            if dual:
                command = [args.worker, "--engines", args.fixture, str(args.frames),
                           str(args.value), "1" if args.inject_fault else "0",
                           args.reference, args.candidate, "1" if args.inject_pcm_fault else "0",
                           "1" if args.inject_request_fault else "0",
                           "1" if args.inject_memory_fault else "0",
                           "1" if args.inject_register_fault else "0",
                           "1" if args.inject_both_skip_dma else "0",
                           "1" if args.inject_both_pcm_silence else "0",
                           "1" if args.inject_both_corrupt_event else "0",
                           "1" if args.inject_both_corrupt_ram else "0",
                           "1" if args.inject_architectural_fault else "0",
                           "1" if args.inject_initial_state_fault else "0",
                           {None: "0", "r0": "16", "n0": "24", "m0": "32", "sr": "57"}[
                               args.inject_address_fault],
                           {None: "0", "gp0": "1", "gp1": "2", "ep0": "3", "ep1": "4"}[
                               args.inject_stack_fault],
                           {None: "0", "gp": "1", "ep": "2"}[
                               args.inject_native_interrupt_fault], args.signal]
            elif mapped:
                operation = "--mapped-trace" if mapped_trace else (
                    "--mapped-sequence" if mapped_sequence else "--mapped-frame")
                command = [args.worker, operation] + [
                    getattr(args, name) for name, _ in MAPPED_FILES]
                if mapped_sequence:
                    command.append(str(args.frames))
                if mapped_trace:
                    command.append(args.events)
                    if args.gp_frame_states:
                        command[1] = "--mapped-trace-checkpoints"
                        command.extend([args.gp_frame_states, args.ep_frame_states])
            elif frame:
                command = [args.worker, "--sequence" if sequence else "--frame"] + [
                    getattr(args, name) for name, _ in FRAME_FILES] + [str(args.phase)]
                if sequence:
                    command.append(str(args.frames))
            else:
                command = [args.worker, args.initial, args.expected, str(args.steps)]
                if scratch:
                    command.extend([args.memory, args.expected_memory, args.events])
            with tempfile.TemporaryFile() as stdout, tempfile.TemporaryFile() as stderr:
                child = subprocess.run(command, stdin=subprocess.DEVNULL,
                                       stdout=stdout, stderr=stderr,
                                       preexec_fn=apply_limits,
                                       timeout=args.timeout, check=False)
                stdout.seek(0)
                worker_output = stdout.read(65537).decode("utf-8")
        except subprocess.TimeoutExpired:
            report["status"] = "TIMEOUT"
            break
        except (OSError, UnicodeError, subprocess.SubprocessError):
            report["status"] = "CRASH"
            break
        try:
            current = {name: file_hash(path, limit)
                       for name, (path, limit) in paths.items()}
        except (OSError, ValueError):
            current = None
        if current != identities:
            report["status"] = "INCONCLUSIVE"
            break
        if hasattr(signal, "SIGXCPU") and child.returncode == -signal.SIGXCPU:
            report["status"] = "TIMEOUT"
            break
        try:
            result = json.loads(worker_output)
            status = result["status"]
            if status not in ("BASELINE_MATCH", "MISMATCH", "TIMEOUT",
                              "UNSUPPORTED", "INCOMPLETE", "INVALID"):
                raise ValueError("unknown worker outcome")
            expected_exit = {"BASELINE_MATCH": 0, "MISMATCH": 1,
                             "TIMEOUT": 2, "UNSUPPORTED": 2,
                             "INCOMPLETE": 2, "INVALID": 2}[status]
            if child.returncode != expected_exit:
                raise ValueError("inconsistent worker outcome")
        except (ValueError, KeyError, TypeError):
            report["status"] = "CRASH"
            break
        library = result.get("library", {"status": "UNVERIFIED"})
        if not isinstance(library, dict):
            report["status"] = "INCONCLUSIVE"
            break
        if library.get("status") == "BUILD_RECORDED":
            if (not isinstance(library.get("version"), str) or
                    not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:[-+][0-9A-Za-z.-]+)?", library["version"]) or
                    not isinstance(library.get("sha256"), str) or
                    not re.fullmatch(r"[0-9a-f]{64}", library["sha256"])):
                report["status"] = "INCONCLUSIVE"
                break
        elif library.get("status") == "UNVERIFIED":
            library = {"status": "UNVERIFIED"}
        else:
            report["status"] = "INCONCLUSIVE"
            break
        if report["completed"] and report["linked_dsp_library"] != library:
            report["status"] = "INCONCLUSIVE"
            break
        report["linked_dsp_library"] = library
        if dual and status == "BASELINE_MATCH" and any((
                args.inject_fault, args.inject_pcm_fault, args.inject_request_fault,
                args.inject_memory_fault, args.inject_register_fault,
                args.inject_both_skip_dma, args.inject_both_pcm_silence,
                args.inject_both_corrupt_event, args.inject_both_corrupt_ram,
                args.inject_architectural_fault, args.inject_initial_state_fault,
                args.inject_address_fault, args.inject_stack_fault,
                args.inject_native_interrupt_fault)):
            report["status"] = "SELF_TEST_FAILED"
            break
        if status != "BASELINE_MATCH":
            # Identical recorded inputs have already matched in this run.
            # A later divergence is inconsistent behavior, not an ordinary
            # mismatch that can be attributed to the fixed reference alone.
            report["status"] = ("NONDETERMINISTIC" if status == "MISMATCH" and
                                report["completed"] else status)
            if status == "MISMATCH" and isinstance(result.get("difference"), dict):
                report["difference"] = result["difference"]
            if (dual and status == "UNSUPPORTED" and result.get("reason") == "INITIAL_STATE_MISMATCH"
                    and isinstance(result.get("initial_difference"), dict)):
                report["reason"] = result["reason"]
                report["initial_difference"] = result["initial_difference"]
            if dual and status == "INVALID" and result.get("reason") == "unsupported fixture or signal":
                report["reason"] = result["reason"]
            if dual and status == "MISMATCH" and isinstance(result.get("post_subframe_pc"), dict):
                pcs = result["post_subframe_pc"]
                if (set(pcs) == {"reference_gp", "reference_ep", "candidate_gp", "candidate_ep"}
                        and all(type(pc) is int and 0 <= pc <= 0xffffff for pc in pcs.values())):
                    report["post_subframe_pc"] = pcs
            failure_frame = result.get("failure_frame")
            if ((mapped_sequence or dual) and type(failure_frame) is int and
                    0 <= failure_frame < args.frames):
                report["failure_frame"] = failure_frame
            break
        if dual:
            observations = result.get("observations")
            if (not isinstance(observations, dict) or
                    type(observations.get("io_events")) is not int or
                    observations["io_events"] < 0 or
                    type(observations.get("pcm_nonzero_comparisons")) is not int or
                    observations["pcm_nonzero_comparisons"] < 0 or
                    type(observations.get("dsp_memory_word_comparisons")) is not int or
                    observations["dsp_memory_word_comparisons"] < 0 or
                    type(observations.get("architectural_comparisons")) is not int or
                    observations["architectural_comparisons"] < 0 or
                    type(observations.get("native_checkpoint_comparisons")) is not int or
                    observations["native_checkpoint_comparisons"] != (
                        2 * (args.frames + 1) if args.reference == args.candidate else 0) or
                    observations.get("initial_subset_checked") is not True or
                    (report["completed"] and report.get("observations") != observations)):
                report["status"] = "INCONCLUSIVE"
                break
            report["observations"] = observations
        report["completed"] += 1
    print(json.dumps(report, sort_keys=True))
    return 0 if report["status"] == "BASELINE_MATCH" else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    for name in ("verify-core", "verify-scratch", "verify-frame", "verify-sequence",
                 "verify-mapped-frame", "verify-mapped-sequence", "verify-mapped-trace",
                 "verify-engines"):
        verify = subparsers.add_parser(name)
        verify.add_argument("--worker", required=True)
        if name == "verify-engines":
            verify.add_argument("--fixture", choices=("scratch", "fifo", "ep-sink"), required=True)
            verify.add_argument("--frames", type=int, required=True)
            verify.add_argument("--value", type=lambda s: int(s, 0), required=True)
            verify.add_argument("--inject-fault", action="store_true")
            verify.add_argument("--inject-pcm-fault", action="store_true")
            verify.add_argument("--inject-request-fault", action="store_true")
            verify.add_argument("--inject-memory-fault", action="store_true")
            verify.add_argument("--inject-register-fault", action="store_true")
            verify.add_argument("--inject-both-skip-dma", action="store_true")
            verify.add_argument("--inject-both-pcm-silence", action="store_true")
            verify.add_argument("--inject-both-corrupt-event", action="store_true")
            verify.add_argument("--inject-both-corrupt-ram", action="store_true")
            verify.add_argument("--inject-architectural-fault", action="store_true")
            verify.add_argument("--inject-initial-state-fault", action="store_true")
            verify.add_argument("--inject-address-fault", choices=("r0", "n0", "m0", "sr"))
            verify.add_argument("--inject-stack-fault", choices=("gp0", "gp1", "ep0", "ep1"))
            verify.add_argument("--inject-native-interrupt-fault", choices=("gp", "ep"))
            verify.add_argument("--signal", choices=("impulse", "silence", "alternating", "ramp",
                                                      "left", "right", "extremes", "near-zero",
                                                      "deterministic-noise", "swept-sine"),
                                default="impulse")
            verify.add_argument("--reference", choices=("jit", "interpreter"), default="jit")
            verify.add_argument("--candidate", choices=("jit", "interpreter"), default="interpreter")
        elif name in ("verify-frame", "verify-sequence", "verify-mapped-frame", "verify-mapped-sequence", "verify-mapped-trace"):
            for field, _ in (MAPPED_FILES if name.startswith("verify-mapped-") else FRAME_FILES):
                verify.add_argument("--" + field.replace("_", "-"), required=True)
            if not name.startswith("verify-mapped-"):
                verify.add_argument("--phase", type=int, required=True)
            if name in ("verify-sequence", "verify-mapped-sequence", "verify-mapped-trace"):
                verify.add_argument("--frames", type=int, required=True)
            if name == "verify-mapped-trace":
                verify.add_argument("--events", required=True)
                verify.add_argument("--gp-frame-states")
                verify.add_argument("--ep-frame-states")
        else:
            verify.add_argument("--initial", required=True)
            verify.add_argument("--expected", required=True)
            verify.add_argument("--steps", type=int, required=True)
        verify.add_argument("--repeat", type=int, default=1)
        verify.add_argument("--timeout", type=float, default=10.0)
        verify.add_argument("--memory-mib", type=int, default=2048)
        verify.add_argument("--cpu-seconds", type=int, default=30)
        if name == "verify-scratch":
            verify.add_argument("--memory", required=True)
            verify.add_argument("--expected-memory", required=True)
            verify.add_argument("--events", required=True)
    args = parser.parse_args()
    if args.command == "verify-mapped-trace" and bool(args.gp_frame_states) != bool(args.ep_frame_states):
        parser.error("GP and EP frame states must be supplied together")
    if args.command == "verify-engines":
        valid_execution = 1 <= args.frames <= 1024 and 0 <= args.value <= 0xffffff
    elif args.command == "verify-mapped-frame":
        valid_execution = True
    elif args.command in ("verify-mapped-sequence", "verify-mapped-trace"):
        valid_execution = 1 <= args.frames <= 1024
    elif args.command in ("verify-frame", "verify-sequence"):
        valid_execution = (0 <= args.phase <= 7 and
                           (args.command != "verify-sequence" or 1 <= args.frames <= 1024))
    else:
        valid_execution = 1 <= args.steps <= 1000000
    if not (valid_execution and 1 <= args.repeat <= 1000 and
            math.isfinite(args.timeout) and 0 < args.timeout <= 3600 and
            128 <= args.memory_mib <= 16384 and 1 <= args.cpu_seconds <= 3600):
        parser.error("steps, repeat, timeout, memory or CPU limit outside supported range")
    return verify_core(args)


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare two builds of the retained production DSP test/benchmark.

Keeps A/A controls, ABBA and BAAB attempts, executable identities and every
raw result. This is a CPU arithmetic/instruction benchmark, not a game test.
"""
import argparse
import hashlib
import json
import os
import pathlib
import platform
import statistics
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--parent", required=True, type=pathlib.Path)
    parser.add_argument("--candidate", required=True, type=pathlib.Path)
    parser.add_argument("--out", required=True, type=pathlib.Path)
    parser.add_argument("--cpu", type=int)
    parser.add_argument("--rounds", type=int, default=8)
    parser.add_argument("--helper-iterations", type=int, default=50_000_000)
    parser.add_argument(
        "--instruction-iterations", type=int, default=10_000_000
    )
    args = parser.parse_args()
    if not 1 <= args.rounds <= 32:
        parser.error("--rounds must be 1..32")
    for count in (args.helper_iterations, args.instruction_iterations):
        if not 1 <= count <= 1_000_000_000:
            parser.error("iteration counts must be 1..1000000000")
    if args.cpu is not None:
        if not hasattr(os, "sched_setaffinity"):
            parser.error("CPU affinity is unavailable on this platform")
        os.sched_setaffinity(0, {args.cpu})
    args.out.mkdir(parents=True, exist_ok=False)
    binaries = {"A": args.parent.resolve(), "B": args.candidate.resolve()}
    manifest = {
        "schema": 1,
        "scope": (
            "retained-C-DSP arithmetic and synthetic instruction execution"
        ),
        "platform": platform.platform(),
        "cpuAffinity": sorted(os.sched_getaffinity(0))
        if hasattr(os, "sched_getaffinity") else None,
        "frequencyPolicy": (
            "not changed; frequency and shared-host contention uncontrolled"
        ),
        "rounds": args.rounds,
        "binaries": {
            key: {
                "path": str(path),
                "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            }
            for key, path in binaries.items()
        },
    }
    (args.out / "manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n"
    )
    rows = []
    raw = args.out / "attempts.jsonl"

    def run(mode, count, variant, order, round_number, position):
        command = [str(binaries[variant]), "--benchmark", mode, str(count)]
        result = subprocess.run(command, capture_output=True, text=True)
        row = {"mode": mode, "variant": variant, "order": order,
               "round": round_number, "position": position,
               "command": command, "exitCode": result.returncode,
               "stdout": result.stdout, "stderr": result.stderr}
        try:
            row["measurement"] = json.loads(result.stdout)
        except json.JSONDecodeError:
            row["measurement"] = None
        rows.append(row)
        with raw.open("a") as stream:
            stream.write(json.dumps(row) + "\n")
        if result.returncode or row["measurement"] is None:
            raise RuntimeError(f"Failed attempt retained in {raw}")
        measured = row["measurement"]
        if measured["mode"] != mode or measured["iterations"] != count:
            raise RuntimeError("Benchmark reported a different workload")
        if measured["elapsedUs"] <= 0 or measured["nsPerOperation"] <= 0:
            raise RuntimeError("Elapsed time is below clock resolution")

    summary = {"schema": 1, "results": []}
    for mode in ("helper", "helper-call", "mac", "mixed"):
        count = (args.helper_iterations if mode.startswith("helper")
                 else args.instruction_iterations)
        for position in range(4):
            run(mode, count, "A", "AAAA", 0, position + 1)
        for round_number in range(1, args.rounds + 1):
            for order in ("ABBA", "BAAB"):
                for position, variant in enumerate(order, 1):
                    run(mode, count, variant, order, round_number, position)
        mode_rows = [row for row in rows if row["mode"] == mode]
        if len({row["measurement"]["checksum"] for row in mode_rows}) != 1:
            raise RuntimeError(
                "Checksum mismatch; all attempts retained, no speedup certified"
            )
        warmups = {row["measurement"]["warmupChecksum"] for row in mode_rows}
        if len(warmups) != 1:
            raise RuntimeError(
                "Warmup checksum mismatch; all attempts retained"
            )
        control = [row["measurement"]["nsPerOperation"] for row in mode_rows
                   if row["order"] == "AAAA"]
        item = {"mode": mode, "iterations": count,
                "checksum": mode_rows[0]["measurement"]["checksum"],
                "aaMinNs": min(control), "aaMaxNs": max(control), "orders": {}}
        for order in ("ABBA", "BAAB"):
            samples = {
                variant: [
                    row["measurement"]["nsPerOperation"] for row in mode_rows
                    if row["order"] == order and row["variant"] == variant
                ]
                for variant in ("A", "B")
            }
            a = statistics.median(samples["A"])
            b = statistics.median(samples["B"])
            item["orders"][order] = {
                                     "parentMedianNs": a,
                                     "candidateMedianNs": b,
                                     "timeReductionPercent": (a - b) / a * 100,
                                     "samplesPerVariant": len(samples["A"])}
        summary["results"].append(item)
        (args.out / "summary.json").write_text(
            json.dumps(summary, indent=2) + "\n"
        )
        print(json.dumps(item), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

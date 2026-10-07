#!/usr/bin/env python3
"""Balanced replay of two builds of test-xbox-mcpx-dsp-dispatch.

Retains every run and verifies architectural-state digests before comparing
elapsed batch time. This is a local interpreter experiment, not a game test.
"""
import argparse
import hashlib
import json
import os
import platform
import statistics
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("parent", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--iterations", type=int, default=5000000)
    parser.add_argument("--blocks", type=int, default=8)
    parser.add_argument("--cpu", type=int, default=None)
    args = parser.parse_args()
    if not 1 <= args.iterations <= 1000000000 or args.blocks < 1:
        parser.error("Invalid iteration/block count")
    if args.cpu is not None:
        os.sched_setaffinity(0, {args.cpu})
    binaries = {"A": args.parent.resolve(), "B": args.candidate.resolve()}
    # A campaign is immutable. Refuse a reused destination before writing any
    # manifest or result, including after an interrupted/failed campaign.
    args.output.mkdir(parents=True, exist_ok=False)
    manifest = {
        "platform": platform.platform(),
        "affinity": sorted(os.sched_getaffinity(0)),
        "powerPolicy": "unchanged; frequency and shared-host activity uncontrolled",
        "iterations": args.iterations,
        "blocksPerOrder": args.blocks,
        "binaries": {
            key: {"path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
            for key, path in binaries.items()
        },
        "scope": "synthetic production C-interpreter replay; not game FPS",
    }
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    records = []
    summaries = []
    for mode in ("normal", "parallel", "mixed", "working-set", "cold", "updates"):
        expected_digest = None
        for order in ("AAAA", "ABBA", "BAAB"):
            selected = []
            for block in range(args.blocks):
                for position, role in enumerate(order):
                    command = [str(binaries[role]), "--benchmark", mode, str(args.iterations)]
                    result = subprocess.run(command, capture_output=True, text=True)
                    record = {
                        "mode": mode, "order": order, "block": block,
                        "position": position, "role": role, "command": command,
                        "exitCode": result.returncode,
                        "stdout": result.stdout, "stderr": result.stderr,
                    }
                    records.append(record)
                    # Save even a failing attempt before raising.
                    (args.output / "runs.json").write_text(json.dumps(records, indent=2) + "\n")
                    if result.returncode != 0:
                        raise RuntimeError(f"Replay failed: {record}")
                    data = json.loads(result.stdout)
                    record["result"] = data
                    if expected_digest is None:
                        expected_digest = data["stateDigest"]
                    if data["stateDigest"] != expected_digest:
                        raise RuntimeError(f"Architectural state mismatch: {record}")
                    selected.append(record)
            groups = {
                role: [r["result"]["nsPerInstruction"] for r in selected if r["role"] == role]
                for role in ("A", "B")
            }
            summary = {"mode": mode, "order": order, "stateDigest": expected_digest}
            for role, values in groups.items():
                if values:
                    summary[role] = {
                        "samples": len(values), "medianNs": statistics.median(values),
                        "minimumNs": min(values), "maximumNs": max(values),
                    }
            if groups["B"]:
                parent = summary["A"]["medianNs"]
                candidate = summary["B"]["medianNs"]
                summary["savedNs"] = parent - candidate
                summary["improvementPercent"] = 100 * (parent - candidate) / parent
            summaries.append(summary)
            (args.output / "summary.json").write_text(json.dumps(summaries, indent=2) + "\n")
            print(json.dumps(summary), flush=True)
    (args.output / "runs.json").write_text(json.dumps(records, indent=2) + "\n")


if __name__ == "__main__":
    main()

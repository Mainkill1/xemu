#!/usr/bin/env python3
"""Summarize a tab-separated perf self report; never add rounded percentages."""

import argparse
import json
from pathlib import Path
import re


def summarize(report):
    rows = []
    for line in report.splitlines():
        fields = [field.strip() for field in line.split("\t")]
        if len(fields) < 6 or not re.fullmatch(r"\d+(?:\.\d+)?%", fields[0]):
            continue
        rows.append({
            "percent": float(fields[0][:-1]),
            "samples": int(fields[1]),
            "period": int(fields[2]),
            "comm": fields[3],
            "dso": fields[4],
            "symbol": re.sub(r"^\[\.\]\s+", "", fields[5]),
        })
    event = re.search(r"event '([^']+)'", report)
    lost = re.search(r"Total Lost Samples:\s+(\d+)", report)
    recorded = re.search(r"Event count \(approx\.\):\s+(\d+)", report)
    if not rows or not event or not lost or not recorded:
        raise ValueError("Expected a complete perf report with event/period/loss headers")
    total = sum(row["period"] for row in rows)
    if total != int(recorded[1]):
        raise ValueError("Rows omit recorded periods; use --percent-limit 0")
    groups = {
        "xemu DSO": lambda r: r["dso"] == "xemu",
        "libsamplerate DSO": lambda r: "libsamplerate" in r["dso"],
        "named C/DSP functions": lambda r: r["dso"] == "xemu" and
            re.match(r"^(dsp_|dsp56k_|emu_)", r["symbol"]),
        "multiply-family instruction functions": lambda r: r["dso"] == "xemu" and
            re.match(r"^emu_(mac|mpy)", r["symbol"]),
    }
    summary = {
        "event": event[1], "samples": sum(r["samples"] for r in rows),
        "lostSamples": int(lost[1]), "period": total,
        "groupingsOverlap": True, "selfOnly": True, "groups": {},
    }
    for name, predicate in groups.items():
        selected = [row for row in rows if predicate(row)]
        period = sum(row["period"] for row in selected)
        summary["groups"][name] = {
            "samples": sum(row["samples"] for row in selected),
            "period": period, "percent": 100 * period / total,
        }
    return rows, summary


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    rows, summary = summarize(args.report.read_text())
    args.out.mkdir(parents=True, exist_ok=False)
    for name, value in [("rows.json", rows), ("summary.json", summary)]:
        (args.out / name).write_text(json.dumps(value, indent=2) + "\n")
    print(json.dumps(summary, indent=2))

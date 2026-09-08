#!/usr/bin/env python3
"""Reproduce the archived Morrowind cadence statistics without running xemu.

Usage: python3 analyze-morrowind.py [directory-containing-export-and-manifest]
Prints CSV. Input timestamps are microseconds relative to each sealed window.
Cadence is a display-write proxy, not rendered FPS. No outliers are excluded.
"""
import csv
import json
import math
from pathlib import Path
import statistics
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).parent
manifest = json.loads((root / "morrowind-review-manifest.json").read_text())
cells = {row["cell"]: [] for row in manifest["cells"]}
with (root / "morrowind-event-times.csv").open(newline="") as source:
    for row in csv.DictReader(source):
        cells[row["cell"]].append(int(row["timestamp_us_since_window_start"]))
writer = csv.writer(sys.stdout)
writer.writerow(["cell", "events", "cadence_proxy", "mean_ms", "median_ms",
                 "p95_ms", "p99_ms", "max_ms", "gaps_over_100ms"])
for record in manifest["cells"]:
    times = cells[record["cell"]]
    if len(times) < 2 or any(b <= a for a, b in zip(times, times[1:])):
        raise ValueError("Missing or non-increasing events: " + record["cell"])
    intervals = [(b - a) / 1000 for a, b in zip(times, times[1:])]
    ordered = sorted(intervals)
    cadence = len(intervals) * 1000000 / (times[-1] - times[0])
    p95 = ordered[math.ceil(0.95 * len(ordered)) - 1]
    p99 = ordered[math.ceil(0.99 * len(ordered)) - 1]
    for actual, key in [(cadence, "average_fps_proxy"),
                        (p95, "frame_interval_p95_ms"),
                        (p99, "frame_interval_p99_ms")]:
        if not math.isclose(actual, record[key], abs_tol=1e-6):
            raise ValueError("Published statistic mismatch: " + key)
    writer.writerow([record["cell"], len(times), f"{cadence:.6f}",
                     f"{statistics.mean(intervals):.6f}",
                     f"{statistics.median(intervals):.6f}",
                     f"{p95:.6f}", f"{p99:.6f}",
                     f"{max(intervals):.6f}",
                     sum(value > 100 for value in intervals)])

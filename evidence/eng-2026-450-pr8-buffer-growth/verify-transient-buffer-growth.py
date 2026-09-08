#!/usr/bin/env python3
"""Verify PR #8 test-only production-path transient-buffer events."""

import argparse
import csv
import json
from pathlib import Path


INITIAL_CAPACITY = 8 * 1024 * 1024
SMALL_DRAW = 128
LARGE_DRAW = 2_097_024
EXPECTED_GROWTHS = [
    INITIAL_CAPACITY + SMALL_DRAW,
    INITIAL_CAPACITY + 2 * SMALL_DRAW,
    INITIAL_CAPACITY + 3 * SMALL_DRAW,
    INITIAL_CAPACITY + 3 * SMALL_DRAW + LARGE_DRAW,
]


def integer(row: dict[str, str], key: str) -> int:
    return int(row[key])


def verify(path: Path) -> dict[str, object]:
    with path.open(newline="", encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle))

    failures: list[str] = []
    initial_rows = [row for row in rows if row["event"] == "initial"]
    if len(initial_rows) != 1:
        failures.append(f"expected one initial event, observed {len(initial_rows)}")
        staging_index = -1
    else:
        initial = initial_rows[0]
        staging_index = integer(initial, "index")
        if integer(initial, "buffer_size") != INITIAL_CAPACITY:
            failures.append("initial staging capacity was not 8 MiB")
        if integer(initial, "paired_size") != INITIAL_CAPACITY:
            failures.append("initial device capacity did not match staging")
        if integer(initial, "buffer_mapped") != 1:
            failures.append("initial staging buffer was not mapped")
        if integer(initial, "paired_mapped") != 0:
            failures.append("device-local pair was unexpectedly mapped")

    staging_rows = [
        row for row in rows if integer(row, "index") == staging_index
    ]
    requests = [row for row in staging_rows if row["event"] == "request"]
    required_requests = {integer(row, "required_size") for row in requests}
    for boundary, label in (
        (INITIAL_CAPACITY - 512, "just-below capacity"),
        (INITIAL_CAPACITY, "exact capacity"),
    ):
        if boundary not in required_requests:
            failures.append(f"missing {label} request {boundary}")

    completed = [
        row for row in staging_rows if row["event"] == "ensure-complete"
    ]
    observed_growths = [integer(row, "required_size") for row in completed]
    if observed_growths != EXPECTED_GROWTHS:
        failures.append(
            f"growth sequence {observed_growths} != {EXPECTED_GROWTHS}"
        )

    submit_counts = [integer(row, "submit_count") for row in completed]
    if any(right <= left for left, right in zip(submit_counts, submit_counts[1:])):
        failures.append(f"growth submissions did not advance: {submit_counts}")

    for row in completed:
        required = integer(row, "required_size")
        if integer(row, "buffer_size") != required:
            failures.append(f"staging capacity was not exact after {required}")
        if integer(row, "paired_size") != required:
            failures.append(f"paired capacity differed after {required}")
        if integer(row, "buffer_mapped") != 1:
            failures.append(f"staging mapping missing after {required}")
        if integer(row, "paired_mapped") != 0:
            failures.append(f"device buffer mapped after {required}")
        if integer(row, "buffer_present") != 1 or integer(
            row, "paired_present"
        ) != 1:
            failures.append(f"buffer pair incomplete after {required}")
        if integer(row, "in_command_buffer") or integer(
            row, "in_aux_command_buffer"
        ):
            failures.append(f"resize completed inside a command buffer at {required}")

    resize_after = [row for row in rows if row["event"] == "resize-after"]
    if len(resize_after) != len(EXPECTED_GROWTHS) * 2:
        failures.append(
            f"expected {len(EXPECTED_GROWTHS) * 2} completed buffer resizes, "
            f"observed {len(resize_after)}"
        )

    return {
        "schema_version": 1,
        "status": "PASS" if not failures else "FAIL",
        "event_count": len(rows),
        "initial_capacity": INITIAL_CAPACITY,
        "just_below_capacity_observed": INITIAL_CAPACITY - 512
        in required_requests,
        "exact_capacity_observed": INITIAL_CAPACITY in required_requests,
        "growth_capacities": observed_growths,
        "growth_submit_counts": submit_counts,
        "resize_count": len(resize_after),
        "mapping_restored_after_every_growth": not any(
            integer(row, "buffer_mapped") != 1 for row in completed
        ),
        "paired_capacity_consistent_after_every_growth": not any(
            integer(row, "buffer_size") != integer(row, "paired_size")
            for row in completed
        ),
        "failures": failures,
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Validate production-path transient-buffer growth events."
    )
    parser.add_argument("events", type=Path, help="Probe CSV emitted by xemu")
    parser.add_argument("--json-out", type=Path, help="Write the JSON verdict")
    args = parser.parse_args()

    result = verify(args.events)
    rendered = json.dumps(result, indent=2, sort_keys=True) + "\n"
    print(rendered, end="")
    if args.json_out:
        args.json_out.write_text(rendered, encoding="utf-8")
    return 0 if result["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())

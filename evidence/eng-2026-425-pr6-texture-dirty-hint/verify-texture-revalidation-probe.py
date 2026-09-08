#!/usr/bin/env python3
"""Validate test-only events emitted by Vulkan texture revalidation."""

import argparse
import csv
import json
from collections import Counter
from pathlib import Path


FIELDS = (
    "sequence",
    "event",
    "texture_index",
    "binding",
    "texture_offset",
    "texture_length",
    "palette_offset",
    "palette_length",
    "content_hash",
    "stored_hash",
    "content_changed",
    "upload_succeeded",
    "dirty_hint",
)


def summarize(path: Path) -> dict[str, object]:
    counts: Counter[str] = Counter()
    completed_successfully = 0
    changed_successfully = 0
    unchanged_successfully = 0
    retained_dirty_hint = 0
    failed_completions = 0
    failed_retaining_dirty_hint = 0
    successful_retries = 0
    palette_successfully = 0
    pending_failures: set[tuple[str, str]] = set()
    page_bindings: dict[int, set[str]] = {}

    with path.open(newline="", encoding="utf-8") as handle:
        for event in csv.DictReader(handle, fieldnames=FIELDS):
            counts[event["event"]] += 1
            if event["event"] != "complete":
                continue
            failure_key = (event["binding"], event["content_hash"])
            if event["upload_succeeded"] == "0":
                failed_completions += 1
                failed_retaining_dirty_hint += event["dirty_hint"] == "1"
                pending_failures.add(failure_key)
                continue
            if event["upload_succeeded"] != "1":
                continue
            completed_successfully += 1
            changed_successfully += event["content_changed"] == "1"
            unchanged_successfully += event["content_changed"] == "0"
            retained_dirty_hint += event["dirty_hint"] == "1"
            palette_successfully += int(event["palette_length"]) > 0
            if failure_key in pending_failures:
                successful_retries += 1
                pending_failures.remove(failure_key)
            page = int(event["texture_offset"]) // 4096
            page_bindings.setdefault(page, set()).add(event["binding"])

    return {
        "event_counts": dict(counts),
        "completed_successfully": completed_successfully,
        "changed_successfully": changed_successfully,
        "unchanged_successfully": unchanged_successfully,
        "successful_completions_retaining_dirty_hint": retained_dirty_hint,
        "failed_completions": failed_completions,
        "failed_completions_retaining_dirty_hint": failed_retaining_dirty_hint,
        "successful_retries": successful_retries,
        "successful_palette_revalidations": palette_successfully,
        "texture_start_pages_with_multiple_bindings": sum(
            len(bindings) > 1 for bindings in page_bindings.values()
        ),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("probe_csv", type=Path)
    parser.add_argument("--expect", choices=("broken", "fixed"), required=True)
    parser.add_argument("--require-retry", action="store_true")
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()

    summary = summarize(args.probe_csv)
    counts = summary["event_counts"]
    failures: list[str] = []

    if summary["changed_successfully"] == 0:
        failures.append("no changed-payload production revalidation was observed")
    if summary["unchanged_successfully"] == 0:
        failures.append("no unchanged-payload production revalidation was observed")

    retained = summary["successful_completions_retaining_dirty_hint"]
    if args.expect == "fixed":
        if retained:
            failures.append(
                f"{retained} successful production revalidations retained the dirty hint"
            )
        if counts.get("fast-path", 0) < 100:
            failures.append("fewer than 100 subsequent fast-path binds were observed")
    elif retained == 0:
        failures.append("negative control did not reproduce retained dirty hints")

    if args.require_retry:
        if summary["failed_completions"] == 0:
            failures.append("no injected production upload failure was observed")
        if summary["failed_completions"] != summary[
            "failed_completions_retaining_dirty_hint"
        ]:
            failures.append("an upload failure did not retain its dirty hint")
        if summary["successful_retries"] == 0:
            failures.append("no successful retry followed an injected failure")

    result = {
        "expectation": args.expect,
        "status": "PASS" if not failures else "FAIL",
        **summary,
        "failures": failures,
    }
    rendered = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.json_out:
        args.json_out.write_text(rendered, encoding="utf-8")
    print(rendered, end="")
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())

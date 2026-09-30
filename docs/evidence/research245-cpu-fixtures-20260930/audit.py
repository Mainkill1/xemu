#!/usr/bin/env python3
"""Verify retained source identities and receipts without recomputing timings."""
import hashlib
import json
import copy
from pathlib import Path

ROOT = Path(__file__).resolve().parent


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    manifest = json.loads((ROOT / "artifact-hashes.json").read_text())
    for relative, expected in manifest.items():
        assert sha(ROOT / relative) == expected, relative
    summary = json.loads((ROOT / "summary.json").read_text())
    provenance = json.loads((ROOT / "oracle-provenance-final.json").read_text())
    for filename, key in (
        ("cpu-code-rewrite-reference.json", "defaultOracleSha256"),
        ("cpu-code-rewrite-suite-reference.json", "suiteOracleSha256"),
        ("historical-reference-results.txt", "historicalReferenceSha256"),
    ):
        assert sha(ROOT / filename) == provenance[key], filename
    default = json.loads((ROOT / "cpu-code-rewrite-reference.json").read_text())
    historical = json.loads((ROOT / "historical-reference-results.txt").read_text())
    combined = json.loads((ROOT / "cpu-code-rewrite-suite-reference.json").read_text())
    assert len(historical) == provenance["historicalRecords"] == 141
    assert len(default) == provenance["newKnownAnswerRecords"] == 2
    authored = copy.deepcopy(default)
    for record in authored:
        record.update(iterations=40, warmup_iterations=3, measurement_iterations_multiplier=4)
    assert combined == historical + authored
    definition = json.loads((ROOT / "deck-cpu-suite-definition-v2.json").read_text())
    assert definition["workload"]["guestHddResults"]["expectedResultsSha256"] == provenance["suiteOracleSha256"]
    oracle = {record["id"]: record for record in default}
    reference = {record["id"]: record for record in combined}
    status = json.loads((ROOT / "research245-cpu-main-full-v1-status.json").read_text())
    attempts = json.loads((ROOT / "research245-cpu-main-full-v1-attempts.json").read_text())["items"]
    reviews = json.loads((ROOT / "full-correctness-review.json").read_text())
    assert status == summary["fullSuiteStatus"]
    assert status["terminal"] and status["finished"] == len(attempts) == len(reviews) == 17
    assert status["passed"] == 2 and status["failed"] == 15 and status["incomplete"] == 0
    all_records = []
    for attempt, row, review in zip(attempts, summary["fullSuiteOutcomes"], reviews):
        assert attempt["terminal"] and attempt["runId"] == row["runId"] == review["runId"]
        source = ROOT / row["source"]
        assert sha(source) == row["sourceSha256"]
        result = json.loads(source.read_text())
        assert result["assessment"] == row["assessment"] == review["assessment"]
        assert result["exitCode"] == row["exitCode"] == 0
        assert row["operatorActivity"] == result["operatorActivity"]
        assert not row["operatorActivity"]["Intervened"]
        guest = source.parent / "guest/results.txt"
        normalized = json.loads((source.parent / "guest/normalized-results.json").read_text())
        assert sha(guest) == row["guestSourceSha256"] == normalized["sourceSha256"]
        records = json.loads(guest.read_text())
        all_records.extend(records)
        assert len(records) == review["guestCount"]
        assert review["guestFailed"] == [r["id"] for r in records if r["outcome"] != "PASS"] == []
        config = json.loads((source.parent / "guest/xiso-guest-config.json").read_text())
        receipt = json.loads((source.parent / "guest/resolved-plan-result.json").read_text())
        assert receipt["completion"] == "COMPLETE" and receipt["plan_id"] == config["resolved_plan"]["plan_id"]
        selected = {r["id"] for r in config["resolved_plan"]["tests"]}
        assert selected == {r["id"] for r in records if r["kind"] == "leaf"}
        assert receipt["selected_leaf_count"] == receipt["emitted_leaf_count"] == len(selected)
        assert review["missingPinnedIds"] == sorted({r["id"] for r in records} - set(reference))
        failed = [c for c in result["assessment"]["Checks"] if c["Category"] == "correctness" and not c["Passed"]]
        assert review["failedChecks"] == failed
        assert {d["id"] for d in review["mismatchDetails"]} == {c["Name"][6:] for c in failed if c["Name"].startswith("guest:")}
        by_id = {r["id"]: r for r in records}
        reported_ids = {d["id"] for d in review["mismatchDetails"]}
        extra_differences = {
            r["id"] for r in records if r["id"] in reference
            and "framebuffer_fnv1a64" in reference[r["id"]]
            and reference[r["id"]]["framebuffer_fnv1a64"] != r.get("framebuffer_fnv1a64")
            and r["id"] not in reported_ids}
        assert {d["id"] for d in review["unreportedFramebufferDifferences"]} == extra_differences
        for difference in review["unreportedFramebufferDifferences"]:
            assert review["missingPinnedIds"]
            assert difference["expected"] == reference[difference["id"]]["framebuffer_fnv1a64"]
            assert difference["actual"] == by_id[difference["id"]]["framebuffer_fnv1a64"]
        for mismatch in review["mismatchDetails"]:
            actual, expected = by_id[mismatch["id"]], reference[mismatch["id"]]
            assert set(mismatch["differentFields"]) == {"framebuffer_fnv1a64"}
            assert mismatch["differentFields"]["framebuffer_fnv1a64"] == {
                "expected": expected["framebuffer_fnv1a64"], "actual": actual["framebuffer_fnv1a64"]}
            for field in ("schema_version", "revision", "kind", "iterations", "sample_count", "measurement_iterations_multiplier", "warmup_iterations", "gpu_completion_mode"):
                assert actual[field] == expected[field]
        for record in records:
            if record["id"] in oracle:
                for key in ("oracle_status", "work_checksum", "result_checksum"):
                    assert record["metadata"][key] == oracle[record["id"]]["metadata"][key]
    assert len(all_records) == len({r["id"] for r in all_records}) == 161
    assert sum(r["kind"] == "leaf" for r in all_records) == 156
    assert sum(len(r["missingPinnedIds"]) for r in reviews) == 18
    assert sum(len(r["mismatchDetails"]) for r in reviews) == 68
    assert sum(len(r["unreportedFramebufferDifferences"]) for r in reviews) == 2
    for row in summary["outcomes"]:
        source = ROOT / row["source"]
        assert sha(source) == row["sourceSha256"], row["runId"]
        result = json.loads(source.read_text())
        assert result["runId"] == row["runId"]
        for key in ("execution", "correctness", "evidence", "comparison"):
            assert row[key] == result["assessment"][key.capitalize()]
        if "measurements" in row:
            assert row["measurements"] == result["workload"]["Measurements"]
        if "guestSourceSha256" not in row:
            continue
        guest = source.parent / "guest/results.txt"
        normalized = json.loads((ROOT / row["normalizationSource"]).read_text())
        assert sha(guest) == row["guestSourceSha256"] == normalized["sourceSha256"]
        records = json.loads(guest.read_text())
        if row["correctness"] != "passed":
            continue
        config = json.loads((source.parent / "guest/xiso-guest-config.json").read_text())
        receipt = json.loads((source.parent / "guest/resolved-plan-result.json").read_text())
        selected = {item["id"] for item in config["resolved_plan"]["tests"]}
        assert selected == {record["id"] for record in records}
        assert receipt["completion"] == "COMPLETE"
        assert receipt["plan_id"] == config["resolved_plan"]["plan_id"]
        assert receipt["selected_leaf_count"] == receipt["emitted_leaf_count"] == len(selected)
        for record in records:
            assert record["outcome"] == "PASS"
            if record["id"] not in oracle:
                continue
            known = oracle[record["id"]]
            assert record["revision"] == known["revision"]
            for key in ("oracle_status", "work_checksum", "result_checksum"):
                assert record["metadata"][key] == known["metadata"][key]
            assert record["metadata"]["expected_checksum"] == known["metadata"]["result_checksum"]
    print(f"Verified {len(manifest)} artifact hashes, {len(summary['outcomes'])} retained outcomes, and 17 full-suite attempts; no timing calculations.")


if __name__ == "__main__":
    main()

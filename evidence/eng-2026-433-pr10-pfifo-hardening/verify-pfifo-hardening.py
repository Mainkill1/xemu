#!/usr/bin/env python3
import csv
import sys
from pathlib import Path

CAPACITY = 524287
EXPECTED_HASHES = {
    "PFIFOPacketBoundary::pfifo.boundary-array-element16": "4d1756526f052325",
    "PFIFOPacketBoundary::pfifo.boundary-array-element32": "6eb7071be692a325",
    "PFIFOPacketBoundary::pfifo.boundary-inline-array": "847da1930526a325",
    "PFIFOPacketBoundary::pfifo.incrementing-inline-fallback": "640439ee8ecd2325",
}

def fail(message):
    raise SystemExit(f"FAIL: {message}")

def read_csv(path):
    with Path(path).open(newline="", encoding="utf-8-sig") as f:
        return list(csv.DictReader(f))

def verify_results(path):
    rows = read_csv(path)
    if len(rows) != 18:
        fail(f"expected 18 result rows, got {len(rows)}")
    old = [r for r in rows if r["role"] == "old-negative"]
    if len(old) != 1 or old[0]["outcome"] != "EXPECTED_FAIL":
        fail("old-head assertion control is missing")
    repaired = [r for r in rows if r["role"].startswith("repaired")]
    if len(repaired) != 14 or any(r["outcome"] != "PASS" for r in repaired):
        fail("repaired production cells are incomplete or failed")
    for r in repaired:
        if r["test_id"] in EXPECTED_HASHES:
            expected = EXPECTED_HASHES[r["test_id"]]
            if r["framebuffer_fnv1a64"] != expected:
                fail(f"wrong framebuffer hash for {r['test_id']} in {r['mode']}")
            if r["expected_framebuffer_fnv1a64"] != expected:
                fail(f"guest KAT mismatch for {r['test_id']}")
        if r["renderer"] == "vulkan" and r["vuid_count"] != "0":
            fail(f"Vulkan validation errors in {r['test_id']}")
    pgr2 = [r for r in repaired if r["test_id"] == "PFIFOArrayElements::pfifo.array-element-pgr2"]
    if len(pgr2) != 2 or len({r["framebuffer_fnv1a64"] for r in pgr2}) != 1:
        fail("PGR2 representative scalar/bulk framebuffer mismatch")
    trace = [r for r in repaired if r["mode"] == "scalar_trace"]
    if len(trace) != 5 or any(r["trace_argument"] != "true" for r in trace):
        fail("scalar trace cells are incomplete")

def verify_probe(path, expected_mode, incrementing=False):
    rows = read_csv(path)
    if not rows:
        fail(f"empty probe: {path}")
    if incrementing:
        matches = [r for r in rows if r["mode"] == "scalar_incrementing" and r["method"] == "0x1818"]
        if len(matches) != 1:
            fail("incrementing fallback event missing")
        r = matches[0]
        if (r["available_words"], r["consumed_words"], r["length_before"], r["length_after"]) != ("2", "1", "0", "1"):
            fail("incrementing fallback consumed or stored the wrong count")
        return
    relevant = [r for r in rows if r["method"] in ("0x1800", "0x1808") and int(r["length_before"]) >= CAPACITY - 1]
    sequence = [(r["method"],r["mode"],r["action"],int(r["consumed_words"]),int(r["length_before"]),int(r["length_after"])) for r in relevant]
    expected = [
        ("0x1800",expected_mode,"rejected",1,CAPACITY-1,CAPACITY-1),
        ("0x1808",expected_mode,"accepted",1,CAPACITY-1,CAPACITY),
        ("0x1808",expected_mode,"rejected",1,CAPACITY,CAPACITY),
    ]
    if sequence != expected:
        fail(f"unexpected {expected_mode} boundary transitions: {sequence}")

def main(argv):
    root = Path(argv[1]) if len(argv) > 1 else Path(__file__).resolve().parent
    verify_results(root / "focused-results.csv")
    verify_probe(root / "probe-bulk.csv", "bulk")
    verify_probe(root / "probe-trace.csv", "scalar_trace")
    verify_probe(root / "probe-increment.csv", "scalar_incrementing", incrementing=True)
    print("PASS: old assertion reproduced; repaired bulk, trace, incrementing, hashes, and validation verified")

if __name__ == "__main__":
    main(sys.argv)

#!/usr/bin/env python3
"""Check retained diagnostic contracts; summarize counters, not benchmark CSV."""
import gzip
import hashlib
import json
import re
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parent


def counters(path):
    text = path.read_text()
    result = {}
    for name in ("TB count", "TB flush count", "TB invalidate count"):
        match = re.search(r"^" + re.escape(name) + r"\s+(\d+)\s*$", text, re.M)
        assert match, (path, name)
        result[name] = int(match.group(1))
    return result


rows = []
build = json.loads((ROOT / "build-identity.json").read_text())
for label in ("windows-w1", "deck-native-v2", "deck-perf-v1"):
    paths = list((ROOT / "runs" / label).glob("*/result.json"))
    assert len(paths) == 1, label
    path = paths[0]
    result = json.loads(path.read_text())
    expected_hash = build["windowsExecutableSha256" if label == "windows-w1"
                          else "innerExecutableSha256"]
    assert result["executableSha256"] == expected_hash
    assert result["status"] == "completed" and result["exitCode"] == 0
    assert result["correctnessStatus"] == "passed"
    assert result["evidenceStatus"] == "complete"
    assert result["comparisonStatus"] == "ineligible"
    diagnostics = {d["Id"]: d for d in result["diagnostics"]}
    assert all(d["Status"] == "completed" for d in diagnostics.values())
    before_file = next(path.parent.glob("diagnostics/*-jit-before/monitor.txt"))
    after_file = next(path.parent.glob("diagnostics/*-jit-after/monitor.txt"))
    before, after = counters(before_file), counters(after_file)
    assert before["TB flush count"] == after["TB flush count"] == 0
    delta = after["TB invalidate count"] - before["TB invalidate count"]
    assert delta > 0
    start = datetime.fromisoformat(diagnostics["jit-before"]["StartedUtc"])
    end = datetime.fromisoformat(diagnostics["jit-after"]["StartedUtc"])
    seconds = (end - start).total_seconds()
    assert 30 < seconds < 35
    performance = json.loads((path.parent / "performance.json").read_text())
    assert performance["Complete"] and not performance["Errors"]
    rows.append({
        "label": label, "runId": result["runId"],
        "executableSha256": result["executableSha256"],
        "before": before, "after": after, "invalidations": delta,
        "diagnosticStartIntervalSeconds": seconds,
        "approximateInvalidationsPerSecond": delta / seconds,
        "runnerAnalysis": performance,
        "comparisonStatus": result["comparisonStatus"],
        "productionAcceptance": False,
    })

identity = json.loads((ROOT / "perf-identity.json").read_text())
compressed = next((ROOT / "runs" / "deck-perf-v1").glob("*/research245-perf.data.gz"))
raw = gzip.decompress(compressed.read_bytes())
assert len(raw) == identity["rawBytes"]
assert hashlib.sha256(raw).hexdigest() == identity["rawSha256"]
assert identity["samples"] == 8714 and identity["lostSamples"] == 0
assert "Total Lost Samples: 0" in (ROOT / "perf-symbolized-correct.txt").read_text()
assert identity["buildId"] in (ROOT / "perf-buildids.txt").read_text()
assert "cycles:u" in (ROOT / "perf-header.txt").read_text()

summary = {"sourceCommit": "2d289cb349bca95eae81b6a54b0f8d965365ff82",
           "productionAcceptance": False, "runs": rows, "perf": identity}
(ROOT / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
for row in rows:
    print(f"{row['label']}: {row['invalidations']} invalidations / "
          f"{row['diagnosticStartIntervalSeconds']:.6f}s; diagnostic only")
print("Perf payload hash, build ID and completed diagnostic contracts verified")

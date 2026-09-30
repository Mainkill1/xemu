#!/usr/bin/env python3
"""Validate retained voice-write completion and count invariants; preserve failures."""
import csv
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parent
original_aliases = {
    "REPORT.md": "ORIGINAL-REPORT.md",
    "audit.py": "ORIGINAL-audit.py",
    "summary.json": "ORIGINAL-summary.json",
}
for line in (root / "SOURCE-SHA256SUMS").read_text().splitlines():
    expected, relative = line.split("  ", 1)
    retained = root / original_aliases.get(relative, relative)
    assert hashlib.sha256(retained.read_bytes()).hexdigest() == expected, relative
labels = json.loads((root / "run-labels.json").read_text())
comparison = json.loads((root / "runner-parent-versus-probe-comparison.json").read_text())
assert comparison["status"] == "incomparable"
assert all(row["improvementPercent"] is None for row in comparison["rows"])
summary = []
for folder in sorted((root / "runs").iterdir()):
    run = next(folder.iterdir())
    result = json.loads((run / "result.json").read_text())
    item = {
        "label": folder.name, "runId": run.name,
        "runName": labels[folder.name]["name"],
        "host": labels[folder.name]["host"],
        "role": labels[folder.name]["role"],
        "executableSha256": result["executableSha256"],
        "execution": result["status"], "exitCode": result.get("exitCode"),
        "correctness": result.get("correctnessStatus"),
        "evidence": result.get("evidenceStatus"),
        "runnerComparison": result.get("comparisonStatus"),
        "productionAcceptance": False,
        "analysis": json.loads((run / "performance.json").read_text()),
    }
    assert labels[folder.name]["runId"] == run.name
    for source in item["analysis"]["Sources"]:
        data = (run / source["Path"]).read_bytes()
        assert len(data) == source["Bytes"]
        assert hashlib.sha256(data).hexdigest() == source["Sha256"]
    path = run / "voice-writes.csv"
    if path.exists():
        rows = list(csv.reader(path.open()))
        complete = bool(rows and len(rows[-1]) == 5 and rows[-1][0] == "summary"
                        and all(x.isdecimal() for x in rows[-1][1:]))
        item["attributionComplete"] = complete
        if result["status"] == "completed":
            assert complete, f"{run.name}: completed run lacks numeric summary"
        if complete:
            fields = [x for x in rows if x[0] == "field" and x[1] != "offset"]
            calls, changed, equal = (sum(int(x[i]) for x in fields) for i in (3, 4, 5))
            assert calls == changed + equal
            assert int(rows[-1][4]) == calls * 4
            for x in fields:
                assert int(x[3]) == int(x[4]) + int(x[5])
                assert int(x[6]) <= int(x[3])
                assert int(x[7]) <= int(x[4]) and int(x[9]) <= int(x[5])
            item["attribution"] = dict(
                calls=calls, changed=changed, unchanged=equal,
                unchangedPercent=equal * 100 / calls,
                frames=int(rows[-1][1]), debugActiveVoiceSum=int(rows[-1][2]),
                debugActiveVoiceMax=int(rows[-1][3]), storeBytes=int(rows[-1][4]),
                changedSamples=sum(int(x[7]) for x in fields),
                changedNs=sum(int(x[8]) for x in fields),
                unchangedSamples=sum(int(x[9]) for x in fields),
                unchangedNs=sum(int(x[10]) for x in fields),
                scope="entire process, not measurement segment",
            )
    summary.append(item)
(root / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
files = sorted(x for x in root.rglob("*") if x.is_file() and x.name != "SHA256SUMS")
(root / "SHA256SUMS").write_text("".join(
    hashlib.sha256(x.read_bytes()).hexdigest() + "  " + x.relative_to(root).as_posix() + "\n"
    for x in files))
print(f"Audited {len(summary)} runs; hashed {len(files)} retained files.")

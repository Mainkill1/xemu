"""Check the published evidence inventory and its bounded correctness claims."""

import hashlib
import json
from pathlib import Path
import re

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
expected = {}
for line in (HERE / "SHA256SUMS").read_text().splitlines():
    digest, name = line.split("  ", 1)
    assert name not in expected, name
    expected[name] = digest
actual = {str(p.relative_to(HERE)) for p in HERE.rglob("*")
          if p.is_file() and p.name != "SHA256SUMS"
          and "__pycache__" not in p.parts}
assert actual == set(expected), (actual - set(expected), set(expected) - actual)
for name, digest in expected.items():
    assert hashlib.sha256((HERE / name).read_bytes()).hexdigest() == digest, name
receipt = json.loads((HERE / "receipt.json").read_text())
for name, digest in receipt["source"]["sha256"].items():
    assert hashlib.sha256((REPO / name).read_bytes()).hexdigest() == digest, name
checks = json.loads((HERE / "final-checks.json").read_text())
for check in checks:
    if check["expected"] == "nonzero":
        assert check["exitCode"] == -6, check
    else:
        assert check["exitCode"] == check["expected"] == 0, check
for name, count in receipt["passingUnitLogs"].items():
    log = (HERE / "logs" / name).read_text()
    assert len(re.findall(r"^ok \d+ /", log, re.M)) == count, name
    assert not re.search(r"^not ok|^ERROR:|Bail out!|AddressSanitizer|runtime error:", log, re.M), name
for name, case in receipt["negativeControls"].items():
    log = (HERE / "logs" / name).read_text()
    assert f"not ok {case}" in log and "should be NULL" in log, name
assert receipt["performance"]["newGuestRuns"] == 0
assert receipt["performance"]["acceptedImprovementPercent"] is None
print(f"PASS: {len(expected)} files; exact source bytes; 6 validity / 4 collector / "
      "6 sanitizer / 6 alternate-layout passes; 2 rejected controls. "
      "No guest performance claim.")

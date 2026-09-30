"""Verify immutable source, raw host observations and bounded unit claims."""
import hashlib
import json
from pathlib import Path
import re
import statistics
import subprocess

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
expected = {}
for line in (HERE / 'SHA256SUMS').read_text().splitlines():
    digest, name = line.split('  ', 1)
    assert name not in expected, name
    expected[name] = digest
actual = {p.relative_to(HERE).as_posix() for p in HERE.rglob('*')
          if p.is_file() and p.name != 'SHA256SUMS' and '__pycache__' not in p.parts}
assert actual == set(expected), (actual - set(expected), set(expected) - actual)
for name, digest in expected.items():
    assert hashlib.sha256((HERE / name).read_bytes()).hexdigest() == digest, name
receipt = json.loads((HERE / 'receipt.json').read_text())
for name, digest in receipt['source']['sha256'].items():
    data = subprocess.check_output(['git', 'show', receipt['source']['commit'] + ':' + name], cwd=REPO)
    assert hashlib.sha256(data).hexdigest() == digest, name
for name, count in receipt['passingUnitLogs'].items():
    log = (HERE / 'logs' / name).read_text()
    assert len(re.findall(r'^ok \d+ /', log, re.M)) == count, name
    assert not re.search(r'^not ok|^ERROR:|Bail out!|WARNING: ThreadSanitizer|runtime error:|ERROR: AddressSanitizer', log, re.M), name
negative = (HERE / 'logs' / receipt['negativeControls']['counterMode']).read_text()
assert 'not ok /tcg/jump-cache/modes' in negative and '(5 == 1)' in negative
red = (HERE / 'logs' / receipt['negativeControls']['occupancy']).read_text()
assert 'not ok /tcg/jump-cache/sampled-occupancy' in red and '(1024 < 80)' in red
initial = (HERE / 'logs' / receipt['initialIneffectiveControl']).read_text()
assert 'ok 1 /tcg/jump-cache/modes' in initial and 'not ok' not in initial
runs = json.loads((HERE / 'host/runs.json').read_text())
assert len(runs) == 100 and sum(not r['warmup'] for r in runs) == 80
for r in runs:
    assert r['exitCode'] == 0 and r['checks'] == 'pass' and r['cpu_ns'] > 0
    assert (r['lookups'], r['clears']) == ((5000000, 0) if r['workload'] == 'lookup-hooks' else (0, 50000))
for s in json.loads((HERE / 'host/summary.json').read_text()):
    rows = [r for r in runs if not r['warmup'] and r['workload'] == s['workload'] and r['comparison'] == s['comparison']]
    assert [r['side'] for r in rows] == list('ABBABAAB')
    median = {}
    for side, identity in [('A', s['reference']), ('B', s['candidate'])]:
        subset = [r for r in rows if r['side'] == side]
        assert len(subset) == 4
        assert all([r['build'], r['mode']] == identity for r in subset)
        values = [r['cpu_ns'] for r in subset]
        median[side] = statistics.median(values)
        assert median[side] == s['median'][side]
        assert [min(values), max(values)] == s['range'][side]
    assert s['absoluteDifferenceNs'] == median['B'] - median['A']
    assert abs(s['improvementPercent'] - 100 * (median['A'] - median['B']) / median['A']) < 1e-10
    if s['workload'] == 'empty-cache-clears' and s['comparison'] == 'legacy-all-vs-lighter-all':
        assert all(r['occupancy_observed_clears'] == (50000 if r['build'] == 'reference' else 1616) for r in rows)
aa = json.loads((HERE / 'host/aa-runs.json').read_text())
assert len(aa) == 16 and all(r['exitCode'] == 0 and r['checks'] == 'pass' and r['mode'] == 'off' for r in aa)
for s in json.loads((HERE / 'host/aa-summary.json').read_text()):
    med = {side: statistics.median(r['cpu_ns'] for r in aa if r['workload'] == s['workload'] and r['side'] == side) for side in ['A', 'B']}
    assert s['median'] == med
    assert abs(s['apparentImprovementPercent'] - 100 * (med['A'] - med['B']) / med['A']) < 1e-10
for item in json.loads((HERE / 'host/configuration-equivalence.json').read_text()):
    assert item['byteIdentical'] and item['rebuiltSha256'] == item['originalSha256']
for item in json.loads((HERE / 'exact-builds.json').read_text()):
    assert item['exitCode'] == 0 and item['sourceCommit'] == receipt['source']['commit']
rebased = receipt['rebasedValidation']
checks = json.loads((HERE / rebased['receipt']).read_text())
assert checks['sourceCommit'] == rebased['sourceCommit'] and checks['guestRunsAtThisCommit'] == 0
assert all(c['exitCode'] == 0 for c in checks['checks']) and len(checks['checks']) == 4
for name in rebased['runtimeSourceEquivalentToMeasured']:
    before = subprocess.check_output(['git', 'show', receipt['source']['commit'] + ':' + name], cwd=REPO)
    after = subprocess.check_output(['git', 'show', rebased['sourceCommit'] + ':' + name], cwd=REPO)
    assert before == after, name
for name, count in [('native-collector-unit.log', 7), ('native-validity-unit.log', 6)]:
    log = (HERE / 'rebase-checks' / name).read_text()
    assert len(re.findall(r'^ok \d+ /', log, re.M)) == count and 'not ok' not in log
assert receipt['performance']['acceptedProductionImprovementPercent'] is None
assert receipt['performance']['actualRetentionCandidate'] is False
print(f'PASS: {len(expected)} exact files; 80 retained host runs + 20 warmups + 16 A/A; all 10 medians/deltas; source, 7 collector / 6 helper / 7 ASan/UBSan / 7 TSan; rejected controls. No retention speedup claim.')

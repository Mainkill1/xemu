"""Check archived bytes, production equivalence and all reported arithmetic."""
import hashlib
import json
import math
from pathlib import Path
import re
import statistics
import subprocess

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]


def read(name):
    return json.loads((HERE / name).read_text())


expected = {}
for line in (HERE / 'SHA256SUMS').read_text().splitlines():
    digest, name = line.split('  ', 1)
    assert name not in expected
    expected[name] = digest
actual = {p.relative_to(HERE).as_posix() for p in HERE.rglob('*')
          if p.is_file() and p.name != 'SHA256SUMS' and '__pycache__' not in p.parts}
assert actual == set(expected)
for name, digest in expected.items():
    assert hashlib.sha256((HERE / name).read_bytes()).hexdigest() == digest, name
manifest = read('manifest.json')
assert not manifest['runtimeCandidate'] and not manifest['runtimeChanges']
assert manifest['acceptedPerformanceImprovementPercent'] is None
for row in manifest['sourceFiles']:
    a = subprocess.check_output(['git', 'show', manifest['productionBaseline'] + ':' + row['path']], cwd=REPO)
    assert a == (REPO / row['path']).read_bytes()
    assert hashlib.sha256(a).hexdigest() == row['sha256']
replay = read('final-controls/receipt.json')
assert replay['candidateCommit'] == manifest['testedFixtureCommit']
assert replay['referenceCommit'] == manifest['historicalReference']
for row in replay['inputs']:
    if row['tree'] in ('candidate', 'reference'):
        ref = manifest['productionBaseline'] if row['tree'] == 'candidate' else manifest['historicalReference']
        data = subprocess.check_output(['git', 'show', ref + ':' + row['path']], cwd=REPO)
    else:
        data = subprocess.check_output(['git', 'show', manifest['verificationFixtureCommit'] + ':tests/xbox/ptimer/' + row['path']], cwd=REPO)
    assert hashlib.sha256(data).hexdigest() == row['sha256']
    if row['tree'] == 'fixture' and row['path'].endswith(('.c', '.h')):
        assert data == (REPO / 'tests/xbox/ptimer' / row['path']).read_bytes()
changed = subprocess.check_output(['git', 'diff', '--name-only', manifest['publicationBase'], 'HEAD'], cwd=REPO).decode().splitlines()
assert all(path.startswith(('tests/', 'docs/')) for path in changed), changed
assert [r['exitCode'] for r in replay['runs']][:2] == [0, 0]
assert replay['runs'][2]['exitCode'] != 0 and not any(r['timedOut'] for r in replay['runs'])
def metrics(name, phase):
    line = next(l for l in (HERE / name).read_text().splitlines() if l.startswith('# ' + phase + ' '))
    return {key: int(value) for key, value in re.findall(r'(\w+)=(-?\d+)', line)}
current = metrics('final-controls/current.log', 'recurring')
old = metrics('final-controls/reference-negative.log', 'recurring')
assert current['callback_count'] == 10000 and current['alarm_write_count'] == 10001
assert current['past_target_rejections'] == current['forbidden_renderer_waits'] == 0
assert current['PGRAPH_control_accesses'] == 40000
assert old['callback_count'] == old['alarm_write_count'] == old['past_target_rejections'] == old['forbidden_renderer_waits'] == 1
assert old['maximum_callback_lateness_ns'] == old['maximum_BQL_wait_virtual_ns'] == 25666666
assert metrics('final-controls/current.log', 'CAS-matrix')['CAS_retries'] == 10000
assert metrics('final-controls/current.log', 'PFIFO-matrix')['flip_stall_transitions'] == 1792
for folder in ('control-cost', 'control-cost-final-v2'):
    c = read(folder + '/receipt.json')
    assert c['exitCode'] == 0 and c['clock'] == 'CLOCK_PROCESS_CPUTIME_ID'
    assert not c['runtimeCandidate'] and c['acceptedPerformanceImprovementPercent'] is None
    found = re.findall(r'cost pair=(\d+) position=(\d+) mmio=(\d+) iterations=(\d+) cpu_ns=(\d+)', (HERE / folder / 'raw.log').read_text())
    rows = [dict(zip(('pair', 'position', 'mmio', 'iterations', 'cpuNs'), map(int, r))) for r in found]
    assert rows == c['samples'] and len(rows) == 60
    deltas = []
    for pair in range(30):
        selected = sorted((r for r in rows if r['pair'] == pair), key=lambda r: r['mmio'])
        assert len(selected) == 2 and [r['mmio'] for r in selected] == [0, 1]
        a, b = selected
        assert a['iterations'] == b['iterations'] == 100000
        deltas.append((b['cpuNs'] - a['cpuNs']) / 100000)
    for key, value in [('emptyLoopMedianNs', statistics.median(r['cpuNs'] for r in rows if not r['mmio'])),
                       ('fixedMmioMedianNs', statistics.median(r['cpuNs'] for r in rows if r['mmio'])),
                       ('pairedMedianAdditionalNsPerReadWrite', statistics.median(deltas)),
                       ('minimumAdditionalNs', min(deltas)), ('maximumAdditionalNs', max(deltas)),
                       ('hypotheticalCpuPercentAt60PairsPerSecond', statistics.median(deltas) * 60 / 1e7)]:
        assert math.isclose(c[key], value, rel_tol=1e-12, abs_tol=1e-10), key
excluded = read('initial-cost-summary-excluded.json')
assert not excluded['retained']
native = read('smoke-vulkan-operation-status.json')
assert native['state'] == 'failed' and not native['result']['Passed']
failed = [c for c in native['result']['Checks'] if not c['Passed']]
assert [c['Name'] for c in failed] == ['free_space']
assert '1001476096' in failed[0]['Detail'] and '1073741824' in failed[0]['Detail']
assert 'No space left on device' in (HERE / 'xbox-suite.log').read_text()
assert 'Fail:              0' in (HERE / 'xbox-suite-no-rebuild.log').read_text()
print(f'PASS: {len(expected)} archived files; production unchanged; historical failure and current 10k callbacks / 10k CAS retries / 1792 FIFO transitions; 120 raw cost observations; native storage blocker. No runtime candidate or speedup.')

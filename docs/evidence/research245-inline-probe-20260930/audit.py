"""Verify archived hashes and independently recompute observer comparisons."""
import hashlib
import json
import math
from pathlib import Path
import random
import statistics
import subprocess

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]


def read(path):
    return json.loads((HERE / path).read_text())


def quantile(values, p):
    v = sorted(values)
    x = (len(v) - 1) * p
    lo = int(x)
    return v[lo] + (v[min(lo + 1, len(v) - 1)] - v[lo]) * (x - lo)


def close(a, b):
    assert math.isclose(a, b, rel_tol=1e-12, abs_tol=1e-10), (a, b)


expected = {}
for line in (HERE / 'SHA256SUMS').read_text().splitlines():
    digest, name = line.split('  ', 1)
    assert name not in expected
    expected[name] = digest
actual = {p.relative_to(HERE).as_posix() for p in HERE.rglob('*')
          if p.is_file() and p.name != 'SHA256SUMS' and '__pycache__' not in p.parts}
assert actual == set(expected), (actual - set(expected), set(expected) - actual)
for name, digest in expected.items():
    assert hashlib.sha256((HERE / name).read_bytes()).hexdigest() == digest, name

receipt = read('receipt.json')
assert receipt['completedAttempts'] == 28 and receipt['acceptedProductionImprovementPercent'] is None
assert not receipt['actualRetentionCandidate'] and not receipt['driverQualification']
folder = HERE / 'deck'
observations = read('observations.json')
assert len(observations) == 28 and len({r['runId'] for r in observations}) == 28
schedule = read('deck/plans/schedule.json')
fixed_inputs = None
for obs in observations:
    assert obs['sourceCommit'] == (receipt['referenceCommit'] if obs['mode'] in ('disabled', 'prior-off') else receipt['sourceCommit'])
    assert obs['executableSha256'] == schedule['packages'][obs['mode']]['executableSha256']
    planned = next(p for p in schedule['attempts'] if p['campaign'] == obs['campaign'])
    assert all(obs[k] == v for k, v in planned.items())
    obs['retained'] = True
    run = folder / 'runs' / obs['runId']
    assessment = json.loads((run / 'assessment.json').read_text())
    assert assessment['Execution'] == 'completed' and assessment['Correctness'] == 'passed'
    assert assessment['Evidence'] == 'complete' and assessment['Comparison'] == 'ineligible'
    normalized = json.loads((run / 'guest/normalized-results.json').read_text())
    raw_bytes = (run / 'guest/results.txt').read_bytes()
    assert normalized['sourceSha256'] == hashlib.sha256(raw_bytes).hexdigest()
    raw = json.loads(raw_bytes)[0]
    assert raw['id'] == obs['leaf'] and raw['outcome'] == 'PASS'
    assert raw['metadata']['operations'] == (50000000 if obs['leaf'].endswith('code_stable') else 1000000)
    assert raw['metadata']['oracle_status'] == 'PASS'
    assert raw['metadata']['result_checksum'] == raw['metadata']['expected_checksum']
    assert len(raw['raw_results']) == raw['sample_count'] == obs['samples'] == 10
    assert statistics.median(raw['raw_results']) == obs['medianUs'] == normalized['records'][0]['MedianUs']
    assert normalized['records'][0]['Correct'] and all(c['Passed'] for c in normalized['checks'])
    launch = json.loads((run / 'launch.json').read_text())
    manifest = json.loads((run / 'input-manifest.json').read_text())
    assert launch['executableSha256'] == manifest['ExecutableSha256'] == obs['executableSha256']
    assert launch['experiment']['Variant'] == obs['mode']
    assert all(i['Verified'] and i['Sha256'] == i['ExpectedSha256'] for i in manifest['Inputs'])
    inputs = {i['Path']: i['Sha256'] for i in manifest['Inputs'] if i['Path'] not in ('xemu', 'xemu.exe')}
    if obs['retained']:
        if fixed_inputs is None:
            fixed_inputs = inputs
        assert inputs == fixed_inputs
    if obs['retained']:
        assert not (run / 'operator-events.jsonl').read_text().strip(), obs['runId']
        assert not launch['experiment']['AllowOperatorIntervention']
    job = json.loads((run / 'job.json').read_text())
    assert job['Environment']['XEMU_TCG_JUMP_CACHE_PROBE'] == ('off' if obs['mode'] in ('disabled', 'prior-off') else obs['mode'])
    assert job['Plan'] == [] and job['Diagnostics'] == []
    assert not job['RuntimeState']['Isolation']['AllowUncontrolledDriverCache']
    xiso = job['RuntimeState']['Xiso']
    assert xiso['IsoSha256'] == '74a10c400f4fb280dcb4037e38e7e4d3150e73e0061a04f65b3c68cc61a49e63'
    assert xiso['CatalogId'] == 'sha256:34b8ee7f91f3412b5e7758aab9f4ca08746ef2e97fee8b5c61be8947e145f7f4'
    assert xiso['Tests'] == [obs['leaf']]
    assert xiso['Settings'] == {'warmup_iterations': 0, 'measurement_iterations_multiplier': 1, 'gpu_completion_mode': 'per_iteration'}
    state = json.loads((run / 'diagnostics/run-state/report.json').read_text())
    assert not state['DriverNamespaceVerified'] and not state['AllowUncontrolledDriverCache'] and not state['ComparisonReady']
for cmp in read('comparisons.json'):
    selected = [o for o in observations if o['retained'] and o['leaf'] == cmp['leaf'] and o['phase'] == 'inline-sweep']
    values = {m: [o['medianUs'] for o in selected if o['mode'] == m] for m in (cmp['referenceMode'], cmp['candidateMode'])}
    assert all(len(v) == 2 for v in values.values())
    a, b = [statistics.median(values[m]) for m in (cmp['referenceMode'], cmp['candidateMode'])]
    assert a == cmp['referenceMedianUs'] and b == cmp['candidateMedianUs']
    close(b-a, cmp['absoluteDeltaUs'])
    close(100*(a-b)/a, cmp['improvementPercent'])
for control in read('aa-controls.json'):
    rows = [o for o in observations if o['retained'] and o['leaf'] == control['leaf'] and o['phase'] == 'A/A']
    assert len(rows) == 2 and all(o['mode'] == 'off' for o in rows)
    a, b = [o['medianUs'] for o in rows]
    assert a == control['firstOffUs'] and b == control['secondOffUs']
    close(100*(a-b)/a, control['apparentImprovementPercent'])

assert len(read('comparisons.json')) == 10 and len(read('aa-controls.json')) == 2
for row in read('selected-artifact-inventory.json'):
    f = folder / 'runs' / row['runId'] / row['path']
    assert f.stat().st_size == row['bytes']
    assert hashlib.sha256(f.read_bytes()).hexdigest() == row['sha256']
eq = read('build/collector-equivalence.json')
for row in eq['files']:
    a = subprocess.check_output(['git', 'show', eq['referenceCommit'] + ':' + row['path']], cwd=REPO)
    b = subprocess.check_output(['git', 'show', eq['sourceCommit'] + ':' + row['path']], cwd=REPO)
    assert a == b and hashlib.sha256(b).hexdigest() == row['sha256']
for host in ('deck', 'windows'):
    r = read('build/' + host + '-receipt.json')
    assert r['sourceCommit'] == receipt['sourceCommit'] and all(c['exitCode'] == 0 for c in r['commands'])
    assert r['symbols'] and not any(' tb_lookup' in s for s in r['symbols'])
print(f'PASS: {len(expected)} files; all28 guest attempts PASS, exact inputs/sources/modes, no operator events; 10 comparisons and2 A/A controls recomputed. All performance comparisons unqualified.')

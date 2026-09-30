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

runs = read('host/ablation-runs.json')
assert sum(not r['warmup'] for r in runs) == 960
assert sum(r['warmup'] for r in runs) == 32
checksums = {}
for r in runs:
    assert r['iterations'] == 5000000 and r['work'] in (0, 16)
    checksums.setdefault(r['work'], r['checksum'])
    assert checksums[r['work']] == r['checksum']
    s = r['stage']
    assert r['started'] == (0 if s in (0, 1, 7) else r['iterations'])
    assert r['completed'] == (r['iterations'] if s in (3, 4, 5, 6) else 0)
    assert (r['timing_samples'] > 0) == (s == 5)
for summary in read('host/ablation-summary.json'):
    rows = [r for r in runs if r['comparison'] == summary['comparison']
            and r['work'] == summary['work'] and not r['warmup']]
    a, b = summary['baselineStage'], summary['candidateStage']
    assert len(rows) == 60 and {r['pair'] for r in rows} == set(range(30))
    med = {s: statistics.median(r['cpu_ns'] for r in rows if r['stage'] == s)
           for s in (a, b)}
    assert med[a] == summary['medianCpuNs'][str(a)]
    assert med[b] == summary['medianCpuNs'][str(b)]
    close(med[b] - med[a], summary['absoluteDeltaNs'])
    close(100 * (med[a] - med[b]) / med[a], summary['improvementPercent'])
    pairs = []
    for i in range(30):
        pair = [r for r in rows if r['pair'] == i]
        assert len(pair) == 2 and {r['position'] for r in pair} == {0, 1}
        values = {r['stage']: r['cpu_ns'] for r in pair}
        pairs.append(100 * (values[a] - values[b]) / values[a])
    close(statistics.median(pairs), summary['pairedMedianImprovementPercent'])
    close(quantile([-p for p in pairs], .95), summary['p95SlowdownPercent'])
    close(max(-p for p in pairs), summary['worstSlowdownPercent'])
    rng = random.Random(245272 + summary['work'] + a * 10 + b)
    bootstrap = [statistics.median(rng.choices(pairs, k=30)) for _ in range(10000)]
    for p, value in zip((.025, .975), summary['pairedBootstrapMedianCI95']):
        close(quantile(bootstrap, p), value)
for row in read('host/configuration-equivalence.json')['stages']:
    assert row['equal'] and row['recordedSha256'] == row['regeneratedConfigBinarySha256']
eq = read('host/rebase-equivalence.json')
for row in eq['files']:
    a = subprocess.check_output(['git', 'show', eq['measuredCommit'] + ':' + row['path']], cwd=REPO)
    b = subprocess.check_output(['git', 'show', eq['rebasedCodeCommit'] + ':' + row['path']], cwd=REPO)
    assert a == b and row['equal'] and hashlib.sha256(b).hexdigest() == row['sha256']
for name in ('jump-cache-probe.c', 'jump-cache-probe.h'):
    source = subprocess.check_output(['git', 'show', 'f105bdc7c1d8dd0919adf0ea7e3b102b6abf3c4d:accel/tcg/' + name], cwd=REPO)
    assert source == (HERE / 'host/reference' / name).read_bytes()

retained = excluded = 0
for host in ('deck', 'windows'):
    folder = HERE / host
    observations = read(host + '/observations.json')
    fixed_inputs = None
    for obs in observations:
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
        assert job['Environment']['XEMU_TCG_JUMP_CACHE_PROBE'] == ('off' if obs['mode'] in ('disabled', 'parent') else obs['mode'])
        assert job['Plan'] == [] and job['Diagnostics'] == []
        assert not job['RuntimeState']['Isolation']['AllowUncontrolledDriverCache']
        xiso = job['RuntimeState']['Xiso']
        assert xiso['IsoSha256'] == '74a10c400f4fb280dcb4037e38e7e4d3150e73e0061a04f65b3c68cc61a49e63'
        assert xiso['CatalogId'] == 'sha256:34b8ee7f91f3412b5e7758aab9f4ca08746ef2e97fee8b5c61be8947e145f7f4'
        assert xiso['Tests'] == [obs['leaf']]
        assert xiso['Settings'] == {'warmup_iterations': 0, 'measurement_iterations_multiplier': 1, 'gpu_completion_mode': 'per_iteration'}
        state = json.loads((run / 'diagnostics/run-state/report.json').read_text())
        assert not state['DriverNamespaceVerified'] and not state['AllowUncontrolledDriverCache'] and not state['ComparisonReady']
        retained += obs['retained']
        excluded += not obs['retained']
    for cmp in read(host + '/comparisons.json'):
        selected = [o for o in observations if o['retained'] and o['leaf'] == cmp['leaf'] and o['phase'] == cmp['phase']]
        values = {m: [o['medianUs'] for o in selected if o['mode'] == m] for m in (cmp['referenceMode'], cmp['candidateMode'])}
        assert all(len(v) == 2 for v in values.values())
        a, b = [statistics.median(values[m]) for m in (cmp['referenceMode'], cmp['candidateMode'])]
        assert a == cmp['referenceMedianUs'] and b == cmp['candidateMedianUs']
        close(b-a, cmp['absoluteDeltaUs'])
        close(100*(a-b)/a, cmp['improvementPercent'])
    for control in read(host + '/aa-controls.json'):
        rows = [o for o in observations if o['retained'] and o['leaf'] == control['leaf'] and o['phase'] == 'A/A']
        assert len(rows) == 2 and all(o['mode'] == 'off' for o in rows)
        a, b = [o['medianUs'] for o in rows]
        assert a == control['firstOffUs'] and b == control['secondOffUs']
        close(100*(a-b)/a, control['apparentImprovementPercent'])
for row in read('host/parent-configuration-equivalence.json'):
    assert row['configEquivalentExceptProbeMacro']
    assert [d['name'] for d in row['buildOptionDifferences']] == ['xemu_tcg_jump_cache_probe']
assert retained == 48 and excluded == 40, (retained, excluded)
print(f'PASS: {len(expected)} files; 960 retained host observations +32 warmups; all16 summaries/CIs; 48 retained +40 excluded guest attempts, exact inputs/modes, checksums, timings; all comparisons unqualified.')

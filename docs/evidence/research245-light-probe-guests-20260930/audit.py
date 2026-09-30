"""Recompute all observer comparisons from archived guest output and verify pins."""
import hashlib
import json
from pathlib import Path
import statistics

HERE = Path(__file__).resolve().parent
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
builds = json.loads((HERE.parent / 'research245-light-probe-20260930/exact-builds.json').read_text())
count = 0
for host in ['deck', 'windows']:
    folder = HERE / host
    receipt = json.loads((folder / 'receipt.json').read_text())
    schedule = json.loads((folder / 'schedule.json').read_text())
    observations = json.loads((folder / 'observations.json').read_text())
    assert receipt['sourceCommit'] == schedule['sourceCommit'] == '3a3d3c390fbb5d3835206d47614a86467e7a9330'
    build = next(b for b in builds if b['label'] == ('native' if host == 'deck' else 'windows'))
    assert build['executableSha256'] == receipt['executableSha256'] == schedule['executableSha256']
    assert receipt['attempts'] == len(observations) == len(schedule['attempts']) == 24
    assert not receipt['driverQualification'] and not receipt['allowUncontrolledDriverCache']
    assert receipt['acceptedProductionImprovementPercent'] is None and not receipt['actualRetentionCandidate']
    fixed_inputs = None
    for obs, planned in zip(observations, schedule['attempts']):
        assert all(obs[k] == v for k, v in planned.items())
        run = folder / 'runs' / obs['runId']
        assessment = json.loads((run / 'assessment.json').read_text())
        assert assessment['Execution'] == 'completed' and assessment['Correctness'] == 'passed' and assessment['Evidence'] == 'complete'
        assert assessment['Comparison'] == 'ineligible' and assessment['ComparisonReasons'] == obs['comparisonReasons']
        normalized = json.loads((run / 'guest/normalized-results.json').read_text())
        raw_bytes = (run / 'guest/results.txt').read_bytes()
        assert normalized['sourceSha256'] == hashlib.sha256(raw_bytes).hexdigest()
        raw = json.loads(raw_bytes)[0]
        assert raw['id'] == obs['leaf'] and raw['outcome'] == 'PASS'
        assert raw['metadata']['operations'] == (50000000 if obs['leaf'].endswith('code_stable') else 1000000)
        assert raw['metadata']['oracle_status'] == 'PASS' and raw['metadata']['result_checksum'] == raw['metadata']['expected_checksum']
        assert len(raw['raw_results']) == raw['sample_count'] == obs['samples'] == 10
        assert statistics.median(raw['raw_results']) == obs['medianUs'] == normalized['records'][0]['MedianUs']
        assert normalized['records'][0]['Correct'] and all(c['Passed'] for c in normalized['checks'])
        launch = json.loads((run / 'launch.json').read_text())
        manifest = json.loads((run / 'input-manifest.json').read_text())
        assert launch['executableSha256'] == manifest['ExecutableSha256'] == receipt['executableSha256']
        assert launch['experiment']['Variant'] == obs['mode']
        assert not launch['experiment']['AllowOperatorIntervention']
        assert all(i['Verified'] and i['Sha256'] == i['ExpectedSha256'] for i in manifest['Inputs'])
        inputs = {i['Path']: i['Sha256'] for i in manifest['Inputs']}
        if fixed_inputs is None: fixed_inputs = inputs
        assert inputs == fixed_inputs
        job = json.loads((run / 'job.json').read_text())
        assert job['Environment']['XEMU_TCG_JUMP_CACHE_PROBE'] == obs['mode']
        assert job['Plan'] == [] and job['Diagnostics'] == []
        assert not job['RuntimeState']['Isolation']['AllowUncontrolledDriverCache']
        xiso = job['RuntimeState']['Xiso']
        assert xiso['IsoSha256'] == '74a10c400f4fb280dcb4037e38e7e4d3150e73e0061a04f65b3c68cc61a49e63'
        assert xiso['CatalogId'] == 'sha256:34b8ee7f91f3412b5e7758aab9f4ca08746ef2e97fee8b5c61be8947e145f7f4'
        assert xiso['Tests'] == [obs['leaf']] and xiso['Settings'] == {'warmup_iterations': 0, 'measurement_iterations_multiplier': 1, 'gpu_completion_mode': 'per_iteration'}
        state = json.loads((run / 'diagnostics/run-state/report.json').read_text())
        assert not state['DriverNamespaceVerified'] and not state['AllowUncontrolledDriverCache'] and not state['ComparisonReady']
    comparisons = json.loads((folder / 'comparisons.json').read_text())
    assert len(comparisons) == 8
    for cmp in comparisons:
        selected = [o for o in observations if o['leaf'] == cmp['leaf'] and o['phase'] == 'balanced-mode-sweep']
        values = {mode: [o['medianUs'] for o in selected if o['mode'] == mode] for mode in ['off', cmp['candidateMode']]}
        assert all(len(v) == 2 for v in values.values())
        a, b = [statistics.median(values[m]) for m in ['off', cmp['candidateMode']]]
        assert a == cmp['referenceMedianUs'] and b == cmp['candidateMedianUs']
        assert b - a == cmp['absoluteDeltaUs']
        assert abs(100 * (a - b) / a - cmp['improvementPercent']) < 1e-10
        assert cmp['comparison'] == 'ineligible'
    for control in json.loads((folder / 'aa-controls.json').read_text()):
        rows = [o for o in observations if o['leaf'] == control['leaf'] and o['phase'] == 'A/A']
        assert len(rows) == 2 and all(o['mode'] == 'off' for o in rows)
        a, b = [o['medianUs'] for o in rows]
        assert a == control['firstOffUs'] and b == control['secondOffUs']
        assert abs(100 * (a - b) / a - control['apparentImprovementPercent']) < 1e-10
    inventory = json.loads((folder / 'canonical-inventory.json').read_text())
    for item in inventory:
        if item['published']:
            file = folder / 'runs' / item['runId'] / item['path']
            assert file.stat().st_size == item['bytes'] and hashlib.sha256(file.read_bytes()).hexdigest() == item['sha256']
    count += len(observations)
print(f'PASS: {len(expected)} files; {count} guest leaf attempts and source/executable/input pins; raw sample medians, mode comparisons and A/A; all correctness PASS, all comparisons remain unqualified.')

"""Summarize archived native attempts without pooling frame/CPU observations.

Adapted from the retained issue229 audit recipe. AAAA stays separate
from causal A/B comparisons, and report source hashes are checked.

Run after HTTP collection into CAMPAIGN/collected. Missing attempts
remain explicit. All frozen controls and balanced attempts remain visible. This script
does not contact the runner, schedule work, filter failures or modify outcomes.
"""
from pathlib import Path
import argparse
import hashlib
import json
import statistics

parser = argparse.ArgumentParser(description='Audit collected issue234 native attempts without changing outcomes.')
parser.add_argument('campaign', type=Path)
parser.add_argument('--out', type=Path, required=True, help='New summary file; existing outputs are refused')
args = parser.parse_args()
ROOT = args.campaign.resolve()
if args.out.exists():
    raise SystemExit('Refusing an existing output file')
plan = json.loads((ROOT / 'plan.json').read_text())
assert len({a['id'] for a in plan['attempts']}) == len(plan['attempts']), 'Duplicate frozen IDs'
for order in dict.fromkeys(a['order'].upper() for a in plan['attempts']):
    ordered = [a for a in plan['attempts'] if a['order'].upper() == order]
    assert [a['position'] for a in ordered] == [1, 2, 3, 4], 'Invalid block positions'
    assert ''.join(a['physicalRole'] for a in ordered) == order, 'Wrong physical build order'
role_hashes = {role: {a['executableSha256'] for a in plan['attempts'] if a['physicalRole'] == role}
               for role in ['A', 'B']}
assert all(len(v) == 1 for v in role_hashes.values()), 'Ambiguous physical build identities'
assert role_hashes['A'] != role_hashes['B'], 'Parent and candidate executable are identical'
records = {}
for path in (ROOT / 'collected').glob('*/result.json'):
    result = json.loads(path.read_text())
    assert result['job'] not in records, 'Duplicate attempts are not allowed'
    records[result['job']] = (path.parent, result)
def optional_json(path):
    if not path.exists():
        return None
    try:
        return json.loads(path.read_text())
    except (OSError, ValueError) as error:
        identity_issues.append(dict(path=str(path.relative_to(ROOT)),
                                   reason='unreadable optional evidence', error=str(error)))
        return None


def normalized(value):
    if isinstance(value, dict):
        return {key.lower(): normalized(item) for key, item in value.items()}
    if isinstance(value, list):
        return [normalized(item) for item in value]
    return value


rows = []
identity_issues = []
identities = []
for attempt in plan['attempts']:
    identifier = attempt['id']
    role = attempt['physicalRole']
    assert role in ('A', 'B'), 'Unknown physical build role'
    variant = 'parent' if role == 'A' else 'candidate'
    if identifier not in records:
        rows.append(dict(id=identifier, variant=variant, order=attempt['order'].upper(),
                         position=attempt['position'], state='not-collected'))
        continue
    directory, result = records.pop(identifier)
    if result.get('executableSha256') != attempt['executableSha256']:
        identity_issues.append(dict(id=identifier, reason='result executable identity missing or rejected'))
    performance = optional_json(directory / 'performance.json') or {}
    storage = optional_json(directory / 'diagnostics/run-state/report.json') or {}
    assessment = result.get('assessment') or {}
    job = optional_json(directory / 'job.json')
    inputs = optional_json(directory / 'input-manifest.json')
    launch = optional_json(directory / 'launch.json')
    missing = [name for name in ['performance.json', 'diagnostics/run-state/report.json',
                                'job.json', 'input-manifest.json', 'launch.json']
               if not (directory / name).exists()]
    if missing:
        identity_issues.append(dict(id=identifier, reason='missing evidence', files=missing))
    if job is not None:
        frozen = json.loads((ROOT / (identifier + '-draft.json')).read_text())['job']
        if normalized(job) != normalized(frozen):
            identity_issues.append(dict(id=identifier, reason='job differs from prelaunch draft'))
    if inputs is not None and launch is not None:
        identities.append(dict(
            profile=performance.get('ProfileSha256'), storage=storage.get('ContractSha256'),
            host=launch.get('host'),
            inputs=[{key: row.get(key) for key in ['Path', 'Role', 'Bytes', 'Sha256', 'ExpectedSha256', 'Verified']}
                    for row in inputs['Inputs'] if row['Path'] != 'xemu'],
            runtime=inputs['RuntimeFiles']))
        if inputs['ExecutableSha256'] != attempt['executableSha256']:
            identity_issues.append(dict(id=identifier, reason='input executable differs from frozen attempt'))
    values = {}
    # Verify original bytes behind the runner's stored calculations.
    for source in performance.get('Sources', []):
        source_path = directory / source['Path']
        if not source_path.resolve().is_relative_to(directory.resolve()) or source_path.is_symlink():
            identity_issues.append(dict(id=identifier, reason='unsafe performance source path'))
        elif not source_path.is_file():
            identity_issues.append(dict(id=identifier, reason='missing performance source', path=source['Path']))
        elif hashlib.sha256(source_path.read_bytes()).hexdigest() != source['Sha256']:
            identity_issues.append(dict(id=identifier, reason='performance source hash differs', path=source['Path']))
    if (storage.get('DriverQualification') != 'mesa_private_disk_writes_observed' or
        storage.get('AllowUncontrolledDriverCache') is not False or
        storage.get('TargetStopped') is not True or storage.get('Issues') != []):
        identity_issues.append(dict(id=identifier, reason='private stopped-target Mesa qualification absent or invalid'))
    if performance.get('Complete') is not True or performance.get('Errors') != []:
        identity_issues.append(dict(id=identifier, reason='stored performance report incomplete or contains errors'))
    if performance.get('Monitoring') is not None:
        values['cpuCorePercent'] = performance['Monitoring']['Cpu']['Mean']
        values['cpuSamples'] = performance['Monitoring']['Cpu']['Count']
    if performance.get('Flips') is not None:
        values['flipControlFps'] = performance['Flips']['CadenceFps']
    if performance.get('Frames') is not None:
        intervals = performance['Frames']['IntervalsMs']
        for key in ['Mean', 'P95', 'P99', 'Count']:
            values['frameControl' + key] = intervals[key]
    rows.append(dict(id=identifier, variant=variant, order=attempt['order'].upper(),
                     position=attempt['position'], state='archived', runId=result['runId'],
                     executableSha256=result['executableSha256'], execution=result['status'],
                     exitCode=result['exitCode'], assessment=assessment,
                     performanceProfileSha256=performance.get('ProfileSha256'),
                     storageContractSha256=storage.get('ContractSha256'),
                     driverQualification=storage.get('DriverQualification'),
                     driverIssues=storage.get('Issues'), targetStopped=storage.get('TargetStopped'),
                     cacheWaiver=storage.get('AllowUncontrolledDriverCache'), missingEvidence=missing, values=values))
assert not records, 'Unplanned attempt found in campaign collection'
complete = all(row['state'] == 'archived' for row in rows)
eligible = complete and all(row['assessment'].get('Comparison') == 'eligible' for row in rows)
if identities and any(identity != identities[0] for identity in identities[1:]):
    identity_issues.append(dict(reason='profile, storage, host or fixed inputs differ across attempts'))
comparable = eligible and not identity_issues and len(identities) == len(rows)
if comparable and any(len(row['values']) != 7 for row in rows):
    comparable = False
    identity_issues.append(dict(reason='required measurement values are missing'))
comparisons = []
if comparable:
    for order in ['ABBA', 'BAAB', 'both']:
        selected = [row for row in rows if row['order'] in ('ABBA', 'BAAB') and (order == 'both' or row['order'] == order)]
        for metric, unit, direction in [('flipControlFps', 'fps', 'higher'),
                                        ('frameControlMean', 'ms', 'lower'),
                                        ('frameControlP95', 'ms', 'lower'),
                                        ('frameControlP99', 'ms', 'lower'),
                                        ('cpuCorePercent', 'core_pct', 'diagnostic')]:
            a = [row['values'][metric] for row in selected if row['variant'] == 'parent']
            b = [row['values'][metric] for row in selected if row['variant'] == 'candidate']
            baseline, candidate = statistics.median(a), statistics.median(b)
            delta = candidate - baseline
            improvement = None if direction == 'diagnostic' else 100 * delta / baseline * (1 if direction == 'higher' else -1)
            comparisons.append(dict(order=order, metric=metric, unit=unit, direction=direction,
                                    parentMedian=baseline, candidateMedian=candidate,
                                    afterMinusBefore=delta, improvementPercent=improvement,
                                    parentCount=len(a), candidateCount=len(b),
                                    parentRange=[min(a), max(a)], candidateRange=[min(b), max(b)]))
controls = [row for row in rows if row['order'] == 'AAAA']
control_ranges = {}
if controls and all(row['state'] == 'archived' for row in controls):
    for metric in ['cpuCorePercent', 'flipControlFps', 'frameControlMean', 'frameControlP95', 'frameControlP99']:
        if all(metric in row['values'] for row in controls):
            values = [row['values'][metric] for row in controls]
            median = statistics.median(values)
            control_ranges[metric] = dict(count=len(values), median=median, min=min(values), max=max(values),
                rangePercentOfMedian=100 * (max(values)-min(values)) / median if median else None)
summary = dict(controlRanges=control_ranges, planSha256=hashlib.sha256((ROOT / 'plan.json').read_bytes()).hexdigest(),
               plannedAttempts=len(rows), archivedAttempts=sum(row['state'] == 'archived' for row in rows),
               complete=complete, allCanonicallyEligible=eligible, attempts=rows,
               comparable=comparable, identityIssues=identity_issues, comparisons=comparisons,
               interpretation='Median of per-attempt metrics; no pooled samples. Core-percent is diagnostic, not fixed-work cost. Host-clock flip-control cadence is not necessarily rendered FPS. Cold launch plus scene warmup; power/driver RAM/OS page cache uncontrolled; PCM absent.')
args.out.parent.mkdir(parents=True, exist_ok=True)
args.out.write_text(json.dumps(summary, indent=2) + '\n')
print(json.dumps({key: summary[key] for key in ['plannedAttempts', 'archivedAttempts', 'complete', 'allCanonicallyEligible']}))

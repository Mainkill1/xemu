"""Summarize archived native attempts without pooling frame/CPU observations.

Run after HTTP collection into native-balanced-v3/collected. Missing attempts
remain explicit. The procedure pilot is deliberately excluded. This script
does not contact the runner, schedule work, filter failures or modify outcomes.
"""
from pathlib import Path
import hashlib
import json
import statistics

ROOT = Path('/home/codex/src/steamdeck-xemu/artifacts/issue229-dsp-multiply/native-balanced-v3')
plan = json.loads((ROOT / 'plan.json').read_text())
records = {}
for path in (ROOT / 'collected').glob('*/result.json'):
    result = json.loads(path.read_text())
    assert result['job'] not in records, 'Duplicate attempts are not allowed'
    records[result['job']] = (path.parent, result)
rows = []
for attempt in plan['attempts']:
    identifier = attempt['id']
    if identifier not in records:
        rows.append(dict(id=identifier, variant=attempt['variant'], order=attempt['order'],
                         position=attempt['position'], state='not-collected'))
        continue
    directory, result = records.pop(identifier)
    assert result['executableSha256'] == attempt['executableSha256']
    performance = json.loads((directory / 'performance.json').read_text())
    storage = json.loads((directory / 'diagnostics/run-state/report.json').read_text())
    assessment = result['assessment']
    values = {}
    if performance['Monitoring'] is not None:
        values['cpuCorePercent'] = performance['Monitoring']['Cpu']['Mean']
        values['cpuSamples'] = performance['Monitoring']['Cpu']['Count']
    if performance['Flips'] is not None:
        values['flipControlFps'] = performance['Flips']['CadenceFps']
    if performance['Frames'] is not None:
        intervals = performance['Frames']['IntervalsMs']
        for key in ['Mean', 'P95', 'P99', 'Count']:
            values['frameControl' + key] = intervals[key]
    rows.append(dict(id=identifier, variant=attempt['variant'], order=attempt['order'],
                     position=attempt['position'], state='archived', runId=result['runId'],
                     executableSha256=result['executableSha256'], execution=result['status'],
                     exitCode=result['exitCode'], assessment=assessment,
                     performanceProfileSha256=performance['ProfileSha256'],
                     storageContractSha256=storage['ContractSha256'],
                     driverQualification=storage['DriverQualification'],
                     driverIssues=storage['Issues'], targetStopped=storage['TargetStopped'],
                     cacheWaiver=storage['AllowUncontrolledDriverCache'], values=values))
assert not records, 'Unplanned attempt found in campaign collection'
complete = all(row['state'] == 'archived' for row in rows)
eligible = complete and all(row['assessment']['Comparison'] == 'eligible' for row in rows)
comparisons = []
if eligible:
    for order in ['abba', 'baab', 'both']:
        selected = [row for row in rows if order == 'both' or row['order'] == order]
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
summary = dict(planSha256=hashlib.sha256((ROOT / 'plan.json').read_bytes()).hexdigest(),
               plannedAttempts=len(rows), archivedAttempts=sum(row['state'] == 'archived' for row in rows),
               complete=complete, allCanonicallyEligible=eligible, attempts=rows,
               comparisons=comparisons,
               interpretation='Median of per-attempt metrics; no pooled samples. Core-percent is diagnostic, not fixed-work cost. Host-clock flip-control cadence is not necessarily rendered FPS. Cold launch plus scene warmup; power/driver RAM/OS page cache uncontrolled; PCM absent.')
(ROOT / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
print(json.dumps({key: summary[key] for key in ['plannedAttempts', 'archivedAttempts', 'complete', 'allCanonicallyEligible']}))

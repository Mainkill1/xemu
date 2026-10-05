"""Summarize owned canonical results without modifying tests or pooling frames."""
import csv
import json
import statistics
import sys
from pathlib import Path

p = Path(__file__).resolve().parent
pr = 'final'
apps = json.loads((p / f'{pr}-apps.json').read_text())
metrics = ['cpu', 'guestFlipCadence', 'avgFrameMs', 'p95FrameMs',
           'p99FrameMs', 'maxFrameMs']
order = ['parent', 'candidate', 'candidate', 'parent',
         'candidate', 'parent', 'parent', 'candidate']
rows = []
for host in ['deck', 'win']:
    source = p / f'{pr}-{host}-outcomes.json'
    if not source.exists():
        continue
    for r in json.loads(source.read_text()):
        result = r['result']
        rid = result.get('runId')
        row = {k: r[k] for k in ['title', 'cell', 'variant', 'request']}
        app = apps[f'{host}-{r["variant"]}-{r["title"]}']
        manualFile = p / f'{pr}-manual-scene-review.json'
        flags = json.loads(manualFile.read_text()) if manualFile.exists() else []
        if isinstance(flags, dict):
            flags = flags.get('findings', flags.get('anomalies', []))
        row['manualSceneFindings'] = [v for v in flags if isinstance(v, dict) and v.get('runId') == rid]
        row.update(host=host, runId=rid,
                   outcome=(result.get('result') or {}).get('outcome', {}),
                   failureCode=result.get('code'),
                   executableSha256=app['sha256'], source=app['productSource'],
                   configSha256=app['configSha256'])
        if rid:
            evidence = p / f'{pr}-{host}-evidence' / rid
            perf = evidence / 'performance.json'
            if perf.exists():
                a = json.loads(perf.read_text())['analysis']
                intervals = (a.get('frames') or {}).get('intervalsMs') or {}
                row.update(cpu=((a.get('monitoring') or {}).get('cpu') or {}).get('mean'),
                           guestFlipCadence=(a.get('flips') or {}).get('cadenceFps'),
                           **{key: intervals.get(stat) for key, stat in
                              [('avgFrameMs', 'mean'), ('p95FrameMs', 'p95'),
                               ('p99FrameMs', 'p99'), ('maxFrameMs', 'max')]})
            detail = evidence / 'result.json'
            if detail.exists():
                row['failureDetail'] = json.loads(detail.read_text()).get('detail')
        rows.append(row)

summary = []
for host in ['deck', 'win']:
    for title in ['morrowind', 'conker', 'pgr2']:
        attempts = [r for r in rows if r['host'] == host and r['title'] == title]
        completed = [r for r in attempts if r['outcome'].get('execution') == 'completed'
                     and r['outcome'].get('correctness') == 'passed'
                     and r['outcome'].get('evidence') == 'complete']
        completed.sort(key=lambda r: r['cell'])
        complete = (len(completed) == 8
                    and [r['cell'] for r in completed] == list(range(1, 9))
                    and [r['variant'] for r in completed] == order
                    and len({r['runId'] for r in completed}) == 8)
        s = dict(host=host, title=title, attempted=len(attempts),
                 completed=len(completed), completeOrder=complete,
                 runnerEligible=sum(r['outcome'].get('comparison') == 'eligible'
                                    for r in completed),
                 statistic='Median of four per-run values per side; independent ABBA/BAAB blocks',
                 metricSource='Canonical performance API; READ_3D cadence, not SDL presentations; frame milliseconds; CPU one core=100%',
                 sceneLimitation='PGR2 cross-host flyover/countdown mismatch; diagnostic, original inputs unchanged' if title == 'pgr2' else None,
                 metrics={},
                 manualSceneFindings=[{'runId': r['runId'], 'findings': r['manualSceneFindings']} for r in attempts if r.get('manualSceneFindings')],
                 manualSceneKnownFailure=any(r.get('manualSceneFindings') for r in attempts))
        if complete:
            for key in metrics:
                if any(r.get(key) is None for r in completed):
                    s['metrics'][key] = {'status': 'missing canonical metric'}
                    continue
                aa = [r[key] for r in completed if r['variant'] == 'parent']
                bb = [r[key] for r in completed if r['variant'] == 'candidate']
                a, b = statistics.median(aa), statistics.median(bb)
                def gain(x, y):
                    if not x:
                        return None
                    return 100 * ((y - x) if key == 'guestFlipCadence' else (x - y)) / x
                m = dict(A=a, B=b, improvementPercent=gain(a, b),
                         rangeA=[min(aa), max(aa)], rangeB=[min(bb), max(bb)])
                for label, block in [('ABBA', completed[:4]), ('BAAB', completed[4:])]:
                    x = statistics.median(r[key] for r in block if r['variant'] == 'parent')
                    y = statistics.median(r[key] for r in block if r['variant'] == 'candidate')
                    m[label] = gain(x, y)
                s['metrics'][key] = m
        summary.append(s)

(p / f'{pr}-current-per-run.json').write_text(json.dumps(rows, indent=2))
(p / f'{pr}-three-title-summary.json').write_text(json.dumps(summary, indent=2))
columns = ['host', 'title', 'cell', 'variant', 'request', 'runId', *metrics,
           'executableSha256', 'source', 'configSha256', 'failureCode']
with (p / f'{pr}-current-per-run.csv').open('w') as f:
    writer = csv.DictWriter(f, fieldnames=columns, extrasaction='ignore')
    writer.writeheader()
    writer.writerows(rows)
for s in summary:
    print(s['host'], s['title'], s['completed'], '/8 completed;', s['runnerEligible'], 'eligible')
    if s['completeOrder']:
        for key, m in s['metrics'].items():
            print(' ', key, m)

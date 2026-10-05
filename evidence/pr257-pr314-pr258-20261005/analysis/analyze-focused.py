"""Report actual balanced focused results without overriding oracle/cache gates."""
import csv, json, statistics
from pathlib import Path
p = Path(__file__).resolve().parent
rows = []; summary = {}
for host, version in [('win', 'v1'), ('deck', 'v2')]:
    campaign = f'p258-{host}-vk-texture-focused-{version}'
    af = p / (campaign + '-attempts.json')
    if not af.exists():
        summary[host] = {'campaign': campaign, 'complete': False, 'reason': 'acquisition pending'}
        continue
    attempts = json.loads(af.read_text()); plan = json.loads((p / (campaign + '-plan.json')).read_text())
    grouped = {}
    for a in attempts:
        if not a.get('runId'): continue
        d = p / f'stack-{host}-xiso-vk-evidence' / a['runId']
        if not (d/'guest/results.txt').exists(): continue
        guest = json.loads((d/'guest/results.txt').read_text())
        for leaf in guest:
            if leaf['kind'] != 'leaf': continue
            row = {'host':host, 'backend':'Vulkan', 'test':leaf['id'], 'side':a['variant'], 'order':'ABBA' if a['label'][-1] in '12' else 'BAAB', 'label':a['label'], 'runId':a['runId'], 'meanUs':statistics.mean(leaf['raw_results']), 'guestTotalWorkUs':leaf['total_us'], 'intrinsicOutcome':leaf['outcome'], 'framebuffer':leaf['framebuffer_fnv1a64'], 'correctness':a['correctness'], 'comparison':a['comparison'], 'sharedChunkProcessWallMs':json.loads((d/'result.json').read_text())['durationMs']}
            grouped.setdefault(leaf['id'], []).append(row)
    out = []
    for key in plan['tests']:
        values = grouped.get(key, []); sides = {s:[v for v in values if v['side']==s] for s in ['reference','candidate']}
        a = [v['meanUs'] for v in sides['reference']]; b = [v['meanUs'] for v in sides['candidate']]
        before = statistics.median(a) if a else None; after = statistics.median(b) if b else None
        item = {'host':host, 'backend':'Vulkan', 'test':key, 'beforeMedianUs':before, 'afterMedianUs':after, 'differenceUs':after-before if before is not None and after is not None else None, 'improvementPercent':100*(before-after)/before if before and after is not None else None, 'referenceRuns':len(a), 'candidateRuns':len(b), 'referenceRangeUs':[min(a),max(a)] if a else None, 'candidateRangeUs':[min(b),max(b)] if b else None, 'correctnessPassed':all(v['correctness']=='passed' for v in values) and len(values)==8, 'comparisonEligible':sum(v['comparison']=='eligible' for v in values), 'intrinsicOutcomes':sorted(set(v['intrinsicOutcome'] for v in values)), 'framebuffers':sorted(set(v['framebuffer'] for v in values)), 'sourceRows':values, 'orderImprovements':{}}
        for order in ['ABBA','BAAB']:
            sa=[v['meanUs'] for v in sides['reference'] if v['order']==order];sb=[v['meanUs'] for v in sides['candidate'] if v['order']==order]
            if len(sa)==len(sb)==2:
                ba=statistics.mean(sa);bb=statistics.mean(sb);item['orderImprovements'][order]=100*(ba-bb)/ba
        rows.append(item);out.append(item)
    summary[host]={'campaign':campaign, 'attempts':len(attempts), 'selectedLeaves':len(plan['tests']), 'complete':len(attempts)==24 and all(a['terminal'] for a in attempts), 'overOnePercent':[r['test'] for r in out if r['improvementPercent'] is not None and abs(r['improvementPercent'])>1], 'timingMeaning':'Median of four per-run mean sample costs per side; diagnostic descriptive differences. Oracle/cache eligibility remains authoritative.'}
(p/'current-focused-comparisons.json').write_text(json.dumps({'summary':summary,'comparisons':rows},indent=2))
fields=['host','backend','test','beforeMedianUs','afterMedianUs','differenceUs','improvementPercent','referenceRuns','candidateRuns','correctnessPassed','comparisonEligible']
with (p/'current-focused-comparisons.csv').open('w',newline='') as f:
 w=csv.DictWriter(f,fieldnames=fields,extrasaction='ignore');w.writeheader();w.writerows(rows)
print(json.dumps(summary,indent=2))
for row in rows: print(row['host'],row['test'],row['beforeMedianUs'],row['afterMedianUs'],row['improvementPercent'],row['correctnessPassed'],row['comparisonEligible'],row['orderImprovements'])

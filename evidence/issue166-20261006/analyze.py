from pathlib import Path
import json
p=Path(__file__).resolve().parent
folder=next((p/'native/a1/deck-evidence').iterdir())
perf=json.loads((folder/'performance.json').read_text());window=perf['analysis']['frames']
rows=[json.loads(l) for l in (folder/'tb-lookup.ndjson').read_text().splitlines()]
for r in rows:
 assert r['probes']==sum(r[k] for k in ['hit','empty','pc','cs','flags','cflags'])
 assert r['pc_same_page']<=r['pc']
result=dict(source='a0eb4130b028548b5d142944699bfa57eed6b40d',parent='890fa7954db91993056bb7236269b49af2e20c4b',runId=perf['runId'],app=json.loads((p/'native/a1/deck-app.json').read_text()),test=json.loads((p/'native/a1/deck-saved.json').read_text()),authoredFrameWindow=window,routes={},limits=['Diagnostic counts perturb execution; no performance acceptance claim.','Delta of first and last cumulative snapshots inside authored frame window; no boot counts.','QHT outcome may lag the probe by one in-flight lookup or guest fault.','policy_blocked covers listed guards, not a proof of all proposed inline-path eligibility.','io_recompile_total is process-thread cumulative and repeated across route rows; do not sum across routes.'])
for name in ['dispatcher','generic','i32']:
 rr=[r for r in rows if r['route']==name and window['windowStartUs']<=r['timestamp_us']<=window['windowEndUs']]
 assert len(rr)>=2,(name,len(rr))
 first,last=rr[0],rr[-1];delta={k:last[k]-first[k] for k in first if isinstance(first[k],int) and k!='timestamp_us'}
 n=delta['probes'];delta['hitPercent']=100*delta['hit']/n if n else None
 delta['pcCollisionPercent']=100*delta['pc']/n if n else None
 delta['samePageShareOfPcCollisions']=100*delta['pc_same_page']/delta['pc'] if delta['pc'] else None
 result['routes'][name]=dict(startUs=first['timestamp_us'],endUs=last['timestamp_us'],seconds=(last['timestamp_us']-first['timestamp_us'])/1e6,snapshots=len(rr),delta=delta)
(p/'summary.json').write_text(json.dumps(result,indent=2))
print(json.dumps(result['routes'],indent=2))

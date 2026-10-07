import json
from pathlib import Path
p=Path(__file__).resolve().parent
rows=[]
for folder in [p,p/'candidate290']:
 r=json.loads((folder/'wait-comparison.json').read_text())[0]
 if folder!=p:
  r['limits']=['One instrumented opportunity pair; no controlled-cache or balanced acceptance claim.','Telemetry renderer flip-stall IDs and READ3D IDs differ; only common timestamp window used.','Fence/queue waits are host elapsed time, not GPU execution duration. Partial timing coverage remains sampled.']
 directory=next((folder/'deck-evidence').iterdir())
 trace=[json.loads(line) for line in (directory/'vk-perf.ndjson').open()]
 window=[line for line in trace if line['type']=='frame' and r['windowStartUs']<=line['timestamp_us']<=r['windowEndUs']]
 if 'depth_alias_pending_retirements_peak_per_guest_frame' in r['totals']:
  r['totals'].pop('depth_alias_pending_retirements_peak_per_guest_frame')
  r['maxPendingAliasRetirements']=max(line['depth_alias_pending_retirements_peak_per_guest_frame'] for line in window)
 r['sceneCheck']={'start':'Conker menu','end':'Conker menu','verified':'Manual review of complete saved screenshots','limit':'Animated scene phase differs; not identical guest simulation state.'}
 rows.append(r)
a,b=rows
originals=[json.loads((x/'definition.json').read_text())['job'] for x in [p,p/'candidate290']]
assert a['app']['configSha256']==b['app']['configSha256']
for k in ['plan','controllerInput','timeoutSeconds','runtimeState','environment','workload','operations']:
 assert originals[0].get(k)==originals[1].get(k),k
summary={'reference':a,'candidate':b,'identity':{'settingsIdentical':True,'frozenProcedureIdentical':True,'noExtraWaitsOrInputs':True,'referenceMain':'39338d3d044bef6eae9d28c2f4676474b5137f4c','candidate':'02ebd503171dff15d4f17d27151be3c4fd5c79ed'},'comparisonEligible':False,'reason':'Single opportunity pair, uncontrolled driver cache, animated menu phases not identical; no balanced qualification.'}
(p/'opportunity-comparison.json').write_text(json.dumps(summary,indent=2)+'\n')
for r in rows:
 print(r['variant'],'cadence',round(r['guestCadenceFps'],3),'cpu',round(r['cpuMean'],2),'frames',r['frames'],'intervals',r['frameIntervalsMs'])
 print('submits/frame',r['totals']['vk_queue_submit_calls_per_guest_frame']/r['frames'])
 for kind in ['finish','aux']:
  print(kind,{k:round(v['sampledWaitUsPerFrame']/1000,3) for k,v in r[kind].items() if v['submits']})
 print('alias',r['totals'])
print('cadence directional change',100*(b['guestCadenceFps']/a['guestCadenceFps']-1))

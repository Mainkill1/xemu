import json
from pathlib import Path
p=Path(__file__).resolve().parent
rows=[]
scenes={'a1':'Conker menu, start/end verified','b1':'Conker menu, start/end verified','c1':'PGR2 flyover to parked, not stationary acceptance','d1':'PGR2 intro at both boundaries; insufficient frame samples','e1':'PGR2 flyover to parked; external profile, not acceptance'}
for v in ['a1','b1','c1','d1','e1']:
 files=list((p/'native'/v/'deck-evidence').glob('*/performance.json'))
 if not files:continue
 d=json.loads(files[0].read_text());a=d['analysis'];rows.append({'variant':v,'runId':d['runId'],'app':json.loads((p/'native'/v/'deck-app.json').read_text()),'procedure':json.loads((p/'native'/v/'deck-saved.json').read_text()),'guestFlipCadence':a['flips']['cadenceFps'],'cpuMean':a['monitoring']['cpu']['mean'],'frameIntervalsMs':a['frames']['intervalsMs'] if a['frames'] else None,'analysisErrors':a['errors'],'manualSceneReview':scenes[v],'acceptance':False,'limits':'One development sample; uncontrolled driver cache. Scene failures/profile interventions remain explicit; no stable gain claim.'})
(p/'native-results.json').write_text(json.dumps(rows,indent=2)+'\n')
for r in rows:print(r['variant'],r['guestFlipCadence'],r['cpuMean'],r['frameIntervalsMs'])

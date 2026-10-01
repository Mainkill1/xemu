"""Retain a finished campaign and each complete archived artifact inventory."""
from pathlib import Path
import sys,json
R=Path('/home/codex/src/steamdeck-xemu');sys.path.insert(0,str(R/'worktrees/runner-plan-timeout/scripts'))
from runner_transport import RunnerApi
from runner_workflows import Workflows
id=sys.argv[1];api=RunnerApi('http://10.0.0.123:9368');wf=Workflows(api);out=R/'artifacts/issue229-dsp-multiply/xiso-controls'/id;out.mkdir(parents=True,exist_ok=True)
status=api.json('/api/v1/xiso-campaigns/'+id);assert status['terminal'],status
(out/'status.json').write_text(json.dumps(status,indent=2)+'\n')
plan=api.json('/api/v1/xiso-campaigns/'+id+'?view=plan');(out/'plan.json').write_text(json.dumps(plan,indent=2)+'\n')
attempts=[];offset=0
while offset is not None:
 page=api.json(f'/api/v1/xiso-campaigns/{id}/attempts?offset={offset}&limit=100');attempts.extend(page['items']);offset=page.get('nextOffset')
assert len(attempts)==status['attemptCount']==4
(out/'attempts.json').write_text(json.dumps({'items':attempts,'nextOffset':None},indent=2)+'\n')
for attempt in attempts:
 assert attempt['terminal']
 receipt=wf.collect(attempt['id'],out,[],True);(out/(attempt['id']+'-collected.json')).write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps({'runId':receipt['runId'],'files':receipt['fileCount'],'bytes':receipt['bytes'],'correctness':attempt['correctness'],'comparison':attempt['comparison']}),flush=True)

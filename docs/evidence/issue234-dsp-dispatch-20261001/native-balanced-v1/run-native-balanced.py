from pathlib import Path
import json,sys,time
r=Path('/home/codex/src/steamdeck-xemu');sys.path.insert(0,str(r/'worktrees/runner-plan-timeout/scripts'))
from runner_transport import RunnerApi
from runner_workflows import Workflows
api=RunnerApi('http://10.0.0.123:9368');wf=Workflows(api);out=r/'artifacts/issue234-dsp-dispatch/native-balanced-v1';plan=json.loads((out/'plan.json').read_text())
for attempt in plan['attempts']:
 name=attempt['id'];state=wf.status(name)
 if state['state']=='draft':
  host=api.json('/api/v1/status');assert host['Phase']=='idle' and host['CurrentJob'] is None and not host['Queue']['Testing'] and not host['Queue']['Pending'],host
  state=wf.submit_draft(name);print('submitted',name,attempt['physicalRole'],state['state'],flush=True)
 while state['state']!='tested':
  assert state['state'] in ['queued','testing'],state
  state=wf.wait(name,45)
  print('status',name,state.get('state'),state.get('runId'),flush=True)
 result=wf.result(name);assert result['available'] and result['state']=='tested';(out/(name+'-terminal.json')).write_text(json.dumps(result,indent=2)+'\n');print('terminal',name,result['outcome'],flush=True)
print('ALL 12 NATIVE ATTEMPTS TERMINAL',flush=True)

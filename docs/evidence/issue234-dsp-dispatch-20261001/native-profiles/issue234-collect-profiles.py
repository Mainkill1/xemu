from pathlib import Path
import json,sys
r=Path('/home/codex/src/steamdeck-xemu');sys.path.insert(0,str(r/'worktrees/runner-plan-timeout/scripts'))
from runner_transport import RunnerApi
from runner_workflows import Workflows
wf=Workflows(RunnerApi('http://10.0.0.123:9368'));status=wf.api.json('/api/v1/status');assert status['CurrentJob'] is None and not status['Queue']['Testing'] and not status['Queue']['Pending']
for b in ['c','jit']:
 name='issue234-deck-parent-'+b+'-profile-v1';p=r/'artifacts/issue234-dsp-dispatch/native-profiles'/b
 result=wf.result(name);assert result['state']=='tested' and result['available'];(p/'result-summary.json').write_text(json.dumps(result,indent=2)+'\n');print(b,result,flush=True)
 d=wf.collect(name,p/'collected',[],True);(p/'collected-receipt.json').write_text(json.dumps(d,indent=2)+'\n');print('collected',b,flush=True)

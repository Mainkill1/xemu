from pathlib import Path
import sys,json
r=Path('/home/codex/src/steamdeck-xemu');sys.path.insert(0,str(r/'worktrees/runner-plan-timeout/scripts'))
from runner_transport import RunnerApi
from runner_workflows import Workflows
api=RunnerApi('http://10.0.0.123:9368');p=r/'artifacts/issue229-dsp-multiply/native-packages/issue229-deck-pgr2-stationary-full-c-v2';b=json.loads((p/'baked.json').read_text());labels={'variant':'parent','reference':'ee5ce48b48784f999af374c1452003f8b2b1230f'}
(p/'rejected-label-shape.json').write_text(json.dumps({'phase':'before-job-creation','error':'HTTP400: experiment is not a member of AgentTestRunRequest','rejectedLabels':{'experiment':labels},'correction':'Use supported top-level variant/reference fields; same intended job ID, no emulator execution occurred.'},indent=2)+'\n')
result=Workflows(api).run_test('issue229-deck-pgr2-stationary-full-c-v2',b['revision'],'issue229-deck-parent-stationary-v2-20261001',r/'artifacts/issue229-dsp-multiply/xiso-preparation/issue229-parent-native-linux-v1',labels,1024*1024);(p/'parent-started.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))

import json,pathlib,sys
sys.path.insert(0,'/home/codex/src/steamdeck-xemu/worktrees/runner-mesa-qualification/scripts')
from runner_transport import RunnerApi,job_url
from runner_workflows import Workflows
root=pathlib.Path('/home/codex/src/steamdeck-xemu/artifacts/issue229-dsp-multiply'); test='issue229-deck-pgr2-full-dsp-c-profile-v1';p=root/'native-packages'/test
api=RunnerApi('http://10.0.0.123:9368');wf=Workflows(api);d=json.loads((p/'manifest.json').read_text());seed=d['id'];status=api.json('/api/v1/status');assert not status['CurrentJob'] and status['Queue']['Pending']==status['Queue']['Testing']==0
receipt=api.json('/api/v1/jobs','POST',d);(p/'created.json').write_text(json.dumps(receipt,indent=2)+'\n')
api.json(job_url(seed)+'/reuse','POST',{'sourceJobId':'issue229-deck-pgr2-full-dsp-c-v1-seed'});wf.operation(seed)
entry=next(f for f in d['files'] if f['path'].endswith('.debug'));f=root/'native-builds/parent-linux/debug-root/usr/lib/debug/.build-id/7c'/entry['path'];api.upload_path(job_url(seed)+'/files/'+entry['path'],f,entry['length'],entry['sha256'])
baked=api.json('/api/v1/tests/'+test+'/bake','POST',{'sourceJobId':seed,'description':'Issue229 full C diagnostic only: same native pilot input path plus 30s perf capture; exact parent debug symbols; not performance qualification','buildFiles':['xemu']});(p/'baked.json').write_text(json.dumps(baked,indent=2)+'\n');print(json.dumps(baked),flush=True)

from pathlib import Path
import copy,json,sys
root=Path('/home/codex/src/steamdeck-xemu');sys.path.insert(0,str(root/'worktrees/runner-plan-timeout/scripts'))
from runner_transport import RunnerApi,job_url
from runner_workflows import Workflows
api=RunnerApi('http://10.0.0.123:9368');wf=Workflows(api)
out=root/'artifacts/issue234-dsp-dispatch/native-profiles';out.mkdir(parents=True,exist_ok=True)
state=api.json('/api/v1/status');assert state['Phase']=='idle' and state['CurrentJob'] is None and not state['Queue']['Testing'] and not state['Queue']['Pending']
source=json.loads((root/'artifacts/issue229-dsp-multiply/native-packages/issue229-deck-pgr2-stationary-full-c-v3/manifest.json').read_text())
config=(root/'artifacts/issue229-dsp-multiply/native-packages/issue229-deck-pgr2-stationary-full-c-v3/xemu.toml').read_text()
diag=json.loads((root/'artifacts/issue229-dsp-multiply/native-packages/issue229-deck-pgr2-full-dsp-c-profile-v1/manifest.json').read_text())['job']['diagnostics']
from runner_transport import declaration
for backend in ['c','jit']:
 name='issue234-deck-parent-'+backend+'-profile-v1';p=out/backend;p.mkdir(exist_ok=True)
 (p/'xemu.toml').write_text(config.replace('use_dsp_jit = false','use_dsp_jit = '+str(backend=='jit').lower()))
 d=copy.deepcopy(source);d['id']=name;d['job']['id']=name;j=d['job'];j['tags']=['issue234','deck','pgr2','diagnostic-only',backend];j['timeoutSeconds']=600
 j['experiment'].update(id='issue234-parent-backend-profiles-v1',variant=backend,variedFactors=['C versus JIT backend; diagnostic attribution only'],controlledFactors=['exact previous-main executable','same scene navigation','60 s stationary warmup','90 s profile segment','private guest state','private cold Mesa namespace at launch'],allowDiagnostics=True)
 j['operations'].update(mode='smoke',allowDiagnostics=True);j['diagnostics']=copy.deepcopy(diag)
 for item in j['plan']:
  if item['type']=='wait' and item.get('delayMs')==300000:item['delayMs']=90000
 index=next(i for i,x in enumerate(j['plan']) if x['type']=='screenshot' and x.get('name')=='recording-start')
 j['plan'].insert(index+1,{'type':'diagnostic','diagnosticId':'dsp-cpu-profile'})
 j['workload']['analysis'].update(frameTailSeconds=90,minimumCpuSamples=160)
 row=declaration(p,'xemu.toml'); replacement={'path':row['Path'],'length':row['Length'],'sha256':row['Sha256'],'executable':False}
 d['files']=[replacement if x['path']=='xemu.toml' else x for x in d['files']]
 assert next(x for x in d['files'] if x['path']=='xemu')['sha256']=='4a6e0dae0a83a870aebde726ca40687f35c9293f3da1147a6d7d14e8c4e8e01a'
 for item in j['inputs']:
  if item['path']=='xemu.toml':item.update(role='fixed-full-DSP-'+backend,expectedSha256=None)
 (p/'manifest.json').write_text(json.dumps(d,indent=2)+'\n')
 receipt=api.json('/api/v1/jobs','POST',d);(p/'created.json').write_text(json.dumps(receipt,indent=2)+'\n')
 if wf.status(name)["state"] == "busy":wf.operation(name)
 api.json(job_url(name)+'/reuse','POST',{'sourceJobId':source['id']});wf.operation(name)
 api.upload_path(job_url(name)+'/files/xemu.toml',p/'xemu.toml',replacement['length'],replacement['sha256'])
 baked=api.json('/api/v1/tests/'+name+'/bake','POST',{'sourceJobId':name,'description':'Issue234 exact-parent '+backend.upper()+' backend diagnostic profile, 60s scene warmup and 90s observation; not candidate speedup qualification','buildFiles':['xemu']})
 (p/'baked.json').write_text(json.dumps(baked,indent=2)+'\n');print(name,baked,flush=True)

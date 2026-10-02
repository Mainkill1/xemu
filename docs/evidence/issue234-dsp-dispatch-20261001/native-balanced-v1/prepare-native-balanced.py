from pathlib import Path
import json,copy,sys,hashlib,shutil
root=Path('/home/codex/src/steamdeck-xemu');sys.path.insert(0,str(root/'worktrees/runner-plan-timeout/scripts'))
from runner_transport import RunnerApi,job_url,declaration
from runner_workflows import Workflows
api=RunnerApi('http://10.0.0.123:9368');wf=Workflows(api)
out=root/'artifacts/issue234-dsp-dispatch/native-balanced-v1';out.mkdir(exist_ok=False)
state=api.json('/api/v1/status');assert state['Phase']=='idle' and state['CurrentJob'] is None and not state['Queue']['Testing'] and not state['Queue']['Pending']
source=json.loads((root/'artifacts/issue229-dsp-multiply/native-packages/issue229-deck-pgr2-stationary-full-c-v3/manifest.json').read_text());request=copy.deepcopy(source)
test='issue234-deck-pgr2-stationary-c-v1';seed=test+'-seed';request['id']=seed;j=request['job'];j['id']=seed;j['tags']=['issue234','deck','pgr2','stationary-grid','C-DSP-dispatch'];j['experiment'].update(id='issue234-deck-native-c-balanced-v1',variant='build',reference='ee5ce48b48784f999af374c1452003f8b2b1230f',variedFactors=['typed C DSP handler dispatch'],controlledFactors=['same exact-parent runtime libraries','same scene navigation','private guest state','private cold Mesa launch namespace','60s scene warmup','300s stationary window','full C DSP VPworkers0 Vulkan vsyncoff'],allowDiagnostics=False)
for i in j['inputs']:
 if i['path']=='xemu':i['role']='parent-or-typed-dispatch-candidate'
(out/'seed-manifest.json').write_text(json.dumps(request,indent=2)+'\n')
created=api.json('/api/v1/jobs','POST',request);(out/'seed-created.json').write_text(json.dumps(created,indent=2)+'\n')
if wf.status(seed)['state']=='busy':wf.operation(seed)
api.json(job_url(seed)+'/reuse','POST',{'sourceJobId':source['id']});wf.operation(seed)
baked=api.json('/api/v1/tests/'+test+'/bake','POST',{'sourceJobId':seed,'description':'Issue234 typed C DSP dispatch: same parent runtime libraries and fixed PGR2 stationary scene; 60s warmup/300s observation; original 160-frame and540-CPU floors; no profiling in windows','buildFiles':['xemu']})
(out/'baked.json').write_text(json.dumps(baked,indent=2)+'\n')
binaries={'A':root/'artifacts/issue229-dsp-multiply/native-builds/parent-linux/squashfs-root/usr/bin/xemu','B':root/'artifacts/issue234-dsp-dispatch/native-builds/candidate-linux/squashfs-root/usr/bin/xemu'}
expected={'A':'4a6e0dae0a83a870aebde726ca40687f35c9293f3da1147a6d7d14e8c4e8e01a','B':'4e7bf9783374901cb1b81b24d3932f0018900a638a8989ff1ccea0d1f36c5c26'}
for role,p in binaries.items():assert hashlib.sha256(p.read_bytes()).hexdigest()==expected[role]
attempts=[]
for order in ['AAAA','ABBA','BAAB']:
 for position,role in enumerate(order,1):
  identifier=f'issue234-deck-c-v1-{order.lower()}-{position}'
  files=[declaration(binaries[role].parent,'xemu',True)]
  body={'id':identifier,'testId':test,'revision':baked['revision'],'files':files,'experimentId':'issue234-deck-native-c-balanced-v1','variant':'parent' if role=='A' else 'candidate','reference':'ee5ce48b48784f999af374c1452003f8b2b1230f'}
  attempts.append({'id':identifier,'order':order,'position':position,'physicalRole':role,'executableSha256':expected[role],'request':body})
plan={'testId':test,'revision':baked['revision'],'sourceHead':'58df82fe70dfc5ad07cecac974ce84db81559dac','ciProductCommit':'ecb2a59e3a89f34f079787dd7b02c06b30578e2b','sourceTreeEqual':True,'attempts':attempts,'orders':['AAAA','ABBA','BAAB'],'policy':'Keep every attempt. No reruns, intervention, cache waivers or retroactive outcome edits. Start the next only after the owned previous target is terminal.','controlledLibraryChoice':'Both use exact parent runtime libraries, because candidate AppImage GLib differs. Build slots replace only xemu.','limits':'Synthetic replay gains are not game gains; power/frequency uncontrolled; no PCM oracle. Fixed 300s observation preserves prior160-frame/540-CPU floors on this sparse scene.'}
(out/'plan.json').write_text(json.dumps(plan,indent=2)+'\n');print('frozen',hashlib.sha256((out/'plan.json').read_bytes()).hexdigest(),flush=True)
for attempt in attempts:
 api.json('/api/v1/jobs/from-test','POST',attempt['request']);wf.operation(attempt['id']);assert wf.status(attempt['id'])['state']=='draft'
 p=binaries[attempt['physicalRole']];wf.upload_files(attempt['id'],p.parent,attempt['request']['files'],1024*1024)
 detail=api.json(job_url(attempt['id']));(out/(attempt['id']+'-draft.json')).write_text(json.dumps(detail,indent=2)+'\n');print('prepared',attempt['id'],attempt['physicalRole'],flush=True)

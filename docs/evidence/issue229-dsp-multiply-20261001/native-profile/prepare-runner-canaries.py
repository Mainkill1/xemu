import copy,json,pathlib,sys
sys.path.insert(0,'/home/codex/src/steamdeck-xemu/worktrees/runner-mesa-qualification/scripts')
from runner_transport import RunnerApi,job_url
from runner_workflows import Workflows
r=pathlib.Path('/home/codex/src/steamdeck-xemu/artifacts/issue229-dsp-multiply');api=RunnerApi('http://10.0.0.123:9368');wf=Workflows(api)
for lane in ['diagnostic-timeout','followup-quit']:
 test='runner848-deck-native-'+lane+'-v1';p=r/'native-packages'/test;p.mkdir(exist_ok=True);d=json.loads((r/'native-packages/issue229-deck-pgr2-full-dsp-c-v1/manifest.json').read_text());j=d['job'];j['id']=test+'-seed';d['id']=j['id'];j['timeoutSeconds']=60;j['operations']={'mode':'smoke','allowManualInput':False,'allowPreview':False,'allowDiagnostics':True,'allowBulkTransfers':False};j['experiment']['id']='runner848-native-failure-canary';j['experiment']['variant']=lane;j['experiment']['allowDiagnostics']=True;j['experiment']['variedFactors']=['expected diagnostic failure'];j['workload']={'requirePlanCompletion':True};j['tags']=['runner848','native-runner-reliability-canary','not-game-performance'];j['expectedExecutableSha256']=next(x['sha256'] for x in d['files'] if x['path']=='xemu')
 j['plan']=[{'type':'quit'}];j['diagnostics']=[]
 if lane=='diagnostic-timeout':
  j['diagnostics']=[{'id':'expected-timeout','type':'external','durationMs':100,'pauseBefore':False,'resumeDuring':False,'pauseAfter':False,'toolExecutable':'/usr/bin/sleep','toolArguments':['20']}];j['plan']=[{'type':'diagnostic','diagnosticId':'expected-timeout'}]
 (p/'manifest.json').write_text(json.dumps(d,indent=2)+'\n');receipt=api.json('/api/v1/jobs','POST',d);(p/'created.json').write_text(json.dumps(receipt,indent=2)+'\n');api.json(job_url(d['id'])+'/reuse','POST',{'sourceJobId':'issue229-deck-pgr2-full-dsp-c-v1-seed'});wf.operation(d['id']);b=api.json('/api/v1/tests/'+test+'/bake','POST',{'sourceJobId':d['id'],'description':'Native runner reliability canary only: '+lane+'; fixed parent xemu/firmware; no game performance contract','buildFiles':['xemu']});(p/'baked.json').write_text(json.dumps(b,indent=2)+'\n');print(json.dumps(b),flush=True)

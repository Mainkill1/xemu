"""Freeze the v3 full-C pilot. FlipTailSamples counts five-second summaries.
40 summaries cover roughly 200-240 seconds, fitting inside a 300-second
stationary observation. Keep the 160 positive-frame and 5%/96 image gates.
This is a new frozen procedure; v2 failures stay unchanged. HTTP only.
"""
from pathlib import Path
import copy,hashlib,json,sys,tomllib
R=Path('/home/codex/src/steamdeck-xemu');sys.path.insert(0,str(R/'worktrees/runner-plan-timeout/scripts'))
from runner_transport import RunnerApi,job_url
from runner_workflows import Workflows
api=RunnerApi('http://10.0.0.123:9368');wf=Workflows(api);A=R/'artifacts/issue229-dsp-multiply';test='issue229-deck-pgr2-stationary-full-c-v3';seed=test+'-seed';out=A/'native-packages'/test;out.mkdir(exist_ok=True)
source=api.json('/api/v1/test-configs/issue229-deck-pgr2-full-dsp-c-v1/6944074de8e224d19c16ebdb6f2ceec05e6342e6941fc3595a81efd81f28ba06');(out/'source-definition.json').write_text(json.dumps(source,indent=2)+'\n');definition=source['definition'];job=copy.deepcopy(definition['job']);job['id']=seed;job['timeoutSeconds']=600
job['tags']=['issue229','pgr2','stationary-grid','full-dsp','c','cold-launch-scene-warmup','pilot-not-performance-proof']
job['experiment']={'id':'issue229-pgr2-stationary-full-c-v3','variant':'build','reference':'ee5ce48b48784f999af374c1452003f8b2b1230f','variedFactors':['C DSP multiply'],'controlledFactors':['same-parent-runtime-libraries','private guest state','cold Mesa namespace at launch','60-second stationary warmup','300-second observation','fullscreen 4:3 viewport','full DSP C VPworkers0 Vulkan vsyncoff'],'requireCorrectnessPass':True,'requireCompleteEvidence':True,'allowOperatorIntervention':False,'allowDiagnostics':False}
job['operations']={'mode':'benchmark','allowPreview':False,'allowManualInput':False,'allowPauseResume':False,'allowDiagnostics':False,'allowBulkTransfers':False}
start=next(i for i,s in enumerate(job['plan']) if s['type']=='segment_start');end=next(i for i,s in enumerate(job['plan']) if s['type']=='segment_end');prefix=job['plan'][:start];suffix=job['plan'][end+1:]
def step(kind,**kw):
 item=copy.deepcopy(job['plan'][start]);item.update(type=kind,name=None,button=None,durationMs=100,delayMs=0);item.update(kw);return item
job['plan']=prefix+[step('wait',delayMs=60000),step('segment_start',name='stationary-grid'),step('screenshot',name='recording-start'),step('wait',delayMs=300000),step('screenshot',name='recording-end'),step('segment_end',name='stationary-grid')]+suffix
job['workload']['analysis'].update(segment='stationary-grid',frameTailSeconds=300,flipTailSamples=40,minimumCpuSamples=540)
assert job['workload']['analysis']['minimumFrameSamples']==160 and job['workload']['minimumMetricSamples']==160
for check in job['workload']['correctnessChecks']:
 assert check['minimumNonBlackPixelRatio']==0.05 and check['nonBlackPixelThreshold']==96
 check['imageRegion']={'x':1/12,'y':0,'width':5/6,'height':1}
config=(A/'native-packages/issue229-deck-pgr2-full-dsp-c-v1/xemu.toml').read_text().replace('[display.window]\n','[display.window]\nfullscreen_on_startup = true\nfullscreen_exclusive = false\n')
parsed=tomllib.loads(config);assert parsed['audio']['use_dsp'] and not parsed['audio']['use_dsp_jit'];assert parsed['audio']['vp']['num_workers']==0
(out/'xemu.toml').write_text(config);files=copy.deepcopy(definition['files']);row=next(f for f in files if f['path']=='xemu.toml');row.update(length=(out/'xemu.toml').stat().st_size,sha256=hashlib.sha256((out/'xemu.toml').read_bytes()).hexdigest());input_row=next(i for i in job['inputs'] if i['path']=='xemu.toml');input_row['expectedSha256']=row['sha256'];input_row['role']='fixed-full-C-stationary-fullscreen'
request={'id':seed,'job':job,'files':files};(out/'manifest.json').write_text(json.dumps(request,indent=2)+'\n');created=api.json('/api/v1/jobs','POST',request);(out/'created.json').write_text(json.dumps(created,indent=2)+'\n')
if wf.status(seed)['state']=='busy':wf.operation(seed)
api.json(job_url(seed)+'/reuse','POST',{'sourceJobId':definition['sourceJobId']});wf.operation(seed)
api.upload_path(job_url(seed)+'/files/xemu.toml',out/'xemu.toml',row['length'],row['sha256'])
baked=api.json('/api/v1/tests/'+test+'/bake','POST',{'sourceJobId':seed,'description':'Full C stationary-grid pilot: cold private launch, 60s warmup, 300s observation, fullscreen viewport; retains 5%/96 image and160-frame gates; CPU sample floor540; no gameplay gain claim','buildFiles':['xemu']});(out/'baked.json').write_text(json.dumps(baked,indent=2)+'\n');print(json.dumps({'test':test,'revision':baked['revision'],'timeoutSeconds':600,'minimumFrames':160,'imageMinimum':0.05,'imageThreshold':96,'started':False}),flush=True)

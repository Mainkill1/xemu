"""Freeze #229 CPU controls through maintained HTTP clients; never start a run."""
import copy,hashlib,json,pathlib,sys
ROOT=pathlib.Path('/home/codex/src/steamdeck-xemu')
sys.path.insert(0,str(ROOT/'worktrees/runner-plan-timeout/scripts'))
from runner_transport import RunnerApi,job_url
from runner_workflows import Workflows
from runner_tests import upload_application
A=ROOT/'artifacts/issue229-dsp-multiply';out=A/'xiso-preparation'
api=RunnerApi('http://10.0.0.123:9368');wf=Workflows(api)
source=json.loads((out/'qualified-cpu-reference-definition.json').read_text())['definition']
parent=A/'native-builds/parent-linux/squashfs-root/usr'
for backend in ['vulkan','opengl']:
 test=f'issue229-deck-xiso-cpu-{backend}-v1';seed=test+'-seed';p=out/test;p.mkdir(exist_ok=True)
 job=copy.deepcopy(source['job']);job.update(id=seed,expectedExecutableSha256=None,tags=['issue229','unaffected-cpu-controls','xiso',backend],operations={'mode':'benchmark','allowPreview':False,'allowManualInput':False,'allowPauseResume':False,'allowDiagnostics':False,'allowBulkTransfers':False})
 job['arguments']=['-config_path','{packageDir}/xemu.toml','-name','xemu-issue229-cpu,debug-threads=on','-msg','timestamp=on']
 job['experiment']={'id':f'issue229-cpu-controls-{backend}','variant':'build','reference':'ee5ce48b48784f999af374c1452003f8b2b1230f','variedFactors':['C DSP multiply source'],'controlledFactors':['exact ISO/catalog/oracle/firmware','same parent runtime-library bundle','private HDD/EEPROM','cold Mesa disk cache','warmups3 multiplier4 per_iteration','128MiB','DSP JIT enabled (unaffected control)'],'requireCorrectnessPass':True,'requireCompleteEvidence':True,'allowOperatorIntervention':False,'allowDiagnostics':False}
 config=(A.parent/'research245-cpu-main-2d289cb-deck-v3/xemu.toml').read_text()
 assert 'renderer = "VULKAN"' in config
 config=config.replace('renderer = "VULKAN"','renderer = "'+backend.upper()+'"')
 config=config.replace('[audio]\n','[audio]\nuse_dsp = false\n')
 (p/'xemu.toml').write_text(config)
 local={'xemu.toml':p/'xemu.toml','xemu':parent/'bin/xemu'}
 for f in (parent/'lib').iterdir():
  if f.is_file():local['lib/'+f.name]=f
 files=[f for f in copy.deepcopy(source['files']) if not f['path'].startswith('lib/') and f['path'] not in ['xemu.toml','xemu']]
 for name,f in local.items():files.append({'path':name,'length':f.stat().st_size,'sha256':hashlib.sha256(f.read_bytes()).hexdigest(),'executable':name=='xemu'})
 files.sort(key=lambda x:x['path']);job['requiredFiles']=[f['path'] for f in files];job['inputs']=[{'path':f['path'],'role':'issue229-fixed-runtime' if f['path']!='xemu' else 'parent-or-candidate','hash':True,'expectedSha256':None if f['path']=='xemu' else f['sha256']} for f in files]
 request={'id':seed,'job':job,'files':files};(p/'manifest.json').write_text(json.dumps(request,indent=2)+'\n')
 created=api.json('/api/v1/jobs','POST',request);(p/'created.json').write_text(json.dumps(created,indent=2)+'\n')
 if wf.status(seed)['state']=='busy':wf.operation(seed)
 api.json(job_url(seed)+'/reuse','POST',{'sourceJobId':source['sourceJobId']});wf.operation(seed)
 for name,f in local.items():
  row=next(x for x in files if x['path']==name);api.upload_path(job_url(seed)+'/files/'+name,f,row['length'],row['sha256'])
 ready=api.json(job_url(seed)+'/files');(p/'files-ready.json').write_text(json.dumps(ready,indent=2)+'\n')
 baked=api.json('/api/v1/tests/'+test+'/bake','POST',{'sourceJobId':seed,'description':'Issue229 unaffected CPU controls; exact historical CPU oracle; matched parent runtime libraries; private inputs; '+backend,'buildFiles':['xemu']});(p/'baked.json').write_text(json.dumps(baked,indent=2)+'\n')
 suite=api.json('/api/v1/xiso-suites','POST',{'id':test+'-suite','testId':test,'revision':baked['revision'],'settings':{'warmup_iterations':3,'measurement_iterations_multiplier':4,'gpu_completion_mode':'per_iteration'}});(p/'suite.json').write_text(json.dumps(suite,indent=2)+'\n')
 print(json.dumps({'test':test,'revision':baked['revision'],'suite':suite['id'],'files':len(files),'qualification':suite['qualification']}),flush=True)
for variant in ['parent','candidate']:
 identity='issue229-'+variant+'-native-linux-v1';p=out/identity;p.mkdir(exist_ok=True);target=p/'xemu';src=A/f'native-builds/{variant}-linux/squashfs-root/usr/bin/xemu'
 if not target.exists():target.hardlink_to(src)
 sha=upload_application(api,p,'xemu',identity)
 receipt={'id':identity,'sha256':sha,'source':str(src),'started':False};(p/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(receipt),flush=True)

import copy,hashlib,json,pathlib,sys
sys.path.insert(0,'/home/codex/src/steamdeck-xemu/worktrees/runner-mesa-qualification/scripts')
from runner_transport import RunnerApi,job_url,declaration
from runner_workflows import Workflows
ROOT=pathlib.Path('/home/codex/src/steamdeck-xemu/artifacts/issue229-dsp-multiply')
host=sys.argv[1];backend=sys.argv[2]
assert host in ['deck','windows'] and backend in ['c','jit']
api=RunnerApi({'deck':'http://10.0.0.123:9368','windows':'http://10.0.7.1:9368'}[host]);wf=Workflows(api)
source=json.loads((ROOT/(host+'-source-definition.json')).read_text())['definition']
job=copy.deepcopy(source['job']);test=f'issue229-{host}-pgr2-full-dsp-{backend}-v1';seed=test+'-seed';package=ROOT/'native-packages'/test;package.mkdir(parents=True,exist_ok=True)
job['id']=seed;job['expectedExecutableSha256']=None;job['experiment']={'id':f'issue229-{host}-full-dsp-{backend}-20261001','variant':'build','reference':'ee5ce48b4878','variedFactors':['native C DSP multiply'],'controlledFactors':['fixed-full-DSP-setting','same-native-procedure','matching-runtime-dependencies'],'requireCorrectnessPass':True,'requireCompleteEvidence':True,'allowOperatorIntervention':False,'allowDiagnostics':False}
job['tags']=['issue229','pgr2',host,'full-dsp',backend,'native-release','private-guest-state']
job['operations']={'mode':'benchmark'}
for item in job['inputs']:
 item['expectedSha256']=None
 if item['path'] in ['xemu','xemu.exe']:item['role']='parent-or-candidate-emulator'
 if item['path']=='xemu.toml':item['role']='pgr2-fixed-full-dsp-'+backend
config=(ROOT/(host+'-source.toml')).read_text().replace('use_dsp = false','use_dsp = true')
config=config.replace('use_dsp_jit = true','use_dsp_jit = '+('true' if backend=='jit' else 'false'))
if 'use_dsp =' not in config:config=config.replace('[audio]\n','[audio]\nuse_dsp = true\n')
(package/'xemu.toml').write_text(config)
files=copy.deepcopy(source['files']);files=[f for f in files if f['path']!='xemu-opengl.toml'];job['requiredFiles']=[p for p in job['requiredFiles'] if p!='xemu-opengl.toml'];job['inputs']=[i for i in job['inputs'] if i['path']!='xemu-opengl.toml']
local_files={'xemu.toml':package/'xemu.toml'}
if host=='deck':
 bundle=ROOT/'native-builds/parent-linux/squashfs-root/usr';local_files['xemu']=bundle/'bin/xemu'
 for p in (bundle/'lib').iterdir():
  if p.is_file():local_files['lib/'+p.name]=p
else:
 local_files['xemu.exe']=ROOT/'native-builds/parent-windows/xemu.exe'
for p,path in local_files.items():
 row={'path':p,'length':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'executable':p in ['xemu','xemu.exe']}
 index=next((i for i,f in enumerate(files) if f['path']==p),None)
 if index is None:files.append(row);job['requiredFiles'].append(p)
 else:files[index]=row
request={'id':seed,'job':job,'files':files};(package/'manifest.json').write_text(json.dumps(request,indent=2)+'\n')
receipt=api.json('/api/v1/jobs','POST',request);(package/'created.json').write_text(json.dumps(receipt,indent=2)+'\n')
status=wf.status(seed)
if status['state']=='busy':wf.operation(seed)
api.json(job_url(seed)+'/reuse','POST',{'sourceJobId':source['sourceJobId']});wf.operation(seed)
for p,path in local_files.items():
 row=next(f for f in files if f['path']==p)
 api.upload_path(job_url(seed)+'/files/'+p,path,row['length'],row['sha256'])
missing=api.json(job_url(seed)+'/files')
(package/'files-ready.json').write_text(json.dumps(missing,indent=2)+'\n')
slots=['xemu' if host=='deck' else 'xemu.exe']
baked=api.json('/api/v1/tests/'+test+'/bake','POST',{'sourceJobId':seed,'description':'Issue229 matched full DSP '+backend.upper()+' backend; PGR2 fixed input path and 30s window; same runtime dependencies; kernel change only','buildFiles':slots})
(package/'baked.json').write_text(json.dumps(baked,indent=2)+'\n')
print(json.dumps({'testId':test,'seed':seed,'baked':baked}),flush=True)

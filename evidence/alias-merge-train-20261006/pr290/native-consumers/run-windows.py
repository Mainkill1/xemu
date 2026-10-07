import base64,json,subprocess,sys,hashlib
from pathlib import Path
r=Path.cwd();o=r/'.scratch/alias-train-20261006/pr290/native-consumers';sys.path.insert(0,str(r/'xemu-test-runner-upload-config/scripts'));from runner_transport import RunnerApi
api=RunnerApi('http://10.0.7.1:9368');opts=['-o','ControlPath='+str(r/'.scratch/pr289-win-native.sock'),'-o','BatchMode=yes'];target='codex@10.0.7.1';remote='C:/xemu-lab/pr290-consumer-qualification'
def ps(s):
 encoded=base64.b64encode(s.encode('utf-16-le')).decode();return subprocess.run(['ssh',*opts,target,'powershell -NoProfile -NonInteractive -EncodedCommand '+encoded],capture_output=True,text=True,timeout=90)
def idle(name):
 s=api.json('/api/v1/status');(o/name).write_text(json.dumps(s,indent=2));assert not s.get('ProcessId') and not s['Queue']['Pending'] and not s['Queue']['Testing']
idle('windows-before.json');x=ps("New-Item -ItemType Directory -Force -Path '"+remote+"' | Out-Null");assert x.returncode==0,x.stderr
try:
 x=subprocess.run(['scp',*opts,'-r',str(o/'native.exe'),str(o/'shaders'),target+':'+remote+'/'],capture_output=True,text=True,timeout=90);assert x.returncode==0,x.stderr
 idle('windows-launch-status.json')
 script="$ErrorActionPreference='Stop'; Set-Location '"+remote+"'; $env:XEMU_NATIVE_SHADER_DIR='shaders'; $env:XEMU_NATIVE_DEVICE='NVIDIA'; & ./native.exe; exit $LASTEXITCODE"
 x=ps(script);(o/'windows-native.log').write_text(x.stdout+x.stderr)
 record={'exitCode':x.returncode,'source':'02ebd503171dff15d4f17d27151be3c4fd5c79ed','sha256':hashlib.sha256((o/'native.exe').read_bytes()).hexdigest(),'method':'Actual current converter/compute/image production files, existing configured Windows headers; cached SPIR-V accepted only after exact equality with freshly generated production GLSL; independent CPU/reference-image oracle','conversions':x.stdout.count('PASS native alias format='),'output':x.stdout,'stderr':x.stderr}
 (o/'windows-result.json').write_text(json.dumps(record,indent=2));print(x.stdout,x.stderr,flush=True)
 assert x.returncode==0 and record['conversions']==32 and x.stdout.count('PASS runtime CPU-read/')==8 and x.stdout.count('PASS native retirement')==2 and x.stdout.count('PASS native texture-consumer')==32 and x.stdout.count('PASS native blit-consumer')==6 and 'errors=0' in x.stdout
finally:
 x=ps("Remove-Item -Force '"+remote+"/native.exe'; Remove-Item -Recurse -Force '"+remote+"/shaders'");(o/'windows-cleanup.json').write_text(json.dumps({'exitCode':x.returncode,'stdout':x.stdout,'stderr':x.stderr}));assert x.returncode==0
idle('windows-after.json')

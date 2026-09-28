import pathlib, subprocess
for args, name in [(['model'],'strict'),(['model','sanitized'],'sanitized')]:
    p=pathlib.Path('/tmp/pr260-model-'+name+'.log')
    with p.open('w') as log:
        result=subprocess.run(['python3','/tmp/pr260-assets-build.py']+args,stdout=log,stderr=subprocess.STDOUT)
    print(name,result.returncode,p,flush=True)
    if result.returncode: raise SystemExit(result.returncode)
print('Asset model strict/sanitized gates passed.',flush=True)

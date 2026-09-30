import hashlib,json,subprocess,sys
from pathlib import Path
ROOT=Path('/home/codex/src/steamdeck-xemu'); host=sys.argv[1]
URL={'deck':'http://10.0.0.123:9368','windows':'http://10.0.7.1:9368'}[host]
OUT=ROOT/f'evidence/research245-owner-probe-{host}-20260930';OUT.mkdir(exist_ok=True)
receipt=json.loads((ROOT/f'artifacts/research245-owner-probe/{host}-build-receipt.json').read_text());HEAD=receipt['sourceCommit'];short=HEAD[:8]
exe='xemu.exe' if host=='windows' else 'xemu'
def call(client,label,*args):
    existing=OUT/(label+'.json')
    if existing.exists():
        value=json.loads(existing.read_text())
        if value.get('ok') is not False:return value
    cmd=['python3',str(ROOT/'test-runner/scripts'/client),'--url',URL,*args]
    p=subprocess.run(cmd,capture_output=True);(OUT/(label+'.json')).write_bytes(p.stdout)
    if p.stderr:(OUT/(label+'.stderr.log')).write_bytes(p.stderr)
    assert p.returncode==0,p.stdout.decode()[-1500:]+p.stderr.decode()
    print(host,label,'ok',flush=True);return json.loads(p.stdout)
packages={}
for b in receipt['binaries']:
    app=f'owner245-{host}-{short}-{b["mode"]}'
    call('runner_tests.py',b['mode']+'-upload','upload',b['package'],'--exe',exe,'--id',app)
    packages[b['mode']]=dict(application=app,executableSha256=b['executableSha256'])
base=json.loads((ROOT/f'evidence/research245-light-probe-{host}-20260930/off-definition.json').read_text())
suites={}
for variant in ['disabled','off','counters','timing','all']:
    mode='disabled' if variant=='disabled' else 'enabled';binary=packages[mode]
    job=json.loads(json.dumps(base));job['id']=f'owner245-{host}-{short}-{variant}';job['expectedExecutableSha256']=binary['executableSha256']
    job['environment']['XEMU_TCG_JUMP_CACHE_PROBE']='off' if variant=='disabled' else variant
    for item in job['inputs']:
        if item['path']==exe:item['expectedSha256']=binary['executableSha256']
    job['experiment']['id']=f'owner245-{host}-{short}-build-runtime-matrix';job['experiment']['variant']=variant
    job['experiment']['variedFactors']=['compile-time-probe','runtime-collector-mode']
    job['experiment']['controlledFactors']=[HEAD,'matched-compiler-options-except-probe','fixture-c02a1a44','stable-50m-rewrite-1m','warmups0-multiplier1','128mb','vulkan','same-firmware-private-seed-reference','no-HMP','driver-cache-unverified-no-waiver']
    job['description']='Matched-source compile-disabled/OFF/counters/timing/all observer matrix; no retention candidate. Cache qualification is required; no waiver.'
    path=OUT/(variant+'-definition.json');path.write_text(json.dumps(job,indent=2)+'\n')
    saved=call('runner_tests.py',variant+'-saved','config-upload',job['id'],str(path),'--assets',binary['application'],'--build-file',exe)
    revision=saved.get('revision') or saved.get('Revision');assert revision
    suite=job['id']+'-suite';call('runner_xiso.py',variant+'-suite','register',job['id'],'--revision',revision,'--id',suite,'--warmups','0','--multiplier','1','--completion','per_iteration');suites[variant]=suite
attempts=[]
order=['off','off','disabled','off','counters','timing','all','all','timing','counters','off','disabled']
for leaf in ['code_stable','code_rewrite']:
    for position,variant in enumerate(order):
        binary=packages['disabled' if variant=='disabled' else 'enabled']
        cid=f'o245-{host[:3]}-{short}-{leaf.removeprefix("code_")}-{position+1:02}'
        call('runner_xiso.py',cid+'-selected','select',binary['application'],'--id',cid,'--suite',suites[variant],'--test','cpu_translation_blocks.'+leaf,'--mode','monolithic')
        plan=call('runner_xiso.py',cid+'-plan','plan',cid);assert plan['tests']==['cpu_translation_blocks.'+leaf]
        attempts.append(dict(campaign=cid,leaf='cpu_translation_blocks.'+leaf,mode=variant,position=position,phase='A/A' if position<2 else 'balanced-mode-sweep',executableSha256=binary['executableSha256']))
(OUT/'schedule.json').write_text(json.dumps(dict(sourceCommit=HEAD,packages=packages,application=packages['enabled']['application'],executableSha256=packages['enabled']['executableSha256'],attempts=attempts),indent=2)+'\n')
print(host,'STAGED',len(attempts),flush=True)

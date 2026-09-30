import json,subprocess,sys
from pathlib import Path
ROOT=Path('/home/codex/src/steamdeck-xemu');host=sys.argv[1];URL={'deck':'http://10.0.0.123:9368','windows':'http://10.0.7.1:9368'}[host]
OUT=ROOT/f'evidence/research245-owner-probe-{host}-parent-controls';OUT.mkdir(exist_ok=True)
old=ROOT/f'evidence/research245-owner-probe-{host}-20260930'
candidate=json.loads((old/'schedule.json').read_text());parent=json.loads((ROOT/f'artifacts/research245-owner-probe/parent-{host}-receipt.json').read_text())
exe='xemu.exe' if host=='windows' else 'xemu'
def call(client,label,*args):
    p=subprocess.run(['python3',str(ROOT/'test-runner/scripts'/client),'--url',URL,*args],capture_output=True)
    (OUT/(label+'.json')).write_bytes(p.stdout)
    if p.stderr:(OUT/(label+'.stderr.log')).write_bytes(p.stderr)
    assert p.returncode==0,p.stdout.decode()[-1500:]+p.stderr.decode();return json.loads(p.stdout)
app=f'owner245-{host}-458730bf-parent';call('runner_tests.py','parent-upload','upload',parent['package'],'--exe',exe,'--id',app)
job=json.loads((old/'disabled-definition.json').read_text());job['id']=f'owner245-{host}-458730bf-parent';job['expectedExecutableSha256']=parent['executableSha256'];job['experiment']['variant']='parent'
job['experiment']['variedFactors']=['upstream-parent-vs-candidate-compiled-out'];job['description']='Matched upstream parent versus candidate with probe compilation disabled; immutable inputs, no driver-cache waiver.'
for item in job['inputs']:
    if item['path']==exe:item['expectedSha256']=parent['executableSha256']
path=OUT/'parent-definition.json';path.write_text(json.dumps(job,indent=2)+'\n')
saved=call('runner_tests.py','parent-saved','config-upload',job['id'],str(path),'--assets',app,'--build-file',exe);revision=saved.get('revision') or saved.get('Revision');assert revision
suite=job['id']+'-suite';call('runner_xiso.py','parent-suite','register',job['id'],'--revision',revision,'--id',suite,'--warmups','0','--multiplier','1','--completion','per_iteration')
attempts=[]
for leaf in ['code_stable','code_rewrite']:
    for position,variant in enumerate(['parent','disabled','disabled','parent']):
        isparent=variant=='parent';package=dict(application=app,executableSha256=parent['executableSha256']) if isparent else candidate['packages']['disabled']
        targetSuite=suite if isparent else json.loads((old/'disabled-suite.json').read_text())['id']
        cid=f'pc245-{host[:3]}-{leaf.removeprefix("code_")}-{position+1:02}'
        call('runner_xiso.py',cid+'-selected','select',package['application'],'--id',cid,'--suite',targetSuite,'--test','cpu_translation_blocks.'+leaf,'--mode','monolithic')
        plan=call('runner_xiso.py',cid+'-plan','plan',cid);assert plan['tests']==['cpu_translation_blocks.'+leaf]
        attempts.append(dict(campaign=cid,leaf='cpu_translation_blocks.'+leaf,mode=variant,position=position,phase='parent-ABBA',executableSha256=package['executableSha256'],sourceCommit=parent['sourceCommit'] if isparent else candidate['sourceCommit']))
(OUT/'schedule.json').write_text(json.dumps(dict(sourceCommit=candidate['sourceCommit'],parentCommit=parent['sourceCommit'],attempts=attempts),indent=2)+'\n')
print(host,'PARENT CONTROLS STAGED',flush=True)

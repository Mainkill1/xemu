import json,subprocess
from pathlib import Path
ROOT=Path('/home/codex/src/steamdeck-xemu');host='deck';URL='http://10.0.0.123:9368';OLD=ROOT/'evidence/research245-owner-probe-deck-20260930';OUT=ROOT/'evidence/research245-owner-probe-deck-clean-dormant';OUT.mkdir(exist_ok=True)
schedule=json.loads((OLD/'schedule.json').read_text());attempts=[]
for position,mode in enumerate(['disabled','off','off','disabled']):
 cid=f'd245-dec-0bd18bf2-stable-{position+1:02}';suite=json.loads((OLD/(mode+'-suite.json')).read_text())['id'];package=schedule['packages']['disabled' if mode=='disabled' else 'enabled']
 for label,args in [('selected',['select',package['application'],'--id',cid,'--suite',suite,'--test','cpu_translation_blocks.code_stable','--mode','monolithic']),('plan',['plan',cid])]:
  p=subprocess.run(['python3',str(ROOT/'test-runner/scripts/runner_xiso.py'),'--url',URL,*args],capture_output=True);(OUT/(cid+'-'+label+'.json')).write_bytes(p.stdout);assert p.returncode==0,p.stdout+p.stderr
 attempts.append(dict(campaign=cid,leaf='cpu_translation_blocks.code_stable',mode=mode,position=position,phase='dormant-ABBA',executableSha256=package['executableSha256']))
(OUT/'schedule.json').write_text(json.dumps(dict(schedule,attempts=attempts),indent=2)+'\n');print('DORMANT PAIR STAGED',flush=True)

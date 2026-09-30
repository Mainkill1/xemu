import json,subprocess,sys
from pathlib import Path
ROOT=Path('/home/codex/src/steamdeck-xemu');host=sys.argv[1];URL={'deck':'http://10.0.0.123:9368','windows':'http://10.0.7.1:9368'}[host]
OLD=ROOT/f'evidence/research245-owner-probe-{host}-20260930';OUT=ROOT/f'evidence/research245-owner-probe-{host}-clean-rewrite';OUT.mkdir(exist_ok=True)
schedule=json.loads((OLD/'schedule.json').read_text());attempts=[]
def call(label,*args):
 p=subprocess.run(['python3',str(ROOT/'test-runner/scripts/runner_xiso.py'),'--url',URL,*args],capture_output=True);(OUT/(label+'.json')).write_bytes(p.stdout)
 if p.stderr:(OUT/(label+'.stderr.log')).write_bytes(p.stderr)
 assert p.returncode==0,p.stdout.decode()+p.stderr.decode();return json.loads(p.stdout)
for row in schedule['attempts']:
 if not row['leaf'].endswith('code_rewrite'):continue
 cid=f'r245-{host[:3]}-0bd18bf2-rewrite-{row["position"]+1:02}'
 package=schedule['packages']['disabled' if row['mode']=='disabled' else 'enabled'];suite=json.loads((OLD/(row['mode']+'-suite.json')).read_text())['id']
 call(cid+'-selected','select',package['application'],'--id',cid,'--suite',suite,'--test',row['leaf'],'--mode','monolithic')
 plan=call(cid+'-plan','plan',cid);assert plan['tests']==[row['leaf']]
 attempts.append(dict(row,campaign=cid))
(OUT/'schedule.json').write_text(json.dumps(dict(schedule,attempts=attempts),indent=2)+'\n');print(host,'CLEAN REWRITE STAGED',flush=True)

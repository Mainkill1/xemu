import json,subprocess,sys,time,urllib.parse
from pathlib import Path
ROOT=Path('/home/codex/src/steamdeck-xemu');host=sys.argv[1];URL={'deck':'http://10.0.0.123:9368','windows':'http://10.0.7.1:9368'}[host]
last=ROOT/f'evidence/research245-owner-probe-{host}-parent-controls'/f'pc245-{host[:3]}-rewrite-04-attempts.json'
while len(sys.argv)<3:
 if last.exists():
  v=json.loads(last.read_text());items=v.get('items',[])
  if len(items)==1 and items[0].get('terminal') and items[0].get('execution')=='completed':break
 time.sleep(5)
def command(script,args,save):
 p=subprocess.run(['python3',str(ROOT/'test-runner/scripts'/script),'--url',URL,*args],capture_output=True);save.write_bytes(p.stdout)
 if p.stderr:save.with_suffix('.stderr.log').write_bytes(p.stderr)
 assert p.returncode==0,p.stdout.decode()[-1000:]+p.stderr.decode();return json.loads(p.stdout)
for kind in ([sys.argv[3]] if len(sys.argv)>3 else ['20260930','clean-rewrite','parent-controls','superseded-aa8bb9ab','parent-prelude']):
 folder=ROOT/f'evidence/research245-owner-probe-{host}-{kind}';schedule=json.loads((folder/'schedule.json').read_text())
 for row in schedule['attempts']:
  cid=row['campaign']
  if not (folder/(cid+'-started.json')).exists():continue
  attemptfile=folder/(cid+'-attempts.json')
  if not attemptfile.exists():command('runner_xiso.py',['attempts',cid],attemptfile)
  attempt=json.loads(attemptfile.read_text())['items'][0]
  if not attempt.get('runId'):
   print(host,cid,'NO EXECUTION',attempt.get('error'),flush=True);continue
  assert attempt['terminal'] and attempt['execution']=='completed'
  run=attempt['runId'];items=[];cursor=None;pages=0
  while True:
   route=f'/api/v1/runs/{run}/artifacts?limit=100'+('&cursor='+urllib.parse.quote(cursor,safe='') if cursor else '')
   page=command('runner_api.py',['request','GET',route],folder/f'{cid}-inventory-{pages:02}.json')
   assert page.get('complete') is True,page;items.extend(page['items']);pages+=1;cursor=page.get('nextCursor')
   if not cursor:break
   assert pages<100
  names=[i['path'] for i in items if Path(i['path']).suffix in ['.json','.jsonl','.log','.csv','.txt','.toml'] and 'state' not in Path(i['path']).parts]
  args=['collect',attempt['id'],str(folder/'canonical')]
  for name in names:args+=['--only',name]
  result=command('runner_api.py',args,folder/(cid+'-collection.json'));assert result['scope']=='selectedArtifacts'
  print(host,kind,cid,'COLLECTED',result['fileCount'],flush=True)
print(host,'ALL SELECTED TEXT EVIDENCE COLLECTED; binary/cache payloads excluded',flush=True)

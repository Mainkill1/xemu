import sys,json,subprocess,urllib.parse
from pathlib import Path
root=Path.cwd();p=Path(__file__).resolve().parent;sys.path.insert(0,str(root/'xemu-test-runner-upload-config/scripts'))
from runner_transport import RunnerApi,inside
host=sys.argv[1];version=sys.argv[2] if len(sys.argv)>2 else 'v1';api=RunnerApi({'deck':'http://10.0.0.42:9368','win':'http://10.0.7.1:9368'}[host]);client=root/'xemu-test-runner-upload-config/scripts/runner_xiso.py'
import time
while not (p/'stack-xiso-ready.json').exists():time.sleep(10)
scopes=json.loads((p/'stack-xiso-ready.json').read_text())['scopes']
for scope in scopes:assert json.loads((p/('stack-xiso-'+scope+'-pair.json')).read_text())['match']
for scope in scopes:
 identity=f'p312-{host}-vk-{scope}-{version}';route='/api/v1/xiso-campaigns/'+identity
 start_file=p/(identity+'-start.json')
 if not start_file.exists():
  s=api.json('/api/v1/status');assert not s.get('ProcessId') and not s['Queue']['Pending'] and not s['Queue']['Testing']
  r=api.json(route+'/start','POST',{});start_file.write_text(json.dumps(r,indent=2));print(identity,'started',flush=True)
 r=subprocess.run([sys.executable,str(client),'--url',api.url,'wait',identity],capture_output=True,text=True,check=True);result=json.loads(r.stdout);(p/(identity+'-finished.json')).write_text(json.dumps(result,indent=2));print(identity,result,flush=True)
 s=api.json('/api/v1/status');assert not s.get('ProcessId') and not s['Queue']['Testing']
 items=[];offset=0
 while True:
  a=api.json(route+'/attempts?limit=100&offset='+str(offset));items+=a['items'];offset=a.get('nextOffset')
  if offset is None:break
 (p/(identity+'-attempts.json')).write_text(json.dumps(items,indent=2))
 for a in items:
  if not a.get('runId'):continue
  rid=a['runId'];target=p/('stack-'+host+'-xiso-vk-evidence')/rid;target.mkdir(parents=True,exist_ok=True)
  if (target/'archive-complete.json').exists():continue
  inventory=[];cursor=None
  while True:
   pg=api.json('/api/v1/runs/'+rid+'/artifacts?limit=100'+('&cursor='+urllib.parse.quote(cursor,safe='') if cursor else ''));assert pg['complete'];inventory+=pg['items'];cursor=pg.get('nextCursor')
   if not cursor:break
  (target/'artifact-inventory.json').write_text(json.dumps(inventory,indent=2))
  for x in inventory:
   n=x['path']
   if n in ['assessment.json','input-manifest.json','job.json','launch.json','result.json','stderr.log','stdout.log','host-inventory.json','metrics.csv'] or n.startswith('guest/') or (n.startswith('diagnostics/run-state/') and n.endswith(('.toml','report.json'))):api.download(x['href'],inside(target,n),x['bytes'])
  (target/'archive-complete.json').write_text(json.dumps({'runId':rid,'items':len(inventory)}));print(identity,'archived',rid,flush=True)
 for fmt in ['json','csv','markdown']:
  r=subprocess.run([sys.executable,str(client),'--url',api.url,'report',identity,'--format',fmt,'--out',str(p/(identity+'-report.'+{'markdown':'md'}.get(fmt,fmt)))],capture_output=True,text=True,check=True);print(r.stdout.strip(),flush=True)

(p/('stack-'+host+'-xiso-finished-'+version+'.json')).write_text(json.dumps({'scopes':scopes,'completed':True}))

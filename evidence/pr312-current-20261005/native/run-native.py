import sys,json,subprocess,urllib.parse
from pathlib import Path
f=Path(__file__).resolve().parent;root=Path.cwd();sys.path.insert(0,str(root/'xemu-test-runner-upload-config/scripts'))
from runner_transport import RunnerApi,inside
host=sys.argv[1];api=RunnerApi({'deck':'http://10.0.0.42:9368','win':'http://10.0.7.1:9368'}[host]);client=root/'xemu-test-runner-upload-config/scripts/runner_tests.py'
apps=json.loads((f/'final-apps.json').read_text());procedures=json.loads((root/'.scratch/pr296-native'/f'{host}-selected-procedures.json').read_text());output=f/f'final-{host}-outcomes.json';rows=json.loads(output.read_text()) if output.exists() else []
def call(*args):
 p=subprocess.run([sys.executable,str(client),'--url',api.url,*args],text=True,capture_output=True)
 if p.returncode: raise RuntimeError(p.stderr or p.stdout)
 return json.loads(p.stdout)
def archive(result):
 rid=result.get('runId')
 if not rid:return
 target=f/f'final-{host}-evidence'/rid;target.mkdir(parents=True,exist_ok=True)
 if (target/'archive-complete.json').exists():return
 items=[];cursor=None
 while True:
  page=api.json('/api/v1/runs/'+rid+'/artifacts?limit=100'+('&cursor='+urllib.parse.quote(cursor,safe='') if cursor else ''));assert page['complete'];items+=page['items'];cursor=page.get('nextCursor')
  if not cursor:break
 (target/'artifact-inventory.json').write_text(json.dumps(items,indent=2))
 for item in items:
  path=item['path']
  if path in ['assessment.json','input-manifest.json','job.json','launch.json','result.json','stderr.log','host-inventory.json','metrics.csv','guest-frames.log','guest-flips.log','segments.jsonl'] or path.startswith('screenshots/') or (path.startswith('diagnostics/run-state/') and path.endswith(('.toml','report.json'))):api.download(item['href'],inside(target,path),item['bytes'])
 try:(target/'performance.json').write_text(json.dumps(api.json('/api/v1/runs/'+rid+'/performance?format=json'),indent=2))
 except Exception as ex:(target/'performance-error.txt').write_text(str(ex))
 (target/'archive-complete.json').write_text(json.dumps({'runId':rid,'items':len(items)}))
for title in ['morrowind','pgr2','conker']:
 assert all((f/f'final-{host}-{variant}-{title}-upload.json').exists() for variant in ['parent','candidate'])
for title in ['morrowind','conker','pgr2']:
 # All three use unchanged frozen inputs; PGR2 retains the disclosed cross-host start-phase mismatch.
 order=['parent','candidate','candidate','parent','candidate','parent','parent','candidate']
 for cell,variant in enumerate(order,1):
  old=next((r for r in rows if r['title']==title and r['cell']==cell),None)
  identity=f'p312-{host}-{title}-{cell:02d}-{variant}-v1';request=identity+'-t001';receipt=f/f'{identity}-select.json'
  if old:
   result=old['result']
   if (result.get('result') or {}).get('outcome', {}).get('execution') != 'completed':
    print('Previously retained failure; leave this title incomplete and continue other titles.', flush=True)
    break
  else:
   if receipt.exists():
    state=api.json('/api/v1/test-runs/'+request);assert state.get('state') not in ['selected','pending'],'Interrupted unstarted request; inspect before restarting'
   else:
    s=api.json('/api/v1/status');assert not s.get('ProcessId') and not s['Queue']['Pending'] and not s['Queue']['Testing'],'Rig busy: stop'
    a=apps[f'{host}-{variant}-{title}'];test=next(p for p in procedures if title in p['id']);
    if title=='conker':
     original=json.loads((root/'.scratch/priority-apu-merge'/f'{host}-conker-original-qualified.json').read_text());test=json.loads((root/'.scratch/priority-apu-merge/deck-conker-original-compatible.json').read_text()) if host=='deck' else {'id':original['definition']['id'],'revision':original['revision']}
    selected=call('select',a['id'],'--id',identity,'--tests',test['id']+'@'+test['revision']);assert selected['sha256']==a['sha256'];receipt.write_text(json.dumps(selected,indent=2));call('start',request);print(host,title,cell,variant,'started',flush=True)
   result=call('wait',request);rows.append({'title':title,'cell':cell,'variant':variant,'request':request,'result':result});output.write_text(json.dumps(rows,indent=2))
  archive(result);outcome=(result.get('result') or {}).get('outcome',{});print(host,title,cell,variant,outcome,flush=True)
  if outcome.get('execution')!='completed' or outcome.get('evidence')!='complete':
   print('Retained execution/evidence failure; stop this title. No benchmark modifications.',flush=True);break
print('Finished unchanged PR312 schedule; scene review and all failure records retained.',flush=True)

(f/f'final-{host}-native-finished.json').write_text(json.dumps({'host':host,'rows':len(rows),'allScheduledCellsRecorded':len(rows)==24,'completedLoop':True},indent=2))

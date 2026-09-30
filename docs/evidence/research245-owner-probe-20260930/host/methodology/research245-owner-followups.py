import json,subprocess,sys,time
from pathlib import Path
ROOT=Path('/home/codex/src/steamdeck-xemu');host=sys.argv[1]
last=ROOT/f'evidence/research245-owner-probe-{host}-20260930'/f'o245-{host[:3]}-0bd18bf2-rewrite-12-attempts.json'
while True:
 if last.exists():
  v=json.loads(last.read_text());items=v.get('items',[])
  if len(items)==1 and items[0].get('terminal') and items[0].get('execution')=='completed':break
 time.sleep(5)
for kind in ['clean-rewrite','parent-controls']:
 p=subprocess.run(['python3',str(ROOT/'scratch/research245-owner-run.py'),host,kind]);assert p.returncode==0
print(host,'ALL CLEAN FOLLOWUPS COMPLETE',flush=True)

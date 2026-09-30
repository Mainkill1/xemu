import json,subprocess,sys
from pathlib import Path
ROOT=Path('/home/codex/src/steamdeck-xemu');host=sys.argv[1]
URL={'deck':'http://10.0.0.123:9368','windows':'http://10.0.7.1:9368'}[host];OUT=ROOT/('evidence/research245-owner-probe-'+host+'-'+(sys.argv[2] if len(sys.argv)>2 else '20260930'))
def call(label,*args):
    p=subprocess.run(['python3',str(ROOT/'test-runner/scripts/runner_xiso.py'),'--url',URL,*args],capture_output=True)
    (OUT/(label+'.json')).write_bytes(p.stdout)
    if p.stderr:(OUT/(label+'.stderr.log')).write_bytes(p.stderr)
    assert p.returncode==0,p.stdout.decode()[-1500:]+p.stderr.decode()
    return json.loads(p.stdout)
for row in json.loads((OUT/'schedule.json').read_text())['attempts']:
    cid=row['campaign'];call(cid+'-started','start',cid);print(host,'START',cid,flush=True)
    done=call(cid+'-wait','wait',cid);assert done.get('terminal') is True
    attempts=call(cid+'-attempts','attempts',cid);assert not attempts.get('nextOffset') and len(attempts['items'])==1
    item=attempts['items'][0];print(host,'FINISH',cid,item.get('runId'),item.get('execution'),item.get('correctness'),item.get('evidence'),item.get('comparison'),flush=True)
    assert item.get('execution')=='completed' and item.get('correctness')=='passed' and item.get('evidence')=='complete',item
print(host,'ALL COMPLETE',flush=True)

import json
from pathlib import Path
import subprocess
import sys
ROOT=Path('/home/codex/src/steamdeck-xemu')
host=sys.argv[1]
url={'deck':'http://10.0.0.123:9368','windows':'http://10.0.7.1:9368'}[host]
out=ROOT / f'evidence/research245-light-probe-{host}-20260930'
for entry in json.loads((out/'schedule.json').read_text())['attempts']:
    cid=entry['campaign']
    attempt=json.loads((out/(cid+'-attempts.json')).read_text())['items'][0]
    p=subprocess.run(['python3',str(ROOT/'test-runner/scripts/runner_api.py'),'--url',url,'collect',attempt['id'],str(out/'canonical'),'--all'],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    (out/(cid+'-collection.json')).write_bytes(p.stdout)
    if p.stderr: (out/(cid+'-collection.stderr.log')).write_bytes(p.stderr)
    assert p.returncode==0, p.stdout.decode()+p.stderr.decode()
    print(cid, 'collected',flush=True)
print('ALL CANONICAL ARTIFACTS COLLECTED',flush=True)

import json
from pathlib import Path
import subprocess

ROOT = Path('/home/codex/src/steamdeck-xemu')
OUT = ROOT / 'evidence/research245-light-probe-windows-20260930'
CLIENT = ROOT / 'test-runner/scripts/runner_xiso.py'
schedule = json.loads((OUT / 'schedule.json').read_text())['attempts']

def call(label, *args):
    result = subprocess.run(['python3', str(CLIENT), '--url', 'http://10.0.7.1:9368', *args],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    (OUT / (label + '.json')).write_bytes(result.stdout)
    if result.stderr:
        (OUT / (label + '.stderr.log')).write_bytes(result.stderr)
    assert result.returncode == 0, result.stdout.decode()[-1500:] + result.stderr.decode()
    return json.loads(result.stdout)

for entry in schedule:
    cid = entry['campaign']
    call(cid + '-started', 'start', cid)
    print('START', cid, flush=True)
    observed = call(cid + '-wait', 'wait', cid)
    assert observed.get('terminal') is True, observed
    attempts = call(cid + '-attempts', 'attempts', cid)
    assert not attempts.get('nextOffset'), attempts
    items = attempts['items']
    assert len(items) == 1, attempts
    print('FINISH', cid, items[0].get('runId'), items[0].get('correctness'),
          items[0].get('evidence'), items[0].get('comparison'), flush=True)
    assert items[0].get('execution') == 'completed', items[0]
    assert items[0].get('correctness') == 'passed', items[0]
    assert items[0].get('evidence') == 'complete', items[0]
print('ALL 24 COMPLETE: compare eligibility remains a separate gate', flush=True)

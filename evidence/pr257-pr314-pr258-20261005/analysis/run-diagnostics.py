"""Run original-input diagnostic copies after the unchanged native schedule."""
import json
import subprocess
import sys
import time
import urllib.parse
from pathlib import Path

p = Path(__file__).resolve().parent
d = p / 'diagnostics'
root = Path.cwd()
sys.path.insert(0, str(root / 'xemu-test-runner-upload-config/scripts'))
from runner_transport import RunnerApi, inside
from runner_tests import upload_application

host = sys.argv[1]
api = RunnerApi({'deck': 'http://10.0.0.42:9368', 'win': 'http://10.0.7.1:9368'}[host])
client = root / 'xemu-test-runner-upload-config/scripts/runner_tests.py'
xiso_done = p / 'xiso' / ('stack-deck-xiso-finished-v2.json' if host == 'deck' else 'stack-win-xiso-finished.json')
while not xiso_done.exists():
    time.sleep(10)
state = api.json('/api/v1/status')
assert not state.get('ProcessId') and not state['Queue']['Pending'] and not state['Queue']['Testing']
setup = json.loads((d / 'prepared.json').read_text())[host]
app = json.loads((p / 'diagnostic-apps.json').read_text())[f'{host}-candidate-morrowind']
uploaded = upload_application(api, Path(app['directory']), 'xemu' if host == 'deck' else 'xemu.exe', app['id'], 'xemu.toml')
assert uploaded == app['sha256']
rows = json.loads((d / f'{host}-outcomes.json').read_text()) if (d / f'{host}-outcomes.json').exists() else []
for cell in setup['cells']:
    if any(row['cell'] == cell['cell'] for row in rows):
        continue
    identity = cell['testId']
    receipt = d / (identity + '-saved.json')
    if not receipt.exists():
        body = json.loads((d / cell['body']).read_text())
        saved = api.json('/api/v1/test-configs/' + identity, 'POST', body)
        receipt.write_text(json.dumps(saved, indent=2))
    saved = json.loads(receipt.read_text())
    revision = saved['revision']
    request = identity + '-t001'
    state = api.json('/api/v1/status')
    assert not state.get('ProcessId') and not state['Queue']['Pending'] and not state['Queue']['Testing']
    selected = api.json('/api/v1/test-runs', 'POST', {'id': request, 'applicationJobId': app['id'], 'testId': identity, 'revision': revision})
    (d / (identity + '-select.json')).write_text(json.dumps(selected, indent=2))
    api.json('/api/v1/test-runs/' + request + '/start', 'POST', {})
    print(host, 'diagnostic', cell['cell'], cell['policy'], 'started', flush=True)
    waited = subprocess.run([sys.executable, str(client), '--url', api.url, 'wait', request], capture_output=True, text=True, check=True)
    result = json.loads(waited.stdout)
    row = {**cell, 'revision': revision, 'request': request, 'result': result}
    rows.append(row)
    (d / f'{host}-outcomes.json').write_text(json.dumps(rows, indent=2))
    rid = result.get('runId')
    if not rid:
        raise RuntimeError('No diagnostic execution; retained result')
    target = d / (host + '-evidence') / rid
    target.mkdir(parents=True, exist_ok=True)
    items = []; cursor = None
    while True:
        page = api.json('/api/v1/runs/' + rid + '/artifacts?limit=100' + ('&cursor=' + urllib.parse.quote(cursor, safe='') if cursor else ''))
        assert page['complete']; items.extend(page['items']); cursor = page.get('nextCursor')
        if not cursor: break
    (target / 'artifact-inventory.json').write_text(json.dumps(items, indent=2))
    for item in items:
        name = item['path']
        if name in ['shortcut-evidence.json', 'assessment.json', 'input-manifest.json', 'job.json', 'launch.json', 'result.json', 'stderr.log', 'host-inventory.json', 'metrics.csv', 'guest-frames.log', 'guest-flips.log', 'segments.jsonl'] or name.startswith('screenshots/') or (name.startswith('diagnostics/run-state/') and name.endswith(('.toml', 'report.json'))):
            api.download(item['href'], inside(target, name), item['bytes'])
    (target / 'archive-complete.json').write_text(json.dumps({'runId': rid, 'items': len(items)}))
    evidence_file = target / 'shortcut-evidence.json'
    outcome = (result.get('result') or {}).get('outcome', {})
    ev = json.loads(evidence_file.read_text()) if evidence_file.exists() else {}
    print(host, 'diagnostic', cell['cell'], outcome, 'counterComplete', ev.get('complete'), 'eligibleStages', ev.get('counters', {}).get('vk.texture.clean_stage_eligible'), flush=True)
    if outcome.get('execution') != 'completed' or not evidence_file.exists() or not ev.get('complete'):
        raise RuntimeError('Retained diagnostic failure; inspect before further repeats')
(d / f'{host}-finished.json').write_text(json.dumps({'completed': True, 'cells': len(rows)}))

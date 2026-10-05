"""Resume only the seven unchanged chunks that did not execute."""
import json
import subprocess
import sys
import urllib.parse
from pathlib import Path

root = Path.cwd()
p = Path(__file__).resolve().parent
sys.path.insert(0, str(root / 'xemu-test-runner-upload-config/scripts'))
from runner_transport import RunnerApi, inside

api = RunnerApi('http://10.0.0.42:9368')
mapping = json.loads((p / 'deck-missing-work-mapping.json').read_text())
assert mapping['settingsIdentical'] and mapping['isoCatalogSuiteIdentical'] and mapping['planIdsIdentical']
identity = mapping['resumeCampaign']
route = '/api/v1/xiso-campaigns/' + identity
state = api.json('/api/v1/status')
assert not state.get('ProcessId') and not state['Queue']['Pending'] and not state['Queue']['Testing']
start = api.json(route + '/start', 'POST', {})
(p / (identity + '-start.json')).write_text(json.dumps(start, indent=2))
print(identity, 'started', flush=True)
client = root / 'xemu-test-runner-upload-config/scripts/runner_xiso.py'
waited = subprocess.run([sys.executable, str(client), '--url', api.url, 'wait', identity], capture_output=True, text=True, check=True)
result = json.loads(waited.stdout)
(p / (identity + '-finished.json')).write_text(json.dumps(result, indent=2))
print(identity, result, flush=True)
items = []
offset = 0
while True:
    page = api.json(route + '/attempts?limit=100&offset=' + str(offset))
    items.extend(page['items'])
    offset = page.get('nextOffset')
    if offset is None:
        break
(p / (identity + '-attempts.json')).write_text(json.dumps(items, indent=2))
for attempt in items:
    rid = attempt.get('runId')
    if not rid:
        continue
    target = p / 'stack-deck-xiso-vk-evidence' / rid
    target.mkdir(parents=True, exist_ok=True)
    inventory = []
    cursor = None
    while True:
        page = api.json('/api/v1/runs/' + rid + '/artifacts?limit=100' + ('&cursor=' + urllib.parse.quote(cursor, safe='') if cursor else ''))
        assert page['complete']
        inventory.extend(page['items'])
        cursor = page.get('nextCursor')
        if not cursor:
            break
    (target / 'artifact-inventory.json').write_text(json.dumps(inventory, indent=2))
    for item in inventory:
        name = item['path']
        if name in ['assessment.json', 'input-manifest.json', 'job.json', 'launch.json', 'result.json', 'stderr.log', 'stdout.log', 'host-inventory.json', 'metrics.csv'] or name.startswith('guest/') or (name.startswith('diagnostics/run-state/') and name.endswith(('.toml', 'report.json'))):
            api.download(item['href'], inside(target, name), item['bytes'])
    (target / 'archive-complete.json').write_text(json.dumps({'runId': rid, 'items': len(inventory)}))
    print(identity, 'archived', rid, flush=True)
for fmt in ['json', 'csv', 'markdown']:
    subprocess.run([sys.executable, str(client), '--url', api.url, 'report', identity, '--format', fmt, '--out', str(p / (identity + '-report.' + {'markdown': 'md'}.get(fmt, fmt)))], check=True)
assert len(items) == 7 and all(a.get('runId') for a in items)
(p / 'deck-missing-work-finished.json').write_text(json.dumps({'campaign': identity, 'attempts': len(items), 'terminal': result}, indent=2))

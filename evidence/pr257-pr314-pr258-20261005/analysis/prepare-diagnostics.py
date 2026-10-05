"""Prepare explicit diagnostic revisions; never edit approved benchmarks."""
import copy
import hashlib
import json
from pathlib import Path

p = Path(__file__).resolve().parent
d = p / 'diagnostics'
d.mkdir(exist_ok=True)
apps = json.loads((p / 'diagnostic-apps.json').read_text())
records = {}

def pin(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()

for host in ['deck', 'win']:
    original = json.loads((p / f'{host}-morrowind-original.json').read_text())
    definition = original['definition']
    job = definition['job']
    plan_hash = pin(job['plan'])
    template = {
        'schema': 'pr258-native-diagnostic-template/v1',
        'host': host, 'originalTestId': definition['id'],
        'originalRevision': original['revision'],
        'originalJob': job, 'originalBuildFiles': definition['buildFiles'],
        'planSha256': plan_hash,
        'candidateSource': apps[f'{host}-candidate-morrowind']['productSource'],
        'window': {'boundary': 'nv097_set_flip_stall_method', 'start': 100, 'count': 200},
        'diagnosticOnly': True,
        'changes': ['job identity', 'diagnostic tags', 'process-only policy override',
                    'evidence path and declared order/window metadata'],
    }
    template_hash = pin(template)
    (d / f'{host}-template.json').write_text(json.dumps(template, indent=2))
    records[host] = {'templateSha256': template_hash, 'planSha256': plan_hash,
                     'originalTestId': definition['id'], 'originalRevision': original['revision'], 'cells': []}
    for cell, side in enumerate('ABBABAAB', 1):
        identity = f'p258-{host}-counter-{cell:02d}-{side.lower()}-v1'
        modified = copy.deepcopy(job)
        modified['id'] = identity
        modified['tags'] = modified.get('tags', []) + ['pr258', 'diagnostic-only']
        policy = 'auto' if side == 'A' else 'disabled'
        extra = ['-xemu-tweak', 'vk_skip_clean_texture_stages=' + policy,
                 '-xemu-shortcut-evidence', '{resultDir}/shortcut-evidence.json',
                 '-xemu-shortcut-session', '{runId}',
                 '-xemu-shortcut-workload', 'native-morrowind-diagnostic:' + template_hash,
                 '-xemu-shortcut-input-sha256', plan_hash,
                 '-xemu-shortcut-order-group', 'p258-' + host + '-counter-v1',
                 '-xemu-shortcut-order', 'ABBA' if cell <= 4 else 'BAAB',
                 '-xemu-shortcut-position', str((cell - 1) % 4 + 1),
                 '-xemu-shortcut-start-frame', '100', '-xemu-shortcut-frames', '200']
        modified['arguments'] += extra
        assert modified['plan'] == job['plan']
        unchanged = copy.deepcopy(modified)
        unchanged['id'] = job['id']
        unchanged['tags'] = job.get('tags', [])
        unchanged['arguments'] = unchanged['arguments'][:-len(extra)]
        assert unchanged == job
        payload = {'sourceJobId': definition['sourceJobId'], 'job': modified,
                   'buildFiles': definition['buildFiles'],
                   'description': 'PR258 diagnostic only; original Morrowind guest inputs unchanged; added bounded owner counters and process policy metadata.'}
        (d / (identity + '-definition.json')).write_text(json.dumps(payload, indent=2))
        records[host]['cells'].append({'cell': cell, 'side': side, 'policy': policy,
                                      'testId': identity, 'body': identity + '-definition.json'})
(d / 'prepared.json').write_text(json.dumps(records, indent=2))
print('16 explicit diagnostic definitions prepared; original guest plans/arguments preserved except declared diagnostic additions.')

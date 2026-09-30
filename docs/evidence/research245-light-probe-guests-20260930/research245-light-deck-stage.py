import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path('/home/codex/src/steamdeck-xemu')
OUT = ROOT / 'evidence/research245-light-probe-deck-20260930'
OUT.mkdir(exist_ok=True)
URL = 'http://10.0.0.123:9368'
PACKAGE = ROOT / 'artifacts/research245-light-probe-deck-3a3d3c39'
APP = 'research245-light-probe-3a3d3c39-deck-v1'
SHA = hashlib.sha256((PACKAGE / 'xemu').read_bytes()).hexdigest()

def call(client, label, *args):
    cmd = ['python3', str(ROOT / 'test-runner/scripts' / client), '--url', URL, *args]
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    (OUT / (label + '.json')).write_bytes(result.stdout)
    if result.stderr:
        (OUT / (label + '.stderr.log')).write_bytes(result.stderr)
    assert result.returncode == 0, result.stderr.decode() + result.stdout.decode()[-1500:]
    print(label, 'ok', flush=True)
    return json.loads(result.stdout)

uploaded = call('runner_tests.py', 'application-upload', 'upload', str(PACKAGE), '--exe', 'xemu', '--id', APP)
base = json.loads((ROOT / 'evidence/research245-cpu-probe-20260930/deck-cpu-probe-definition-v2.json').read_text())
suites = {}
for mode in ['off', 'counters', 'occupancy', 'timing', 'all']:
    job = json.loads(json.dumps(base))
    job['id'] = f'light245-deck-{mode}-3a3d3c39'
    job['expectedExecutableSha256'] = SHA
    job['environment']['XEMU_TCG_JUMP_CACHE_PROBE'] = mode
    job['plan'] = []
    job['diagnostics'] = []
    job['workload']['evidenceRequirements'] = []
    job['experiment']['id'] = 'light245-same-executable-cpu-modes'
    job['experiment']['variant'] = mode
    job['experiment']['variedFactors'] = ['runtime-collector-mode']
    job['experiment']['controlledFactors'] = [
        'same-executable-' + SHA, 'fixture-c02a1a44', 'stable-50m-rewrite-1m',
        'warmups-0', 'multiplier-1', 'cpuid-both-leaves', '128mb-ram',
        'vulkan-radv', 'cold-private-application-cache',
        'mesa-driver-cache-unverified-no-waiver', 'no-HMP-queries',
    ]
    job['description'] = 'Query-free per-leaf CPU observer-mode measurements. Same executable and fixed inputs; no retention candidate or qualified production speedup.'
    for item in job['inputs']:
        if item['path'] == 'xemu':
            item['expectedSha256'] = SHA
    path = OUT / (mode + '-definition.json')
    path.write_text(json.dumps(job, indent=2) + '\n')
    saved = call('runner_tests.py', mode + '-saved', 'config-upload', job['id'], str(path), '--assets', 'xc-research245-cpu-probe-stable-v2-001', '--build-file', 'xemu')
    revision = saved.get('revision') or saved.get('Revision')
    assert revision, saved
    suite = job['id'] + '-suite'
    registered = call('runner_xiso.py', mode + '-suite', 'register', job['id'], '--revision', revision, '--id', suite, '--warmups', '0', '--multiplier', '1', '--completion', 'per_iteration')
    suites[mode] = suite

scheduled = []
order = ['off', 'off', 'off', 'counters', 'occupancy', 'timing', 'all', 'all', 'timing', 'occupancy', 'counters', 'off']
for leaf in ['code_stable', 'code_rewrite']:
    short = 'stable' if leaf == 'code_stable' else 'rewrite'
    for position, mode in enumerate(order):
        cid = f'light245-{short}-{mode}-{position + 1:02}'
        response = call('runner_xiso.py', cid + '-selected', 'select', APP, '--id', cid, '--suite', suites[mode], '--test', 'cpu_translation_blocks.' + leaf, '--mode', 'monolithic')
        plan = call('runner_xiso.py', cid + '-plan', 'plan', cid)
        assert plan['tests'] == ['cpu_translation_blocks.' + leaf], plan
        scheduled.append(dict(campaign=cid, leaf='cpu_translation_blocks.' + leaf,
                              mode=mode, position=position, phase='A/A' if position < 2 else 'balanced-mode-sweep'))
(OUT / 'schedule.json').write_text(json.dumps(dict(sourceCommit='3a3d3c390fbb5d3835206d47614a86467e7a9330', executableSha256=SHA, application=APP, settings='warmups0 multiplier1; fixed-work leaf; Vulkan; same dependencies and private seed; no monitor queries; driver qualification missing', attempts=scheduled), indent=2) + '\n')
print('STAGED', len(scheduled), 'campaigns; none started', flush=True)

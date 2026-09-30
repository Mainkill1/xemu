import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path('/home/codex/src/steamdeck-xemu')
OUT = ROOT / 'evidence/research245-light-probe-windows-20260930'
OUT.mkdir(exist_ok=True)
URL = 'http://10.0.7.1:9368'
PACKAGE = ROOT / 'artifacts/research245-light-probe-windows-3a3d3c39'
PACKAGE.mkdir(exist_ok=True)
(PACKAGE / 'state').mkdir(exist_ok=True)
DECK = ROOT / 'artifacts/research245-light-probe-deck-3a3d3c39'
for relative in ['xemu.toml', 'state/seed-eeprom.bin', 'cpu-code-rewrite-reference.json']:
    shutil.copy2(DECK / relative, PACKAGE / relative)
shutil.copy2(ROOT / 'worktrees/research245-jump-cache/build-windows-probe/qemu-system-i386.exe', PACKAGE / 'xemu.exe')
APP = 'research245-light-probe-3a3d3c39-windows-v1'
SHA = hashlib.sha256((PACKAGE / 'xemu.exe').read_bytes()).hexdigest()
assert SHA == 'fecf6c08a305465de4c2658909e15f94e103f613a7828c8e2c77df12f1a8d44d'

def call(client, label, *args):
    result = subprocess.run(['python3', str(ROOT / 'test-runner/scripts' / client), '--url', URL, *args], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    (OUT / (label + '.json')).write_bytes(result.stdout)
    if result.stderr:
        (OUT / (label + '.stderr.log')).write_bytes(result.stderr)
    assert result.returncode == 0, result.stderr.decode() + result.stdout.decode()[-2000:]
    print(label, 'ok', flush=True)
    return json.loads(result.stdout)

call('runner_tests.py', 'fatx-upload', 'disk-upload', 'research245-cpu-fatx-compat010-78af83a6', str(ROOT / 'artifacts/research245-cpu-fixtures-3f0eb995/fatx-v4-compat010.qcow2'), '--kind', 'xiso-seed', '--description', 'Issue245 fixed clean FATX CPU fixture seed; private per-run copy')
call('runner_tests.py', 'iso-upload', 'disk-upload', 'research245-cpu-iso-c02a1a44-74a10c40', str(ROOT / 'artifacts/research245-cpu-fixtures-c02a1a44/xemu-perf-tests_xiso.iso'), '--kind', 'readonly-input', '--description', 'Pinned issue245 fixed-work CPU fixture ISO c02a1a44')
call('runner_tests.py', 'application-upload', 'upload', str(PACKAGE), '--exe', 'xemu.exe', '--id', APP)
base = json.loads((ROOT / 'evidence/research245-cpu-probe-20260930/deck-cpu-probe-definition-v2.json').read_text())
suites = {}
for mode in ['off', 'counters', 'occupancy', 'timing', 'all']:
    job = json.loads(json.dumps(base))
    job['id'] = f'light245-windows-{mode}-3a3d3c39'
    job['executable'] = 'xemu.exe'
    job['targetOs'] = 'windows'
    job['tags'] = ['research245', 'cpu', 'xiso', 'windows', 'jump-cache-probe']
    job['expectedExecutableSha256'] = SHA
    job['environment'] = {'XEMU_FRAME_LOG': '{resultDir}/guest-frames.log', 'XEMU_FLIP_LOG': '{resultDir}/guest-flips.log', 'XEMU_TCG_JUMP_CACHE_PROBE': mode}
    job['requiredFiles'] = ['xemu.exe', 'xemu.toml', 'state/seed-eeprom.bin', 'cpu-code-rewrite-reference.json']
    job['inputs'] = [item for item in job['inputs'] if not item['path'].startswith('lib/')]
    for item in job['inputs']:
        if item['path'] == 'xemu':
            item['path'] = 'xemu.exe'
            item['expectedSha256'] = SHA
    isolation = job['runtimeState']['isolation']
    isolation['driverCache'] = 'uncontrolled'
    isolation['allowUncontrolledDriverCache'] = False
    isolation['readOnlyAssets'][0]['assetId'] = 'devbox-mcpx10-readonly-v1'
    isolation['readOnlyAssets'][1]['assetId'] = 'devbox-complex4627-readonly-v1'
    job['plan'] = []
    job['diagnostics'] = []
    job['workload']['evidenceRequirements'] = []
    job['experiment']['id'] = 'light245-windows-same-executable-cpu-modes'
    job['experiment']['variant'] = mode
    job['experiment']['variedFactors'] = ['runtime-collector-mode']
    job['experiment']['controlledFactors'] = ['same-executable-' + SHA, 'fixture-c02a1a44', 'stable-50m-rewrite-1m', 'warmups-0', 'multiplier-1', 'cpuid-both-leaves', '128mb-ram', 'vulkan', 'cold-private-application-cache', 'windows-driver-cache-uncontrolled-no-waiver', 'no-HMP-queries']
    job['description'] = 'Query-free per-leaf CPU observer-mode measurements. Same Windows executable and fixed inputs; no retention candidate or qualified production speedup.'
    path = OUT / (mode + '-definition.json')
    path.write_text(json.dumps(job, indent=2) + '\n')
    saved = call('runner_tests.py', mode + '-saved', 'config-upload', job['id'], str(path), '--assets', APP, '--build-file', 'xemu.exe')
    revision = saved.get('revision') or saved.get('Revision')
    assert revision, saved
    suite = job['id'] + '-suite'
    call('runner_xiso.py', mode + '-suite', 'register', job['id'], '--revision', revision, '--id', suite, '--warmups', '0', '--multiplier', '1', '--completion', 'per_iteration')
    suites[mode] = suite
scheduled = []
order = ['off', 'off', 'off', 'counters', 'occupancy', 'timing', 'all', 'all', 'timing', 'occupancy', 'counters', 'off']
for leaf in ['code_stable', 'code_rewrite']:
    short = 'stable' if leaf == 'code_stable' else 'rewrite'
    for position, mode in enumerate(order):
        cid = f'light245-win-{short}-{mode}-{position + 1:02}'
        call('runner_xiso.py', cid + '-selected', 'select', APP, '--id', cid, '--suite', suites[mode], '--test', 'cpu_translation_blocks.' + leaf, '--mode', 'monolithic')
        plan = call('runner_xiso.py', cid + '-plan', 'plan', cid)
        assert plan['tests'] == ['cpu_translation_blocks.' + leaf], plan
        scheduled.append(dict(campaign=cid, leaf='cpu_translation_blocks.' + leaf, mode=mode, position=position, phase='A/A' if position < 2 else 'balanced-mode-sweep'))
(OUT / 'schedule.json').write_text(json.dumps(dict(sourceCommit='3a3d3c390fbb5d3835206d47614a86467e7a9330', executableSha256=SHA, application=APP, settings='warmups0 multiplier1; fixed-work leaf; Vulkan; same private seed/firmware/reference; no HMP; Windows driver cache uncontrolled; no waiver', attempts=scheduled), indent=2) + '\n')
print('STAGED', len(scheduled), 'campaigns; none started', flush=True)

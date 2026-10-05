"""Freeze unchanged registered XISO work after the owned title schedule ends."""
import json
import subprocess
import sys
import time
from pathlib import Path

p = Path(__file__).resolve().parent
root = Path.cwd()
sys.path.insert(0, str(root / 'xemu-test-runner-upload-config/scripts'))
from runner_transport import RunnerApi, ClientError
from runner_tests import upload_application

while not (p.parent / 'compute-fixture-completed.json').exists() or not all((p.parent / f'final-{h}-native-finished.json').exists()
              for h in ['deck', 'win']):
    time.sleep(10)

apps = json.loads((p / 'stack-xiso-apps.json').read_text())
urls = {'deck': 'http://10.0.0.42:9368', 'win': 'http://10.0.7.1:9368'}
full = {}
for host, url in urls.items():
    api = RunnerApi(url)
    state = api.json('/api/v1/status')
    assert not state.get('ProcessId') and not state['Queue']['Pending'] and not state['Queue']['Testing']
    for variant in ['parent', 'candidate']:
        app = apps[host + '-' + variant]
        receipt = upload_application(api, Path(app['directory']),
                                     'xemu' if host == 'deck' else 'xemu.exe',
                                     app['id'], 'xemu.toml')
        assert receipt == app['sha256']
    suite = 'pr256-' + ('deck' if host == 'deck' else 'windows') + '-opengl-v5'
    identity = f'p312-{host}-vk-full-paired-v1'
    body = dict(id=identity, application=app['id'], suite=suite,
                configurationSource='application',
                referenceApplication=apps[host + '-parent']['id'])
    try:
        reply = api.json('/api/v1/xiso-campaigns', 'POST', body)
        (p / (identity + '-freeze.json')).write_text(json.dumps(reply, indent=2))
        plan = api.json('/api/v1/xiso-campaigns/' + identity + '?view=plan')
        (p / (identity + '-plan.json')).write_text(json.dumps(plan, indent=2))
        full[host] = True
        print(host, 'full paired plan accepted', len(plan['tests']), len(plan['attempts']), flush=True)
    except ClientError as error:
        (p / (identity + '-rejected.json')).write_text(json.dumps(error.document(), indent=2))
        full[host] = False
        print(host, 'full paired preflight rejected:', error.code, str(error), flush=True)

scopes = ['full-paired'] if all(full.values()) else ['texture-focused', 'full-parent', 'full-candidate']
if scopes != ['full-paired']:
    for host, url in urls.items():
        api = RunnerApi(url)
        suite = 'pr256-' + ('deck' if host == 'deck' else 'windows') + '-opengl-v5'
        for scope in scopes:
            identity = f'p312-{host}-vk-{scope}-v1'
            variant = 'parent' if scope == 'full-parent' else 'candidate'
            body = dict(id=identity, application=apps[host + '-' + variant]['id'],
                        suite=suite, configurationSource='application')
            if scope == 'texture-focused':
                body.update(referenceApplication=apps[host + '-parent']['id'],
                            tests=['cpu_floating_point.x87_scalar', 'pipeline_texture_switch.texture_switch', 'pipeline_texture_switch.sampler_only_identity', 'pipeline_texture_switch.clear_texture_normal', 'game_load.cross_title_hotpath.texture_binding_reuse'])
            else:
                body['mode'] = 'full'
            reply = api.json('/api/v1/xiso-campaigns', 'POST', body)
            (p / (identity + '-freeze.json')).write_text(json.dumps(reply, indent=2))
            plan = api.json('/api/v1/xiso-campaigns/' + identity + '?view=plan')
            (p / (identity + '-plan.json')).write_text(json.dumps(plan, indent=2))
            print(host, scope, 'leaves', len(plan['tests']), 'attempts', len(plan['attempts']), flush=True)

for scope in scopes:
    left = f'p312-deck-vk-{scope}-v1'
    right = f'p312-win-vk-{scope}-v1'
    result = subprocess.run([sys.executable,
                            str(root / 'xemu-test-runner-upload-config/scripts/runner_xiso.py'),
                            '--url', urls['deck'], 'check-pair', left,
                            '--other-url', urls['win'], '--other-id', right],
                            capture_output=True, text=True)
    (p / ('stack-xiso-' + scope + '-pair.json')).write_text(result.stdout)
    assert result.returncode == 0, result.stderr or result.stdout
    assert json.loads(result.stdout)['match']
(p / 'stack-xiso-ready.json').write_text(json.dumps({'scopes': scopes, 'fullPairedAccepted': full}, indent=2))
print('Both hosts froze identical XISO work and schedules; ready for explicit owned execution.', flush=True)

"""Prepare eight immutable native-title drafts via maintained HTTP clients.

This does not submit or launch work. ABBA then BAAB is frozen before any
attempt executes. Submit one prepared ID through runner_api.py submit-draft only after
the previous owned target has exited; preserve failed attempts, with no reruns.
This is the native-title workflow, separate from server-owned XISO campaigns.
"""
from pathlib import Path
import hashlib
import json
import sys

ROOT = Path('/home/codex/src/steamdeck-xemu')
sys.path.insert(0, str(ROOT / 'worktrees/runner-plan-timeout/scripts'))
from runner_transport import RunnerApi, declaration, job_url
from runner_workflows import Workflows

TEST = 'issue229-deck-pgr2-stationary-full-c-v3'
REVISION = '8f0c913a98217a306760fd5a87ca1104e40784cf913d5beeb0013bdaedb72d12'
PARENT = 'ee5ce48b48784f999af374c1452003f8b2b1230f'
HASHES = {
    'parent': '4a6e0dae0a83a870aebde726ca40687f35c9293f3da1147a6d7d14e8c4e8e01a',
    'candidate': '84ac46913180a55b2fce18cf304fd820d3ff21a828477f7b73c0136401e17418',
}
OUTPUT = ROOT / 'artifacts/issue229-dsp-multiply/native-balanced-v3'
OUTPUT.mkdir(exist_ok=True)
api = RunnerApi('http://10.0.0.123:9368')
workflow = Workflows(api)
state = api.json('/api/v1/status')
assert state['Phase'] == 'idle' and state['CurrentJob'] is None
assert state['Queue']['Pending'] == state['Queue']['Testing'] == 0
(OUTPUT / 'before.json').write_text(json.dumps(state, indent=2) + '\n')
definition = api.json('/api/v1/tests/' + TEST + '/' + REVISION)
assert definition['buildFiles'] == ['xemu']
attempts = []
for order, letters in [('abba', 'ABBA'), ('baab', 'BAAB')]:
    for index, letter in enumerate(letters, 1):
        variant = 'parent' if letter == 'A' else 'candidate'
        build = ROOT / 'artifacts/issue229-dsp-multiply/xiso-preparation' / ('issue229-' + variant + '-native-linux-v1')
        files = [declaration(build, 'xemu')]
        assert files[0]['Sha256'] == HASHES[variant]
        identifier = f'issue229-deck-native-c-v3-{order}-{index}-20261001'
        request = dict(id=identifier, testId=TEST, revision=REVISION, files=files,
                       experimentId='issue229-deck-native-c-v3-balanced',
                       variant=variant, reference=PARENT)
        attempts.append(dict(id=identifier, order=order, position=index, physicalVariant=letter,
                             variant=variant, executableSha256=HASHES[variant], request=request))
plan = dict(testId=TEST, revision=REVISION, parent=PARENT,
            candidateCode='1e6cebe0cb6452f875329710fa19795f8644ad2d',
            physicalOrder='ABBA BAAB', attempts=attempts,
            failurePolicy='Keep every attempt; no automatic or intentional reruns.',
            scope='Cold private launch and 60 s scene warmup; 300 s stationary observation. Power, OS page cache and driver RAM uncontrolled; PCM absent. Not fully warm-cache qualification.')
plan_bytes = (json.dumps(plan, indent=2) + '\n').encode()
plan_file = OUTPUT / 'plan.json'
if plan_file.exists():
    assert plan_file.read_bytes() == plan_bytes, 'Conflicting native batch plan'
else:
    plan_file.write_bytes(plan_bytes)
print('Frozen native plan SHA-256:', hashlib.sha256(plan_bytes).hexdigest(), flush=True)
for attempt in attempts:
    identifier = attempt['id']
    api.json('/api/v1/jobs/from-test', 'POST', attempt['request'])
    workflow.operation(identifier)
    assert workflow.status(identifier)['state'] == 'draft'
    build = ROOT / 'artifacts/issue229-dsp-multiply/xiso-preparation' / ('issue229-' + attempt['variant'] + '-native-linux-v1')
    workflow.upload_files(identifier, build, attempt['request']['files'], 1024 * 1024)
    detail = api.json(job_url(identifier))
    (OUTPUT / (identifier + '-draft.json')).write_text(json.dumps(detail, indent=2) + '\n')
    print('Prepared', identifier, attempt['physicalVariant'], 'without submission', flush=True)

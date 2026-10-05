"""Run the scoped adapter check after the owned Deck benchmark finishes."""
import json
from pathlib import Path
import subprocess
import sys
import time

root = Path.cwd()
p = Path(__file__).resolve().parent
sys.path.insert(0, str(root / 'xemu-test-runner-upload-config/scripts'))
from runner_transport import RunnerApi

api = RunnerApi('http://10.0.0.42:9368')
while not (p / 'final-deck-native-finished.json').exists():
    time.sleep(10)
status = api.json('/api/v1/status')
assert not status.get('ProcessId') and not status['Queue']['Pending'] and not status['Queue']['Testing']
socket = root / '.scratch/deck-landing.sock'
remote = '/home/deck/xemu-performance/pr312-compute-qualification'
options = ['-o', 'ControlPath=' + str(socket), '-o', 'BatchMode=yes']
target = 'deck@10.0.0.42'

def operator(command):
    return subprocess.run(['ssh', *options, target, command],
                          text=True, capture_output=True, timeout=180)

created = operator('mkdir -p ' + remote)
assert created.returncode == 0, created.stderr
try:
    files = [p / 'compute-deck-private/native-compute',
             p / 'compute-deck-private/libshaderc.so.1']
    transfer = subprocess.run(['scp', *options, *map(str, files), target + ':' + remote + '/'],
                              text=True, capture_output=True, timeout=180)
    assert transfer.returncode == 0, transfer.stderr
    command = 'env LD_LIBRARY_PATH=' + remote + ' timeout 120 ' + remote + '/native-compute'
    result = operator(command)
    (p / 'deck-compute-output.log').write_text(result.stdout + result.stderr)
    record = {'exitCode': result.returncode, 'command': command,
              'source': 'e428a42bc3e0f2f60caccc9b738bde47e7a2f800',
              'fixture': 'actual surface-compute.c adapter; shaderc compiler shim',
              'passCount': result.stdout.count('PASS format='),
              'output': result.stdout, 'stderr': result.stderr}
    (p / 'deck-compute-result.json').write_text(json.dumps(record, indent=2) + '\n')
    print(result.stdout, result.stderr, flush=True)
    assert result.returncode == 0 and record['passCount'] == 32, record
finally:
    cleanup = operator('rm -f ' + remote + '/native-compute ' + remote + '/libshaderc.so.1')
    (p / 'deck-compute-cleanup.json').write_text(json.dumps(
        {'exitCode': cleanup.returncode, 'stdout': cleanup.stdout, 'stderr': cleanup.stderr}) + '\n')
assert cleanup.returncode == 0
(p / 'compute-fixture-completed.json').write_text(json.dumps(record, indent=2) + '\n')
print('Physical Deck adapter check complete; temporary files removed.', flush=True)

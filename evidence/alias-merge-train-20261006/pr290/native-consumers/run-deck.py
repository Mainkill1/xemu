"""Existing PR312 scoped operator route, adapted only to this harness/artifacts."""
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import sys

root = Path.cwd()
out = root / '.scratch/alias-train-20261006/pr290/native-consumers'
sys.path.insert(0, str(root / 'xemu-test-runner-upload-config/scripts'))
from runner_transport import RunnerApi
api = RunnerApi('http://10.0.0.42:9368')
remote = '/home/deck/xemu-performance/pr290-consumer-qualification'
options = ['-o', 'ControlPath=' + str(root / '.scratch/pr283-bench-deck.sock'), '-o', 'BatchMode=yes']
target = 'deck@10.0.0.42'
def idle(name):
    status = api.json('/api/v1/status')
    (out / name).write_text(json.dumps(status, indent=2) + '\n')
    assert not status.get('ProcessId') and not status['Queue']['Pending'] and not status['Queue']['Testing']
def operator(command):
    return subprocess.run(['ssh', *options, target, command], text=True,
                          capture_output=True, timeout=75)
idle('deck-native-before.json')
created = operator('mkdir -p ' + remote)
assert created.returncode == 0, created.stderr
files = [out / 'native', Path('/lib/x86_64-linux-gnu/libshaderc.so.1')]
try:
    transfer = subprocess.run(['scp', *options, *map(str, files), target + ':' + remote + '/'],
                              text=True, capture_output=True, timeout=75)
    assert transfer.returncode == 0, transfer.stderr
    expected = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
    hashes = operator('sha256sum ' + remote + '/native ' + remote + '/libshaderc.so.1')
    assert hashes.returncode == 0, hashes.stderr
    for line in hashes.stdout.splitlines():
        sha, path = line.split()
        assert expected[Path(path).name] == sha
    inventory = operator('vulkaninfo --summary')
    (out / 'deck-vulkaninfo.log').write_text(inventory.stdout + inventory.stderr)
    idle('deck-native-launch-status.json')
    command = 'ulimit -c 0; env LD_LIBRARY_PATH=' + remote + ' XEMU_NATIVE_SHADER_DIR=' + remote + '/shaders timeout 60 ' + remote + '/native'
    result = operator(command)
    (out / 'deck-native.log').write_text(result.stdout + result.stderr)
    record = dict(exitCode=result.returncode, command=command,
                  source='02ebd503171dff15d4f17d27151be3c4fd5c79ed production GPU readback, retention lookup, invalidation and flush; coherent VMA/dirty-page adapters',
                  hashes=expected, hashesVerifiedOnTarget=True,
                  oraclePassCount=result.stdout.count('PASS retained format='),
                  
                  unsupportedD24Cases=result.stdout.count('SKIP format=129 unsupported'),
                  output=result.stdout, stderr=result.stderr,
                  method='Previously used PR312 scoped SSH operator native qualification; no runner jobs or definition changes')
    (out / 'deck-native-result.json').write_text(json.dumps(record, indent=2) + '\n')
    print(result.stdout, result.stderr, flush=True)
    assert result.returncode == 0 and record['oraclePassCount'] == 10 and result.stdout.count('PASS runtime CPU-read/') == 4 and result.stdout.count('PASS native retirement') == 1 and result.stdout.count('PASS native texture-consumer')==16 and result.stdout.count('PASS native blit-consumer')==3 and 'errors=0' in result.stdout
finally:
    cleanup = operator('rm -f ' + remote + '/native ' + remote + '/libshaderc.so.1; rm -rf ' + remote + '/shaders')
    (out / 'deck-native-cleanup.json').write_text(json.dumps(dict(exitCode=cleanup.returncode, stdout=cleanup.stdout, stderr=cleanup.stderr), indent=2) + '\n')
    assert cleanup.returncode == 0
idle('deck-native-after.json')

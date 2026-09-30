import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex
import statistics
import subprocess

ROOT = Path('/home/codex/src/steamdeck-xemu')
WT = ROOT / 'worktrees/research245-jump-cache'
ART = ROOT / 'artifacts'
OUT = ART / 'research245-light-probe-host'
OUT.mkdir(exist_ok=True)
REF = '06168a2f455b832bc6eb7936ebe725ec530b1b00'
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
entries = json.loads((WT / 'build-probe/compile_commands.json').read_text())
entry = next(e for e in entries if e['file'].endswith('tests/unit/test-tcg-jump-cache-probe.c'))
args = shlex.split(entry['command'])
flags = []
i = 0
while i < len(args):
    a = args[i]
    if a in ('-o', '-MQ', '-MF'):
        i += 2
        continue
    if a in ('-c', '-MD') or a == entry['file']:
        i += 1
        continue
    flags.append(a)
    i += 1
old = OUT / 'reference'
old.mkdir(exist_ok=True)
for name in ['jump-cache-probe.c', 'jump-cache-probe.h']:
    (old / name).write_bytes(subprocess.check_output(['git', 'show', f'{REF}:accel/tcg/{name}'], cwd=WT))
commands = []
for label, directory in [('reference', old), ('candidate', WT / 'accel/tcg')]:
    exe = OUT / label / 'bench' if label == 'reference' else OUT / 'candidate-bench'
    cmd = flags + ['-I' + str(directory)]
    if label == 'reference':
        cmd += ['-DLEGACY_PROBE']
    cmd += [str(ART / 'research245-light-probe-bench.c'), str(directory / 'jump-cache-probe.c'), str(WT / 'util/qemu-timer-common.c'), '-o', str(exe), '-lglib-2.0']
    result = subprocess.run(cmd, cwd=entry['directory'], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (OUT / (label + '-compile.log')).write_bytes(result.stdout)
    assert result.returncode == 0, result.stdout
    assert b'warning:' not in result.stdout and b'error:' not in result.stdout
    commands.append(dict(label=label, argv=cmd, cwd=entry['directory'], exitCode=result.returncode, executableSha256=sha(exe)))

def execute(label, mode, lookups, clears):
    exe = old / 'bench' if label == 'reference' else OUT / 'candidate-bench'
    cmd = [str(exe), mode, str(lookups), str(clears)]
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    assert result.returncode == 0, result.stderr
    return dict(json.loads(result.stdout), build=label, argv=cmd, exitCode=0)

affinity = sorted(os.sched_getaffinity(0))
core = affinity[-1]
os.sched_setaffinity(0, {core})
started = datetime.datetime.now(datetime.timezone.utc).isoformat()
records = []
comparisons = [
    ('legacy-all-vs-lighter-all', ('reference', 'all'), ('candidate', 'all')),
    ('legacy-off-vs-current-off', ('reference', 'off'), ('candidate', 'off')),
    ('same-executable-off-vs-counters', ('candidate', 'off'), ('candidate', 'counters')),
    ('same-executable-counters-vs-occupancy', ('candidate', 'counters'), ('candidate', 'occupancy')),
    ('same-executable-counters-vs-timing', ('candidate', 'counters'), ('candidate', 'timing')),
]
summaries = []
for workload, lookups, clears in [('lookup-hooks', 5000000, 0), ('empty-cache-clears', 0, 50000)]:
    for comparison, a, b in comparisons:
        for setting in [a, b]:
            records.append(dict(execute(*setting, lookups, clears), workload=workload, comparison=comparison, warmup=True))
        rows = []
        for idx, side in enumerate('ABBABAAB'):
            setting = a if side == 'A' else b
            record = dict(execute(*setting, lookups, clears), workload=workload, comparison=comparison, warmup=False, side=side, position=idx)
            rows.append(record)
            records.append(record)
        medians = {side: statistics.median(r['cpu_ns'] for r in rows if r['side'] == side) for side in ['A', 'B']}
        ranges = {side: [min(r['cpu_ns'] for r in rows if r['side'] == side), max(r['cpu_ns'] for r in rows if r['side'] == side)] for side in ['A', 'B']}
        summaries.append(dict(workload=workload, comparison=comparison, reference=a, candidate=b, repetitionsPerSetting=4, statistic='median process CPU ns', median=medians, range=ranges, absoluteDifferenceNs=medians['B']-medians['A'], improvementPercent=100*(medians['A']-medians['B'])/medians['A'], meaning='Collector host microbenchmark only; no emulator/guest speedup or retention proof'))
        print(workload, comparison, medians, flush=True)
source = {str(p): sha(p) for p in [ART / 'research245-light-probe-bench.c', old / 'jump-cache-probe.c', old / 'jump-cache-probe.h', WT / 'accel/tcg/jump-cache-probe.c', WT / 'accel/tcg/jump-cache-probe.h']}
(OUT / 'runs.json').write_text(json.dumps(records, indent=2) + '\n')
(OUT / 'summary.json').write_text(json.dumps(summaries, indent=2) + '\n')
(OUT / 'receipt.json').write_text(json.dumps(dict(startedUtc=started, host=platform.platform(), referenceCommit=REF, candidateParent='17334c1b1a93c3c4606c044e2af9768187ad03af', candidateSourceSha256=source, commands=commands, inheritedAllowedCpus=affinity, pinnedCpu=core, scope='Host-only collector cost; no driver-cache or emulation acceptance result', order='ABBA BAAB; one excluded warmup per setting per workload/comparison; 4 retained observations/setting', fixedWork={'lookup-hooks':5000000,'empty-cache-clears':50000,'slotsPerClear':4096,'occupancyState':'all slots empty; every clear still writes every slot'}), indent=2) + '\n')

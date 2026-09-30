import datetime, hashlib, json, os, platform, random, shlex, statistics, subprocess
from pathlib import Path
ROOT = Path('/home/codex/src/steamdeck-xemu')
WT = ROOT / 'worktrees/research245-jump-cache'
OUT = ROOT / 'artifacts/research245-owner-probe'
entries = json.loads((WT / 'build-probe/compile_commands.json').read_text())
entry = next(e for e in entries if e['file'].endswith('tests/unit/test-tcg-jump-cache-validity.c'))
args = shlex.split(entry['command'])
flags = []; i = 0
while i < len(args):
    if args[i] in ('-o', '-MQ', '-MF'): i += 2; continue
    if args[i] in ('-c', '-MD') or args[i] == entry['file']: i += 1; continue
    flags.append(args[i]); i += 1
commands = []
for stage in range(8):
    directory = OUT / 'reference' if stage in (6, 7) else WT / 'accel/tcg'
    exe = OUT / f'ablation-{stage}'
    cmd = flags + [f'-DSTAGE={stage}', '-I' + str(directory), str(OUT / 'ablation.c')]
    if stage: cmd += [str(directory / 'jump-cache-probe.c')]
    cmd += [str(WT / 'util/qemu-timer-common.c'), '-o', str(exe), '-lglib-2.0']
    run = subprocess.run(cmd, cwd=entry['directory'], capture_output=True)
    (OUT / f'ablation-{stage}-build.log').write_bytes(run.stdout + run.stderr)
    assert run.returncode == 0, run.stderr.decode()
    assert b'warning:' not in run.stderr, run.stderr.decode()
    commands.append(dict(stage=stage, argv=cmd, cwd=entry['directory'], executableSha256=hashlib.sha256(exe.read_bytes()).hexdigest()))
allowed = sorted(os.sched_getaffinity(0)); core = allowed[-1]; os.sched_setaffinity(0, {core})
seed = 245272; rng = random.Random(seed)
records = []; summaries = []
def run(stage, work):
    p = subprocess.run([str(OUT / f'ablation-{stage}'), '5000000', str(work)], capture_output=True)
    assert p.returncode == 0, p.stderr.decode()
    return json.loads(p.stdout)
def quantile(values, p):
    v = sorted(values); x = (len(v)-1)*p; low = int(x)
    return v[low] + (v[min(low+1,len(v)-1)]-v[low])*(x-low)
for work in (0, 16):
    for a,b in [(0,1),(1,2),(2,3),(3,4),(4,5),(6,4),(7,1),(0,4)]:
        comparison=f'{a}-vs-{b}'
        for stage in (a,b): records.append(dict(run(stage,work), comparison=comparison, warmup=True))
        pairs=[]
        for pair in range(30):
            order=[a,b] if rng.randrange(2)==0 else [b,a]
            values={}
            for position,stage in enumerate(order):
                row=dict(run(stage,work),comparison=comparison,pair=pair,position=position,warmup=False)
                records.append(row); values[stage]=row
            assert values[a]['checksum']==values[b]['checksum']
            pairs.append(100*(values[a]['cpu_ns']-values[b]['cpu_ns'])/values[a]['cpu_ns'])
        rows=[r for r in records if r['comparison']==comparison and r['work']==work and not r['warmup']]
        med={s:statistics.median(r['cpu_ns'] for r in rows if r['stage']==s) for s in (a,b)}
        bootstrap=[]
        brng=random.Random(seed+work+a*10+b)
        for _ in range(10000): bootstrap.append(statistics.median(brng.choices(pairs,k=len(pairs))))
        result=dict(work=work,comparison=comparison,baselineStage=a,candidateStage=b,pairs=30,medianCpuNs=med,absoluteDeltaNs=med[b]-med[a],improvementPercent=100*(med[a]-med[b])/med[a],pairedMedianImprovementPercent=statistics.median(pairs),pairedBootstrapMedianCI95=[quantile(bootstrap,.025),quantile(bootstrap,.975)],p95SlowdownPercent=quantile([-p for p in pairs],.95),worstSlowdownPercent=max(-p for p in pairs))
        summaries.append(result); print(work,comparison,result['improvementPercent'],flush=True)
        (OUT/'ablation-runs.json').write_text(json.dumps(records,indent=2)+'\n')
        (OUT/'ablation-summary.json').write_text(json.dumps(summaries,indent=2)+'\n')
pmu=subprocess.run(['perf','stat','-x,','-e','cycles,instructions,branches,branch-misses,L1-dcache-load-misses,LLC-load-misses',str(OUT/'ablation-4'),'5000000','0'],capture_output=True)
(OUT/'pmu.log').write_bytes(pmu.stdout+pmu.stderr)
policies={}
for p in Path(f'/sys/devices/system/cpu/cpu{core}/cpufreq').glob('*'):
    if p.name in ('scaling_governor','scaling_driver','scaling_min_freq','scaling_max_freq','energy_performance_preference'):
        try: policies[str(p)]=p.read_text().strip()
        except OSError as e: policies[str(p)]=str(e)
source={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [OUT/'ablation.c',OUT/'reference/jump-cache-probe.c',OUT/'reference/jump-cache-probe.h',WT/'accel/tcg/jump-cache-probe.c',WT/'accel/tcg/jump-cache-probe.h',WT/'accel/tcg/jump-cache-probe-lookup.h',WT/'accel/tcg/tb-jmp-cache.h',WT/'util/qemu-timer-common.c']}
(OUT/'ablation-receipt.json').write_text(json.dumps(dict(utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),host=platform.platform(),commands=commands,sourceSha256=source,allowedCpus=allowed,pinnedCpu=core,policies=policies,seed=seed,pmuExitCode=pmu.returncode,stages={0:'compile-disabled',1:'enabled runtime OFF',2:'owner sequence only (ablation)',3:'owner sequence + classified call count without publication (ablation)',4:'candidate counters incl periodic publication',5:'candidate timing incl periodic publication',6:'f105 counters incl per-lookup publication/out-of-line helpers',7:'f105 runtime OFF'},scope='Actual cache-hit validation helper, not full emulator; work0 tight hit and work16 adds sixteen dependent integer rounds. 5M iterations/sample; 30 randomized AB/BA pairs/comparison; excluded warmups retained; process CPU ns; final publication outside timed region.'),indent=2)+'\n')

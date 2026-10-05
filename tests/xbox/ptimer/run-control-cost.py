#!/usr/bin/env python3
"""Measure fixed-main control cost; this is not a runtime optimization claim."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import statistics
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cpu', type=int)
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists() and any(output.iterdir()):
        parser.error('output is not empty; use a new directory to preserve prior observations')
    output.mkdir(parents=True, exist_ok=True)
    allowed = sorted(os.sched_getaffinity(0))
    cpu = args.cpu if args.cpu is not None else allowed[-1]
    assert cpu in allowed
    os.sched_setaffinity(0, {cpu})
    binary = args.build_dir.resolve() / 'tests/xbox/ptimer/benchmark-pgraph-control-cost'
    env = os.environ.copy()
    for key in ('XEMU_FLIP_LOG', 'XEMU_FRAME_LOG'):
        env.pop(key, None)
    argv = [str(binary), '--tap']
    with (output / 'raw.log').open('wb') as stream:
        run = subprocess.run(argv, env=env, stdout=stream, stderr=subprocess.STDOUT, timeout=60)
    assert run.returncode == 0
    pattern = r'cost pair=(\d+) position=(\d+) mmio=(\d+) iterations=(\d+) cpu_ns=(\d+)'
    rows = [dict(zip(('pair', 'position', 'mmio', 'iterations', 'cpuNs'), map(int, m)))
            for m in re.findall(pattern, (output / 'raw.log').read_text())]
    assert len(rows) == 60
    deltas = []
    for pair in range(30):
        selected = [r for r in rows if r['pair'] == pair]
        assert len(selected) == 2 and {r['mmio'] for r in selected} == {0, 1}
        a, b = sorted(selected, key=lambda r: r['mmio'])
        assert a['iterations'] == b['iterations'] == 100000
        deltas.append((b['cpuNs'] - a['cpuNs']) / a['iterations'])
    medians = {str(mode): statistics.median(r['cpuNs'] for r in rows if r['mmio'] == mode)
               for mode in (0, 1)}
    policy = {}
    for name in ('scaling_driver', 'scaling_governor', 'energy_performance_preference',
                 'scaling_min_freq', 'scaling_max_freq'):
        p = Path(f'/sys/devices/system/cpu/cpu{cpu}/cpufreq/{name}')
        policy[name] = p.read_text().strip() if p.exists() else None
    receipt = {'argv': argv, 'exitCode': run.returncode, 'pinnedCpu': cpu,
               'allowedCpus': allowed, 'frequencyPolicy': policy, 'frequencyLocked': False,
               'executableSha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
               'clock': 'CLOCK_PROCESS_CPUTIME_ID', 'samples': rows,
               'emptyLoopMedianNs': medians['0'], 'fixedMmioMedianNs': medians['1'],
               'pairedMedianAdditionalNsPerReadWrite': statistics.median(deltas),
               'minimumAdditionalNs': min(deltas), 'maximumAdditionalNs': max(deltas),
               'hypotheticalCpuPercentAt60PairsPerSecond': statistics.median(deltas) * 60 / 1e7,
               'runtimeCandidate': False, 'acceptedPerformanceImprovementPercent': None,
               'scope': 'Uncontended production INCREMENT read/write, including CAS, PFIFO wake and profiling. Idealized removal ceiling only; not game throughput, GPU contention or total emulator overhead.'}
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps({k: v for k, v in receipt.items() if k != 'samples'}, indent=2))


if __name__ == '__main__':
    main()

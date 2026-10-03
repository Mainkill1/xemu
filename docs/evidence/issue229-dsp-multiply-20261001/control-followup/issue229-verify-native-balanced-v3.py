"""Independently verify archived native CPU/frame/cadence reports and sources."""
from pathlib import Path
import csv
import hashlib
import json
import math
import re
import statistics

ROOT = Path('/home/codex/src/steamdeck-xemu/artifacts/issue229-dsp-multiply/native-balanced-v3/collected')
contracts = set()
for directory in sorted(ROOT.iterdir()):
    if not directory.is_dir():
        continue
    result_path = directory / 'result.json'
    if not result_path.exists():
        print(directory.name, 'incomplete collection; no verification claim')
        continue
    result = json.loads(result_path.read_text())
    required = ['performance.json', 'diagnostics/run-state/report.json']
    missing = [name for name in required if not (directory / name).exists()]
    if missing:
        assert result.get('assessment', {}).get('Comparison') != 'eligible', 'Eligible result is missing evidence'
        print(directory.name, 'terminal failure retained; unavailable verification:', ', '.join(missing))
        continue
    performance = json.loads((directory / 'performance.json').read_text())
    storage = json.loads((directory / 'diagnostics/run-state/report.json').read_text())
    if result.get('assessment', {}).get('Comparison') == 'eligible':
        assert storage['TargetStopped'] and not storage['Issues']
        assert storage['DriverQualification'] == 'mesa_private_disk_writes_observed'
        assert not storage['AllowUncontrolledDriverCache']
        contracts.add(storage['ContractSha256'])
    else:
        print(directory.name, 'canonical result ineligible; available statistics only')
    for source in performance['Sources']:
        blob = (directory / source['Path']).read_bytes()
        assert len(blob) == source['Bytes'] and hashlib.sha256(blob).hexdigest() == source['Sha256']
    if performance['Monitoring'] is not None:
        with (directory / 'metrics.csv').open(newline='') as stream:
            samples = [float(row['process_cpu_core_pct']) for row in csv.DictReader(stream)
                       if row['segment'] == performance['Profile']['Segment']]
        expected = performance['Monitoring']['Cpu']
        assert len(samples) == expected['Count']
        assert math.isclose(statistics.mean(samples), expected['Mean'], abs_tol=1e-9)
    if performance['Frames'] is not None:
        pattern = re.compile(r'timestamp_us=(\d+) frame=(\d+) delta_us=(\d+)')
        rows = []
        for line in (directory / 'guest-frames.log').read_text().splitlines():
            match = pattern.fullmatch(line)
            assert match is not None
            rows.append(tuple(map(int, match.groups())))
        cutoff = rows[-1][0] - performance['Profile']['FrameTailSeconds'] * 1000000
        valid = sorted(row[2] / 1000 for row in rows if row[0] >= cutoff and row[2] > 0)
        expected = performance['Frames']['IntervalsMs']
        assert len(valid) == expected['Count']
        assert math.isclose(statistics.mean(valid), expected['Mean'], abs_tol=1e-9)
        for label, fraction in [('P95', .95), ('P99', .99)]:
            index = (len(valid) - 1) * fraction
            lower = int(index)
            value = valid[lower] + (valid[math.ceil(index)] - valid[lower]) * (index - lower)
            assert math.isclose(value, expected[label], abs_tol=1e-9)
    if performance['Flips'] is not None:
        pattern = re.compile(r'elapsed_us=(\d+) frames=(\d+) fps=([\d.]+)')
        rows = []
        for line in (directory / 'guest-flips.log').read_text().splitlines():
            match = pattern.fullmatch(line)
            assert match is not None
            rows.append(tuple(map(int, match.groups()[:2])))
        tail = rows[-performance['Profile']['FlipTailSamples']:]
        elapsed = sum(row[0] for row in tail)
        frames = sum(row[1] for row in tail)
        expected = performance['Flips']
        assert len(tail) == expected['Samples'] and elapsed == expected['ElapsedUs']
        assert frames == expected['Frames']
        assert math.isclose(frames * 1000000 / elapsed, expected['CadenceFps'], abs_tol=1e-12)
    print(directory.name, 'available source hashes and raw statistics verified; eligibility remains canonical')
assert len(contracts) <= 1, 'Fixed storage contract differs between campaign attempts'
print('One fixed storage contract across collected attempts:', next(iter(contracts), None))

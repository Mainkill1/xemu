"""Secondary description of existing device-wide GPU CSV samples; no live sampler or CPU/frame calculation."""
import csv, hashlib, json, pathlib, statistics
root = pathlib.Path(__file__).resolve().parent
data_root = root / 'balanced' if (root / 'balanced').is_dir() else root
records = []
for host in ['windows', 'deck']:
    for game in ['pgr2', 'doaxbv', 'conker']:
        folder = data_root / host / game
        for evidence in sorted(folder.glob('[0-9][0-9]-[ab]-*')):
            if not (evidence / 'metrics.csv').exists():
                continue
            source = next(s for s in json.loads((evidence / 'performance.json').read_text())['analysis']['sources'] if s['path'] == 'metrics.csv')
            assert hashlib.sha256((evidence / 'metrics.csv').read_bytes()).hexdigest() == source['sha256']
            with (evidence / 'metrics.csv').open(newline='') as stream:
                rows = [r for r in csv.DictReader(stream) if r['segment'] == 'stationary-start']
            values = [float(r['gpu_pct']) for r in rows if r['gpu_pct']]
            process = [float(r['process_gpu_pct']) for r in rows if r['process_gpu_pct']]
            records.append({'host': host, 'game': game, 'variant': evidence.name.split('-')[1], 'runId': evidence.name[5:],
                'sourceSha256': source['sha256'], 'segmentRows': len(rows), 'deviceSamples': len(values),
                'missingDeviceSamples': len(rows) - len(values), 'deviceMeanPercent': statistics.mean(values) if values else None,
                'deviceMinPercent': min(values) if values else None, 'deviceMaxPercent': max(values) if values else None,
                'processSamples': len(process), 'processMeanPercent': statistics.mean(process) if process else None})
summary = []
for host in ['windows', 'deck']:
    for game in ['pgr2', 'doaxbv', 'conker']:
        for variant in ['a', 'b']:
            group = [r for r in records if (r['host'], r['game'], r['variant']) == (host, game, variant)]
            values = [r['deviceMeanPercent'] for r in group if r['deviceMeanPercent'] is not None]
            if group:
                summary.append({'host': host, 'game': game, 'variant': variant, 'attempts': len(group),
                    'medianOfDeviceMeansPercent': statistics.median(values) if len(values) == len(group) else None})
report = {'method': 'Secondary offline description of unchanged original metrics.csv. Named CPU measurement segment only. Median of per-attempt device-wide arithmetic means; no missing-value substitution. Not process-specific GPU attribution or a canonical eligibility calculation.',
    'runs': records, 'summary': summary}
(root / 'gpu-secondary.json').write_text(json.dumps(report, indent=2))
print(json.dumps(summary, indent=2))

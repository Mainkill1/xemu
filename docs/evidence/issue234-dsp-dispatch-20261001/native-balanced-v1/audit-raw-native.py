"""Check raw frame coverage and cached reports without changing runner outcomes.

Use after collect-native-balanced.py, with CAMPAIGN --out NEW_JSON.
Frame parsing/tail selection follows PerformanceAnalyzer.cs; below-floor counts
remain diagnostic and never produce replacement performance reports.
"""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import re


def normalized(value):
    if isinstance(value, dict):
        return {key.lower(): normalized(item) for key, item in value.items()}
    if isinstance(value, list):
        return [normalized(item) for item in value]
    return value


def percentile(values, fraction):
    rank = (len(values) - 1) * fraction
    lower, upper = math.floor(rank), math.ceil(rank)
    return values[lower] + (values[upper] - values[lower]) * (rank - lower)


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('campaign', type=Path)
parser.add_argument('--out', type=Path, required=True)
args = parser.parse_args()
if args.out.exists():
    raise SystemExit('Refusing an existing output file')
root = args.campaign
summary = json.loads((root / 'raw-audit-summary.json').read_text())
rows = []
pattern = re.compile(r'timestamp_us=(\d+) frame=(\d+) delta_us=(\d+)')
for attempt in summary['attempts']:
    directory = root / 'collected' / attempt['runId']
    stored = json.loads((directory / 'performance.json').read_text())
    result = json.loads((directory / 'result.json').read_text())
    cached = json.loads((root / (attempt['id'] + '-performance.json')).read_text())
    assert normalized(cached['analysis']) == normalized(stored), 'Cached/raw report mismatch'
    expected_outcome = {key.lower(): result['assessment'][key]
                        for key in ['Execution', 'Correctness', 'Evidence', 'Comparison']}
    assert normalized(cached['outcome']) == expected_outcome, 'Cached/raw outcome mismatch'
    assert cached['executableSha256'] == result['executableSha256']
    parsed = []
    ignored = 0
    for line in (directory / stored['Profile']['GuestFramesPath']).read_text().splitlines():
        match = pattern.fullmatch(line)
        if not match:
            assert not line.startswith('timestamp_us='), 'Malformed frame record'
            ignored += 1
            continue
        timestamp, frame, delta = map(int, match.groups())
        assert max(timestamp, frame, delta) <= 9007199254740991
        if parsed:
            assert timestamp >= parsed[-1][0] and frame > parsed[-1][1]
        parsed.append((timestamp, frame, delta))
    assert parsed, 'Missing frame records'
    start = parsed[-1][0] - stored['Profile']['FrameTailSeconds'] * 1000000
    tail = [item for item in parsed if item[0] >= start]
    positive = [item for item in tail if item[2] > 0]
    minimum = stored['Profile']['MinimumFrameSamples']
    assert len(tail) <= 250000
    qualified = len(positive) >= minimum
    assert qualified == (stored['Frames'] is not None), 'Frame coverage differs from runner'
    if qualified:
        values = sorted(item[2] / 1000 for item in positive)
        recalculated = dict(Count=len(values), Mean=sum(values) / len(values),
                            Median=percentile(values, .5), Min=values[0], Max=values[-1],
                            P95=percentile(values, .95), P99=percentile(values, .99))
        for key, value in recalculated.items():
            assert math.isclose(value, stored['Frames']['IntervalsMs'][key],
                                rel_tol=1e-12, abs_tol=1e-9), 'Frame distribution mismatch'
        assert stored['Frames']['WindowStartUs'] == start
        assert stored['Frames']['WindowEndUs'] == parsed[-1][0]
        assert stored['Frames']['FirstSampleUs'] == positive[0][0]
        assert stored['Frames']['LastSampleUs'] == positive[-1][0]
    source = next(item for item in stored['Sources']
                  if item['Path'] == stored['Profile']['GuestFramesPath'])
    assert source['Records'] == len(parsed) + ignored
    assert source['IgnoredRecords'] == ignored
    assert source['Sha256'] == hashlib.sha256(
        (directory / source['Path']).read_bytes()).hexdigest()
    failure = None
    if result['status'] != 'completed':
        crash = json.loads((directory / 'crash/report.json').read_text())
        native = json.loads((directory / 'crash/native/exit-native.json').read_text())
        failure = dict(detail=result['detail'], exitCode=result['exitCode'],
                       runnerCrashReport=crash, nativeExit=native,
                       interpretation='QMP quit timeout followed by runner termination; '
                                      'underlying shutdown blockage unclassified')
    rows.append(dict(id=attempt['id'], runId=attempt['runId'], cachedReportMatches=True,
                     parsedFrameRecords=len(parsed), ignoredFrameRecords=ignored,
                     tailStartUs=start, tailEndUs=parsed[-1][0],
                     positiveTailIntervals=len(positive), zeroTailIntervals=len(tail)-len(positive),
                     minimumFrameSamples=minimum, frameCoverageQualified=qualified,
                     canonicalComparison=result['assessment']['Comparison'], failure=failure))
csv_rows = list(csv.DictReader((root / 'server-comparison-full.csv').open()))
selected = [row for row in csv_rows if row['test'] == 'issue234-deck-pgr2-stationary-c-v1']
assert len(selected) == 14
assert all(row['verdict'] == 'ineligible' and row['improvement_percent'] == ''
           and row['a'] == row['b'] == '' for row in selected)
report = dict(attempts=rows, cachedReportsMatch=True,
              csvRows=len(csv_rows), relevantCsvRows=selected,
              rawFramesMatchRunnerCoverage=True,
              interpretation='Independent count/distribution audit only; canonical '
                             'outcomes unchanged; no comparison or repaired reports')
args.out.parent.mkdir(parents=True, exist_ok=True)
args.out.write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps({'attempts': len(rows), 'cachedReportsMatch': True,
                  'rawFramesMatchRunnerCoverage': True,
                  'belowFloor': [{'id': row['id'], 'count': row['positiveTailIntervals']}
                                 for row in rows if not row['frameCoverageQualified']]}))

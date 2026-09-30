#!/usr/bin/env python3
"""Audit retained runner contracts and aggregate probe rows, not CSV metrics."""
from datetime import datetime
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parent


def load(path):
    return json.loads(path.read_text())


def rows(text):
    parsed = {}
    for line in text.splitlines():
        if not line.startswith('jc ') or line.startswith('jc diagnostic:'):
            continue
        name = line.split()[1]
        if '=' in name:
            name = 'totals' if name.startswith('lookup_started') else 'targeted'
        target = parsed.setdefault(name, {})
        for key, value in re.findall(r'(\w+)=([\d,]+)', line):
            target[key] = [int(x) for x in value.split(',')] if key == 'occupancy' else int(value)
    return parsed


def counters(run):
    snapshots = {}
    for when in ('before', 'after'):
        paths = list(run.glob(f'diagnostics/*-jit-{when}/monitor.txt'))
        assert len(paths) == 1
        text = paths[0].read_text()
        result = load(paths[0].with_name('result.json'))
        assert result['Status'] == 'completed'
        snapshots[when] = (rows(text), text, result)
    before, before_text, before_result = snapshots['before']
    after, after_text, after_result = snapshots['after']
    seconds = (datetime.fromisoformat(after_result['StartedUtc']) - datetime.fromisoformat(before_result['StartedUtc'])).total_seconds()
    assert 30 < seconds < 36
    result = {'approximateSnapshotSeconds': seconds}
    invalidations = []
    for text in (before_text, after_text):
        invalidations.append(int(re.search(r'^TB invalidate count\s+(\d+)', text, re.M)[1]))
        assert int(re.search(r'^TB flush count\s+(\d+)', text, re.M)[1]) == 0
    result['individualInvalidations'] = invalidations[1] - invalidations[0]
    if not before and not after:
        result['probeRowsPresent'] = False
        return result
    assert set(before) == set(after)
    result['probeRowsPresent'] = True
    delta = {}
    for name, fields in before.items():
        delta[name] = {}
        for key, value in fields.items():
            if isinstance(value, list):
                diff = [b - a for a, b in zip(value, after[name][key], strict=True)]
                assert all(x >= 0 for x in diff)
            else:
                diff = after[name][key] - value
                assert diff >= 0
            delta[name][key] = diff
    result['delta'] = delta
    completed = sum(delta[name]['calls'] for name in ('hit', 'global_hit', 'global_miss'))
    result['completedLookups'] = completed
    result['startedMinusCompleted'] = delta['totals']['lookup_started'] - completed
    result['globalHitFraction'] = delta['global_hit']['calls'] / completed
    result['globalMissFraction'] = delta['global_miss']['calls'] / completed
    for name in ('hit', 'global_hit', 'global_miss', 'pcrel_flush', 'other_flush'):
        cost = delta[name]
        result[name + 'SampleMeanNs'] = cost['sample_ns'] / cost['samples'] if cost['samples'] else None
    for name in ('pcrel_flush', 'other_flush'):
        clear = delta[name]
        assert sum(clear['occupancy']) == clear['calls']
        assert clear['slots'] == 4096 * clear['calls']
        if clear['calls']:
            result[name + 'MeanObservedNonNull'] = clear['observed_nonnull'] / clear['calls']
            result[name + 'EmptyFraction'] = clear['occupancy'][0] / clear['calls']
        # Preserve absolute cumulative maxima; their difference is not an interval maximum.
        result[name + 'CumulativeMaximumBefore'] = before[name]['maximum_nonnull']
        result[name + 'CumulativeMaximumAfter'] = after[name]['maximum_nonnull']
    assert delta['pcrel_flush']['calls'] == result['individualInvalidations']
    return result


def audit():
    summary = {}
    for host in ('linux', 'windows'):
        identity = load(ROOT / f'{host}-build-identity.json')
        host_runs = {}
        labels = ('on1', 'off1', 'off2', 'on2', 'menu1') if host == 'linux' else ('on1', 'off1', 'off2', 'off3', 'on2')
        assert identity['sourceCommit'] == '06168a2f455b832bc6eb7936ebe725ec530b1b00'
        assert identity['sourceTree'] == '67c893a6de2adadc92557aad7b9e86da4dc14926'
        for label in labels:
            directories = list((ROOT / 'runs' / f'{host}-{label}').iterdir())
            assert len(directories) == 1
            run = directories[0]
            result = load(run / 'result.json')
            assessment = load(run / 'assessment.json')
            performance = load(run / 'performance.json')
            manifest = load(run / 'input-manifest.json')
            assert result['status'] == 'completed' and result['exitCode'] == 0
            expected_evidence = 'incomplete' if (host, label) == ('windows', 'off2') else 'complete'
            assert (assessment['Execution'], assessment['Correctness'], assessment['Evidence']) == ('completed', 'passed', expected_evidence)
            assert result['executableSha256'] == manifest['ExecutableSha256'] == identity['sha256']
            if expected_evidence == 'complete':
                assert performance['Complete'] and not performance['Errors']
            else:
                assert not performance['Complete'] and performance['Frames'] is None
                assert performance['Errors'] == ['frames: Analysis source changed while reading.']
            for source in performance['Sources']:
                data = (run / source['Path']).read_bytes()
                assert len(data) == source['Bytes']
                assert hashlib.sha256(data).hexdigest() == source['Sha256']
            counter_result = counters(run)
            assert counter_result['probeRowsPresent'] == (label.startswith('on') or label == 'menu1')
            host_runs[label] = {
                'runId': run.name,
                'assessment': {k: assessment[k] for k in ('Execution', 'Correctness', 'Evidence', 'Comparison')},
                'counterAnalysis': counter_result,
                'performance': performance,
            }
        summary[host] = {'identity': identity, 'runs': host_runs}
    return summary


if __name__ == '__main__':
    summary = audit()
    (ROOT / 'summary.json').write_text(json.dumps(summary, indent=2, sort_keys=True) + '\n')
    print('Ten retained runner outcomes, executable/source identities, available analysis source hashes and probe invariants verified; Windows OFF2 frame evidence remains incomplete.')

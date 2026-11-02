#!/usr/bin/env python3
"""Bounded seeded comparisons of synthetic production DSP engine fixtures."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import struct
import subprocess
import sys

from dsp_corpus import _open_regular

WRAPPER = Path(__file__).with_name('dsp-replay.py')
FIXTURES = ('scratch', 'fifo', 'ep-sink')
FRAMES = (9, 1, 8, 128, 1024)
BOUNDARIES = (0, 1, 0x7fffff, 0x800000, 0xffffff)
SIGNALS = ('impulse', 'silence', 'alternating', 'ramp', 'left', 'right',
           'extremes', 'near-zero', 'deterministic-noise', 'swept-sine')
cancel_requested = False


def request_cancel(_signal, _frame):
    global cancel_requested
    cancel_requested = True


def parameters(seed, index):
    digest = hashlib.sha256(b'xemu-engine-fuzz-v2' + struct.pack('<QQ', seed, index)).digest()
    fixture = FIXTURES[index % len(FIXTURES)]
    dimension = ('value', 'frames', 'signal', 'interaction', 'control')[(index // 3) % 5]
    if fixture == 'ep-sink' and dimension == 'signal':
        dimension = 'control'  # The EP-sink output is independent of GP mixbins.
    cycle = index // 15
    value = (int.from_bytes(digest[:3], 'little') if cycle % 2 == 0
             else BOUNDARIES[cycle % len(BOUNDARIES)])
    signal = (SIGNALS[1 + ((cycle * 2 + index % 3) % (len(SIGNALS) - 1))]
              if fixture != 'ep-sink' and dimension in ('signal', 'interaction') else 'impulse')
    return {'fixture': fixture,
            'frames': (FRAMES[(cycle + index % 3) % len(FRAMES)]
                       if dimension in ('frames', 'interaction') else 9),
            'value': value if dimension in ('value', 'interaction') else 0x123456,
            'dimension': dimension, 'signal': signal}


def canonical(obj):
    return json.dumps(obj, sort_keys=True, separators=(',', ':')).encode('utf-8')


def valid_parameters(p):
    return (isinstance(p, dict) and set(p) == {'fixture', 'frames', 'value', 'dimension', 'signal'} and
            p['fixture'] in FIXTURES and type(p['frames']) is int and
            p['frames'] in FRAMES and type(p['value']) is int and 0 <= p['value'] <= 0xffffff and
            p['dimension'] in ('value', 'frames', 'signal', 'interaction', 'control') and
            p['signal'] in SIGNALS and (p['fixture'] != 'ep-sink' or p['signal'] == 'impulse'))


def case_record(params, inject, worker_sha, wrapper_sha, runner_sha):
    body = {'version': 2, 'type': 'synthetic-engine-comparison', 'parameters': params,
            'engines': ['jit', 'interpreter'], 'reference_authority': 'UNVERIFIED',
            'injected_fault': inject, 'worker_sha256': worker_sha, 'wrapper_sha256': wrapper_sha,
            'runner_sha256': runner_sha}
    return dict(body, sha256=hashlib.sha256(canonical(body)).hexdigest())


def write_case(path, params, inject, worker_sha, wrapper_sha, runner_sha):
    with path.open('x', encoding='utf-8') as stream:
        json.dump(case_record(params, inject, worker_sha, wrapper_sha, runner_sha), stream, sort_keys=True)
        stream.write('\n')
    path.chmod(0o600)


def read_case(path):
    with _open_regular(path, 65536) as stream:
        record = json.load(stream)
    if not isinstance(record, dict):
        raise ValueError('case must be an object')
    digest = record.pop('sha256', None)
    if (set(record) != {'version', 'type', 'parameters', 'engines', 'reference_authority',
                        'injected_fault', 'worker_sha256', 'wrapper_sha256', 'runner_sha256'} or
            type(record['version']) is not int or record['version'] != 2 or
            record['type'] != 'synthetic-engine-comparison' or
            record['engines'] != ['jit', 'interpreter'] or
            record['reference_authority'] != 'UNVERIFIED' or
            type(record['injected_fault']) is not bool or
            any(not isinstance(record[k], str) or len(record[k]) != 64 or
                any(ch not in '0123456789abcdef' for ch in record[k])
                for k in ('worker_sha256', 'wrapper_sha256', 'runner_sha256')) or
            not valid_parameters(record['parameters']) or
            digest != hashlib.sha256(canonical(record)).hexdigest()):
        raise ValueError('invalid case')
    return record


def file_hash(path):
    digest = hashlib.sha256()
    with _open_regular(path, 1024 * 1024 * 1024) as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def run_case(params, inject, worker, timeout, worker_sha):
    command = [sys.executable, str(WRAPPER), 'verify-engines', '--worker', str(worker),
               '--fixture', params['fixture'], '--frames', str(params['frames']),
               '--value', str(params['value']), '--signal', params['signal'], '--timeout', str(timeout)]
    if inject:
        command.append('--inject-architectural-fault')
    try:
        proc = subprocess.run(command, capture_output=True, text=True, timeout=timeout + 10)
    except subprocess.TimeoutExpired:
        return {'status': 'TIMEOUT'}
    except OSError:
        return {'status': 'CRASH'}
    try:
        result = json.loads(proc.stdout)
        statuses = ('BASELINE_MATCH', 'MISMATCH', 'TIMEOUT', 'CRASH', 'INVALID',
                    'INCOMPLETE', 'UNSUPPORTED', 'INCONCLUSIVE', 'NONDETERMINISTIC',
                    'SELF_TEST_FAILED')
        if (not isinstance(result, dict) or result.get('status') not in statuses or
                (proc.returncode == 0) != (result['status'] == 'BASELINE_MATCH')):
            return {'status': 'INCONCLUSIVE'}
        if result.get('identities', {}).get('worker_sha256') != worker_sha:
            return {'status': 'INCONCLUSIVE'}
        return result
    except (ValueError, TypeError, AttributeError):
        return {'status': 'INCONCLUSIVE'}


def mismatch_key(result):
    diff = result.get('difference')
    if result.get('status') != 'MISMATCH' or not isinstance(diff, dict):
        return None
    if not isinstance(diff.get('field'), str):
        return None
    return (diff['field'], diff.get('index'), diff.get('sample'), diff.get('channel'),
            diff.get('component'), result.get('failure_frame'))


def campaign(args):
    args.output.mkdir(mode=0o700)
    worker_sha = file_hash(args.worker)
    wrapper_sha = file_hash(WRAPPER)
    runner_sha = file_hash(Path(__file__))
    digest = hashlib.sha256()
    unique = set()
    report = {'status': 'BASELINE_MATCH', 'seed': args.seed, 'start': args.start,
              'requested': args.count, 'attempted': 0, 'completed': 0,
              'reference_authority': 'UNVERIFIED', 'engines': ['jit', 'interpreter'],
              'injected_fault': args.inject_fault, 'worker_sha256': worker_sha,
              'wrapper_sha256': wrapper_sha, 'runner_sha256': runner_sha,
              'counts': {key: 0 for key in ('BASELINE_MATCH', 'MISMATCH', 'TIMEOUT', 'CRASH',
                                             'INVALID', 'INCOMPLETE', 'UNSUPPORTED', 'INCONCLUSIVE',
                                             'NONDETERMINISTIC', 'SELF_TEST_FAILED')},
              'coverage': {'fixtures': {}, 'frames': {}, 'dimensions': {}, 'signals': {}}}
    for index in range(args.start, args.start + args.count):
        if cancel_requested:
            report['status'] = 'CANCELLED'
            break
        params = parameters(args.seed, index)
        digest.update(canonical(params) + b'\n')
        unique.add((params['fixture'], params['frames'], params['value'], params['signal']))
        report['attempted'] += 1
        result = run_case(params, args.inject_fault, args.worker, args.timeout, worker_sha)
        if file_hash(WRAPPER) != wrapper_sha or file_hash(Path(__file__)) != runner_sha:
            result = {'status': 'INCONCLUSIVE'}
        library = result.get('linked_dsp_library')
        if (result['status'] == 'BASELINE_MATCH' and
                (not isinstance(library, dict) or
                 ('linked_dsp_library' in report and report['linked_dsp_library'] != library))):
            result = {'status': 'INCONCLUSIVE'}
        report['counts'][result['status']] += 1
        if result['status'] == 'BASELINE_MATCH':
            report['completed'] += 1
            for key, group_name in (('fixture', 'fixtures'), ('frames', 'frames'),
                                    ('dimension', 'dimensions'), ('signal', 'signals')):
                group = report['coverage'][group_name]
                name = str(params[key])
                group[name] = group.get(name, 0) + 1
            if 'linked_dsp_library' in result:
                report['linked_dsp_library'] = result['linked_dsp_library']
            continue
        report.update(status=result['status'], failure=result, failure_index=index)
        write_case(args.output / 'original.json', params, args.inject_fault, worker_sha, wrapper_sha,
                   runner_sha)
        key = mismatch_key(result)
        reduced = dict(params)
        if key is not None:
            for field, choices in (('frames', sorted(FRAMES)), ('value', BOUNDARIES)):
                for value in choices:
                    old = reduced[field]
                    simpler = ((value.bit_count(), value) < (old.bit_count(), old)
                               if field == 'value' else value < old)
                    if not simpler or cancel_requested:
                        continue
                    proposal = dict(reduced, **{field: value})
                    if mismatch_key(run_case(proposal, args.inject_fault, args.worker,
                                             args.timeout, worker_sha)) == key:
                        reduced = proposal
                        break
            if reduced['signal'] != 'impulse' and not cancel_requested:
                proposal = dict(reduced, signal='impulse')
                if mismatch_key(run_case(proposal, args.inject_fault, args.worker,
                                         args.timeout, worker_sha)) == key:
                    reduced = proposal
            if reduced != params:
                write_case(args.output / 'minimized.json', reduced, args.inject_fault,
                           worker_sha, wrapper_sha, runner_sha)
                report['reduction'] = reduced
        break
    if args.inject_fault and report['status'] == 'BASELINE_MATCH':
        report['status'] = 'SELF_TEST_FAILED'
    report['input_sequence_sha256'] = digest.hexdigest()
    report['unique_inputs'] = len(unique)
    with (args.output / 'report.json').open('x', encoding='utf-8') as stream:
        json.dump(report, stream, sort_keys=True)
        stream.write('\n')
    (args.output / 'report.json').chmod(0o600)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    run = sub.add_parser('run')
    run.add_argument('--worker', type=Path, required=True)
    run.add_argument('--seed', type=int, required=True)
    run.add_argument('--start', type=int, default=0)
    run.add_argument('--count', type=int, required=True)
    run.add_argument('--output', type=Path, required=True)
    run.add_argument('--inject-fault', action='store_true')
    replay = sub.add_parser('replay')
    replay.add_argument('--worker', type=Path, required=True)
    replay.add_argument('--case', type=Path, required=True)
    for part in (run, replay):
        part.add_argument('--timeout', type=float, default=30)
    args = parser.parse_args()
    args.worker = Path(os.path.abspath(args.worker))
    if not 0 < args.timeout <= 3600:
        parser.error('invalid timeout')
    if args.command == 'run' and not (0 <= args.seed < 2**64 and 1 <= args.count <= 10000 and
                                      0 <= args.start <= 2**64 - args.count):
        parser.error('invalid seed or case count')
    try:
        if args.command == 'run':
            signal.signal(signal.SIGINT, request_cancel)
            signal.signal(signal.SIGTERM, request_cancel)
            report = campaign(args)
        else:
            case = read_case(args.case)
            worker_sha = file_hash(args.worker)
            if (worker_sha != case['worker_sha256'] or file_hash(WRAPPER) != case['wrapper_sha256'] or
                    file_hash(Path(__file__)) != case['runner_sha256']):
                report = {'status': 'INCONCLUSIVE', 'reason': 'IDENTITY_MISMATCH'}
            else:
                report = run_case(case['parameters'], case['injected_fault'], args.worker,
                                  args.timeout, worker_sha)
            if read_case(args.case) != case:
                report = {'status': 'INCONCLUSIVE'}
    except (OSError, ValueError, TypeError, json.JSONDecodeError):
        report = {'status': 'INVALID'}
    print(json.dumps(report, sort_keys=True))
    return 0 if report.get('status') == 'BASELINE_MATCH' else 1


if __name__ == '__main__':
    raise SystemExit(main())

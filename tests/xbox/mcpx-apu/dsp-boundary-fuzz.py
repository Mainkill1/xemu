#!/usr/bin/env python3
"""Seeded GP-to-EP DMA baseline checks; these are not hardware goldens."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import struct
import subprocess
import sys
import tempfile

from dsp_corpus import _open_regular

WRAPPER = Path(__file__).with_name('dsp-replay.py')
NAMES = ('gp-initial', 'ep-initial', 'gp-expected', 'ep-expected', 'mixbins',
         'prior-pcm', 'expected-pcm', 'apu-registers', 'expected-apu-registers',
         'ram', 'expected-ram', 'control', 'events')
FRAMES = (1, 2, 8, 9, 128, 1024)
LIMIT = 128 * 1024 * 1024
cancel_requested = False


def request_cancel(_signal, _frame):
    global cancel_requested
    cancel_requested = True


def parameters(seed, index):
    material = struct.pack('<QQ', seed, index)
    digest = hashlib.sha256(b'xemu-dsp-boundary-v1' + material).digest()
    dimension = ('value', 'frames', 'page', 'interaction')[index % 4]
    cycle = index // 4
    boundaries = (1, 0x7fffff, 0x800000, 0xffffff, 0)
    variable = (int.from_bytes(digest[:4], 'little') & 0xffffff if cycle % 2 == 0
                else boundaries[((cycle - 1) // 2) % len(boundaries)])
    return {'value': variable if dimension in ('value', 'interaction') else 0x123456,
            'page': 0x4000 + (digest[4] % 4) * 0x1000 if dimension in ('page', 'interaction') else 0x4000,
            'frames': FRAMES[((cycle // 2) * 2 + (dimension == 'interaction')) % len(FRAMES)] if dimension in ('frames', 'interaction') else 1,
            'dimension': dimension, 'mode': 'fifo' if cycle % 2 else 'scratch'}


def scratch_fixture(value, page, frames, injected=False):
    if not 0 <= value <= 0xffffff or page not in (0x4000, 0x5000, 0x6000, 0x7000) or frames not in FRAMES:
        raise ValueError('invalid fixture parameters')
    gp = bytearray(41554)
    gp[:4] = b'XDJT'
    gp[4] = 1
    gp[-1] = 1
    ep = bytearray(gp)
    ep[-1] = 0
    for state, control in ((gp, 0x19e2), (ep, 0x19e0)):
        for index, word in enumerate((0x4000, control, 1, 0x200, 0, 0, 31)):
            struct.pack_into('<I', state, 567 + (0x100 + index) * 4, word)
        for index, word in enumerate((0x08f496, 1, 0x08f484, 1, 0x0c0002)):
            struct.pack_into('<I', state, 25143 + index * 4, word)
        struct.pack_into('<I', state, 41539, 0x100)
    struct.pack_into('<I', gp, 567 + 0x200 * 4, value)
    gp_expected, ep_expected = bytearray(gp), bytearray(ep)
    for state, runs in ((gp_expected, frames), (ep_expected, (frames + 7) // 8)):
        for offset, word in ((8, 4), (12, 2), (16, 6), (20, 994 * runs),
                             (41531, 0x10), (41539, 0x4000), (41547, 2)):
            struct.pack_into('<I', state, offset, word)
        state[26] = state[41552] = 1
    struct.pack_into('<I', ep_expected, 567 + 0x200 * 4, value)
    regs = bytearray(0x80000)
    struct.pack_into('<I', regs, 0x2040 * 4, 0x1000)
    struct.pack_into('<I', regs, 0x2048 * 4, 0x1008)
    ram = bytearray(0x10000)
    struct.pack_into('<I', ram, 0x1000, page)
    struct.pack_into('<I', ram, 0x1008, page)
    expected_ram = bytearray(ram)
    struct.pack_into('<I', expected_ram, page, value ^ int(injected))
    payload = struct.pack('<I', value)
    write = struct.pack('<BBHIIQI', 0, 1, 0, 0, 0, 0, 4) + payload
    read = struct.pack('<BBHIIQI', 0, 0, 0, 1, 0, 0, 4) + payload
    return {'gp-initial': gp, 'ep-initial': ep, 'gp-expected': gp_expected,
            'ep-expected': ep_expected, 'mixbins': bytes(4096 * frames),
            'prior-pcm': bytes(1024), 'expected-pcm': bytes(1024 * frames),
            'apu-registers': regs, 'expected-apu-registers': regs,
            'ram': ram, 'expected-ram': expected_ram,
            'control': struct.pack('<4sIIBBBB', b'XMF1', 3, 3, 2, 0, 0, 0),
            'events': b'XIO1' + struct.pack('<I', 2) + write + read}


def fifo_fixture(value, page, frames, injected=False):
    if not 0 <= value <= 0xffffff or page not in (0x4000, 0x5000, 0x6000, 0x7000) or frames not in FRAMES:
        raise ValueError('invalid fixture parameters')
    gp = bytearray(41554)
    gp[:4] = b'XDJT'
    gp[4] = 1
    gp[-1] = 1
    ep = bytearray(gp)
    ep[-1] = 0
    for index, word in enumerate((0x4000, 0x1802, 2, 0x200, 0, 0, 0)):
        struct.pack_into('<I', gp, 567 + (0x100 + index) * 4, word)
    other = value ^ 0xffffff
    for index, word in enumerate((value, other)):
        struct.pack_into('<I', gp, 567 + (0x200 + index) * 4, word)
    for index, word in enumerate((0x08f496, 1, 0x08f484, 1, 0x0c0002)):
        struct.pack_into('<I', gp, 25143 + index * 4, word)
    struct.pack_into('<I', gp, 41539, 0x100)
    expected_gp = bytearray(gp)
    for offset, word in ((8, 4), (12, 2), (16, 6), (20, 994 * frames),
                         (41531, 0x10), (41539, 0x4000), (41547, 2)):
        struct.pack_into('<I', expected_gp, offset, word)
    expected_gp[26] = expected_gp[41552] = 1
    regs = bytearray(0x80000)
    for address, word in ((0x2044, 0x1000), (0x20d8, 1), (0x3024, 4092),
                          (0x3028, 4100), (0x302c, 4096)):
        struct.pack_into('<I', regs, address * 4, word)
    ram = bytearray(0x10000)
    struct.pack_into('<I', ram, 0x1000, page)
    struct.pack_into('<I', ram, 0x1008, page + 0x2000)
    expected_ram = bytearray(ram)
    struct.pack_into('<I', expected_ram, page + 0x2000, value ^ int(injected))
    struct.pack_into('<I', expected_ram, page + 0xffc, other)
    payload = struct.pack('<II', value, other)
    event = struct.pack('<BBHIIQI', 1, 1, 0, 0, 0, 0, len(payload)) + payload
    return {'gp-initial': gp, 'ep-initial': ep, 'gp-expected': expected_gp,
            'ep-expected': ep, 'mixbins': bytes(4096 * frames),
            'prior-pcm': bytes(1024), 'expected-pcm': bytes(1024 * frames),
            'apu-registers': regs, 'expected-apu-registers': regs,
            'ram': ram, 'expected-ram': expected_ram,
            'control': struct.pack('<4sIIBBBB', b'XMF1', 3, 0, 2, 0, 0, 0),
            'events': b'XIO1' + struct.pack('<I', 1) + event}


def create_case(root, params, injected):
    builder = {'scratch': scratch_fixture, 'fifo': fifo_fixture}[params['mode']]
    files = builder(params['value'], params['page'], params['frames'], injected)
    root.mkdir(mode=0o700)
    hashes = {}
    for name, data in files.items():
        path = root / (name + '.bin')
        with path.open('xb') as stream:
            stream.write(data)
        path.chmod(0o600)
        hashes[name] = hashlib.sha256(data).hexdigest()
    metadata = {'version': 1, 'type': 'synthetic-mapped-dma', 'parameters': params,
                'reference_authority': 'EMULATOR_BASELINE', 'injected_fault': injected,
                'files': hashes}
    with (root / 'case.json').open('x', encoding='utf-8') as stream:
        json.dump(metadata, stream, sort_keys=True)
    (root / 'case.json').chmod(0o600)


def inspect_case(root):
    with _open_regular(root / 'case.json', 65536) as stream:
        data = stream.read(65537)
    if len(data) > 65536:
        raise ValueError('case manifest exceeds limit')
    metadata = json.loads(data)
    if (not isinstance(metadata, dict) or metadata.get('version') != 1 or
            metadata.get('type') != 'synthetic-mapped-dma' or
            not isinstance(metadata.get('files'), dict) or
            set(metadata['files']) != set(NAMES)):
        raise ValueError('invalid case manifest')
    params = metadata.get('parameters')
    if (not isinstance(params, dict) or type(params.get('frames')) is not int or
            params['frames'] not in FRAMES):
        raise ValueError('invalid frame count')
    for name in NAMES:
        with _open_regular(root / (name + '.bin'), LIMIT) as stream:
            digest = hashlib.sha256()
            for block in iter(lambda: stream.read(1024 * 1024), b''):
                digest.update(block)
        if digest.hexdigest() != metadata['files'][name]:
            raise ValueError('case payload digest mismatch')
    return metadata


def replay(root, worker, timeout):
    metadata = inspect_case(root)
    command = [sys.executable, str(WRAPPER), 'verify-mapped-trace', '--worker', str(worker),
               '--frames', str(metadata['parameters']['frames']), '--timeout', str(timeout)]
    for name in NAMES:
        command.extend(['--' + name, str(root / (name + '.bin'))])
    try:
        proc = subprocess.run(command, capture_output=True, text=True,
                              timeout=timeout + 10, check=False)
        report = json.loads(proc.stdout)
        if (not isinstance(report, dict) or report.get('status') not in
                ('BASELINE_MATCH', 'MISMATCH', 'TIMEOUT', 'UNSUPPORTED',
                 'INCOMPLETE', 'INVALID', 'CRASH', 'INCONCLUSIVE', 'NONDETERMINISTIC') or
                (proc.returncode == 0) != (report['status'] == 'BASELINE_MATCH')):
            raise ValueError('invalid replay report')
        if inspect_case(root) != metadata:
            return {'status': 'INCONCLUSIVE'}
        return report
    except (OSError, ValueError, subprocess.SubprocessError):
        return {'status': 'CRASH'}


def mismatch_key(result):
    difference = result.get('difference')
    if result.get('status') != 'MISMATCH' or not isinstance(difference, dict):
        return None
    if not isinstance(difference.get('field'), str):
        return None
    return (difference['field'], difference.get('index'), difference.get('sample'),
            difference.get('channel'), result.get('failure_frame'))


def campaign(args):
    output = args.output
    output.mkdir(mode=0o700)
    seed_digest = hashlib.sha256()
    unique_inputs = set()
    with _open_regular(args.worker, 1024 * 1024 * 1024) as stream:
        worker_hash = hashlib.sha256()
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            worker_hash.update(block)
        worker_sha256 = worker_hash.hexdigest()
    program = (struct.pack('<5I', 0x08f496, 1, 0x08f484, 1, 0x0c0002) +
               bytes((4096 - 5) * 4))
    program_hash = hashlib.sha256(program).hexdigest()
    empty_program_hash = hashlib.sha256(bytes(4096 * 4)).hexdigest()
    report = {'status': 'BASELINE_MATCH', 'reference_authority': 'EMULATOR_BASELINE',
              'seed': args.seed, 'start': args.start, 'requested': args.count,
              'attempted': 0, 'completed': 0,
              'programs': [],
              'modes': [], 'injected_fault': args.inject_fault,
              'worker_sha256': worker_sha256,
              'coverage': {'dimensions': {}, 'frames': {}, 'mode_frames': {}}}
    for index in range(args.count):
        if cancel_requested:
            report['status'] = 'CANCELLED'
            break
        params = parameters(args.seed, args.start + index)
        report['attempted'] += 1
        unique_inputs.add((params['mode'], params['value'], params['page'], params['frames']))
        seed_digest.update(struct.pack('<BIII', params['mode'] == 'fifo', params['value'], params['page'], params['frames']))
        with tempfile.TemporaryDirectory() as tmp:
            case = Path(tmp) / 'case'
            create_case(case, params, args.inject_fault)
            result = replay(case, args.worker, args.timeout)
            observed_worker = result.get('identities', {}).get('worker_sha256')
            if ((observed_worker is not None and observed_worker != worker_sha256) or
                    (result.get('status') == 'BASELINE_MATCH' and observed_worker is None)):
                result = {'status': 'INCONCLUSIVE'}
            if result.get('status') == 'BASELINE_MATCH':
                report['completed'] += 1
                mode = 'jit-mapped-' + params['mode']
                if mode not in report['modes']:
                    report['modes'].append(mode)
                    report['programs'].append({
                        'name': ('synthetic-gp-ep-alias-v1' if params['mode'] == 'scratch'
                                 else 'synthetic-gp-fifo-wrap-v1'),
                        'gp_program_sha256': program_hash,
                        'ep_program_sha256': (program_hash if params['mode'] == 'scratch'
                                              else empty_program_hash),
                        'overlay_generation': 0})
                for field in ('dimension', 'frames'):
                    group = report['coverage']['dimensions' if field == 'dimension' else 'frames']
                    name = str(params[field])
                    group[name] = group.get(name, 0) + 1
                mode_frames = report['coverage']['mode_frames'].setdefault(params['mode'], {})
                frame_name = str(params['frames'])
                mode_frames[frame_name] = mode_frames.get(frame_name, 0) + 1
                continue
            report.update(status=result.get('status', 'CRASH'), failure=result,
                          failure_index=args.start + index)
            create_case(output / 'original', params, args.inject_fault)
            key = mismatch_key(result)
            if key is not None:
                reduced = dict(params)
                for field, choices in (('frames', FRAMES),
                                       ('page', (0x4000, 0x5000, 0x6000, 0x7000)),
                                       ('value', (0, 1, 0x7fffff, 0x800000, 0xffffff))):
                    for value in choices:
                        old = reduced[field]
                        simpler = ((value.bit_count(), value) < (old.bit_count(), old)
                                   if field == 'value' else value < old)
                        if not simpler or cancel_requested:
                            continue
                        proposal = dict(reduced, **{field: value})
                        with tempfile.TemporaryDirectory() as reduced_tmp:
                            candidate = Path(reduced_tmp) / 'case'
                            create_case(candidate, proposal, args.inject_fault)
                            candidate_result = replay(candidate, args.worker, args.timeout)
                            if (candidate_result.get('identities', {}).get('worker_sha256') == worker_sha256 and
                                    mismatch_key(candidate_result) == key):
                                reduced = proposal
                                break
                if reduced != params:
                    create_case(output / 'minimized', reduced, args.inject_fault)
                    report['reduction'] = reduced
            break
    if args.inject_fault and report['status'] == 'BASELINE_MATCH':
        report['status'] = 'SELF_TEST_FAILED'
    report['input_sequence_sha256'] = seed_digest.hexdigest()
    report['unique_inputs'] = len(unique_inputs)
    with (output / 'report.json').open('x', encoding='utf-8') as stream:
        json.dump(report, stream, sort_keys=True)
        stream.write('\n')
    (output / 'report.json').chmod(0o600)
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
    run.add_argument('--inject-fault', action='store_true', help='deliberately corrupt expected RAM for runner self-test')
    replay_parser = sub.add_parser('replay')
    replay_parser.add_argument('--worker', type=Path, required=True)
    replay_parser.add_argument('--case', type=Path, required=True)
    for part in (run, replay_parser):
        part.add_argument('--timeout', type=float, default=30)
    args = parser.parse_args()
    # pathlib drops a leading './'; use an absolute lexical path so the
    # subprocess does not search PATH, while still rejecting worker symlinks.
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
        report = campaign(args) if args.command == 'run' else replay(args.case, args.worker, args.timeout)
    except (OSError, ValueError, TypeError, json.JSONDecodeError):
        report = {'status': 'INVALID'}
    print(json.dumps(report, sort_keys=True))
    return 0 if report.get('status') == 'BASELINE_MATCH' else 1


if __name__ == '__main__':
    raise SystemExit(main())

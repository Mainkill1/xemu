#!/usr/bin/env python3
"""Rebuild the same integration fixture against a historical NV2A source tree.

The supplied current Meson build must have built test-pgraph-ptimer-contention.
The pre-flip reference is2b21ca0126; no source file or shared build is rewritten.
Exact compiler/linker arguments, output, exit status and hashes are retained.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import time


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--reference-source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--test-timeout', type=float, default=60,
                        help='Hang watchdog only; never a latency correctness gate')
    args = parser.parse_args()
    build, reference, output = (p.resolve() for p in
                                (args.build_dir, args.reference_source, args.output))
    source = Path(__file__).resolve().parents[3]
    if output.exists() and any(output.iterdir()):
        parser.error('output is not empty; use a new directory to preserve prior observations')
    output.mkdir(parents=True, exist_ok=True)
    def git(directory, *argv):
        return subprocess.check_output(['git', *argv], cwd=directory).decode().strip()
    # Linked timer implementations come from the current build; this must be
    # exact equivalence, not an assertion based on similar function names.
    timer_inputs = ['hw/xbox/nv2a/ptimer.c', 'hw/xbox/nv2a/ptimer_core.c',
                    'hw/xbox/nv2a/ptimer_core.h']
    for name in timer_inputs:
        assert (source / name).read_bytes() == (reference / name).read_bytes(), name
    commands = json.loads((build / 'compile_commands.json').read_text())
    row = next(r for r in commands if r['file'].endswith('test-pgraph-ptimer-contention.c'))
    compile_args = shlex.split(row['command'])
    for i, value in enumerate(compile_args):
        if value.startswith('-I../'):
            compile_args[i] = '-I' + str(reference / value[5:])
        elif str(source) in value:
            compile_args[i] = value.replace(str(source), str(reference))
    compile_args[compile_args.index('-c') + 1] = str(Path(__file__).with_name('test-pgraph-ptimer-contention.c'))
    compile_args[compile_args.index('-o') + 1] = str(output / 'reference.o')
    for flag in ('-MD', '-MQ', '-MF'):
        if flag in compile_args:
            i = compile_args.index(flag)
            del compile_args[i:i + (1 if flag == '-MD' else 2)]
    compile_args.append('-DNV2A_CONTENTION_PRE_FLIP=1')
    target = 'tests/xbox/ptimer/test-pgraph-ptimer-contention'
    link_args = shlex.split(subprocess.check_output(
        ['ninja', '-t', 'commands', target], cwd=build).decode().splitlines()[-1])
    link_args[link_args.index('-o') + 1] = str(output / 'reference-control')
    for i, value in enumerate(link_args):
        if value.endswith('/test-pgraph-ptimer-contention.c.o'):
            link_args[i] = str(output / 'reference.o')
    receipt = {'candidateCommit': git(source, 'rev-parse', 'HEAD'),
               'referenceCommit': git(reference, 'rev-parse', 'HEAD'),
               'commands': [], 'runs': [], 'inputs': []}
    for name, argv in [('compile', compile_args), ('link', link_args)]:
        (output / (name + '-argv.json')).write_text(json.dumps(argv, indent=2) + '\n')
        with (output / (name + '.log')).open('wb') as stream:
            result = subprocess.run(argv, cwd=build, stdout=stream, stderr=subprocess.STDOUT)
        receipt['commands'].append({'stage': name, 'argv': argv, 'exitCode': result.returncode})
        assert result.returncode == 0, name
    runs = [('current', [str(build / target), '--tap'], 0),
            ('reference-negative', [str(output / 'reference-control'), '--negative-control', '--tap'], 0),
            ('reference-hard-gate', [str(output / 'reference-control'), '--tap', '-p', '/nv2a/contention/recurring-chain'], None)]
    for name, argv, expected in runs:
        start = time.monotonic_ns()
        timed_out = False
        with (output / (name + '.log')).open('wb') as stream:
            try:
                result = subprocess.run(argv, stdout=stream, stderr=subprocess.STDOUT,
                                        timeout=args.test_timeout)
            except subprocess.TimeoutExpired:
                timed_out = True
        elapsed = time.monotonic_ns() - start
        receipt['runs'].append({'name': name, 'argv': argv,
                                'exitCode': None if timed_out else result.returncode,
                                'timedOut': timed_out,
                                'elapsedHostNs': elapsed, 'executableSha256': digest(Path(argv[0]))})
        if timed_out:
            (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
            raise RuntimeError(f'{name}: test hang watchdog expired')
        if expected is None:
            assert result.returncode != 0
            assert 'renderer_waits == 0' in (output / (name + '.log')).read_text()
        else:
            assert result.returncode == expected
    for directory, label in [(source, 'candidate'), (reference, 'reference')]:
        for name in timer_inputs + ['hw/xbox/nv2a/pgraph/pgraph.c',
                                    'hw/xbox/nv2a/pgraph/pgraph.h', 'hw/xbox/nv2a/pfifo.c']:
            receipt['inputs'].append({'tree': label, 'path': name, 'sha256': digest(directory / name)})
    for file in [Path(__file__), Path(__file__).with_name('test-pgraph-ptimer-contention.c'),
                 Path(__file__).with_name('contention-hooks.h')]:
        receipt['inputs'].append({'tree': 'fixture', 'path': file.name, 'sha256': digest(file)})
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(receipt['runs'], indent=2))


if __name__ == '__main__':
    main()

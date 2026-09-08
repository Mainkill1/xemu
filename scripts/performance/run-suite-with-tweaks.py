#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run an unchanged XISO runner with an explicit CPU-wait configuration.

Requires Python 3.11+. The original runner and its dependencies remain in their
existing directory. Its SHA-256 is mandatory. Arguments after -- pass through
unchanged. The runner's normal evidence includes the generated xemu.toml.

Example:
  python run-suite-with-tweaks.py --runner /lab/run-suite.py \
      --runner-sha256 SHA256 --cpu-saving-wait on -- --help

This changes configuration only: workloads, iteration counts, inputs, collection,
and validation are owned by the pinned runner. It does not qualify gameplay.
"""
import argparse
import hashlib
import importlib.util
import logging
from pathlib import Path
import sys
import tomllib


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', type=Path, required=True)
    parser.add_argument('--runner-sha256', required=True)
    parser.add_argument('--cpu-saving-wait', choices=('off', 'on'), required=True)
    parser.add_argument('runner_args', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    runner = args.runner.resolve(strict=True)
    if hashlib.sha256(runner.read_bytes()).hexdigest() != args.runner_sha256.lower():
        raise ValueError('Pinned runner SHA-256 mismatch')
    sys.path.insert(0, str(runner.parent))
    spec = importlib.util.spec_from_file_location('xemu_pinned_suite', runner)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    original = module.xemu_config_addend

    def configured(*positional, **keyword):
        text = original(*positional, **keyword)
        parsed = tomllib.loads(text)
        if 'tweaks' in parsed:
            raise ValueError('Existing tweaks table: refusing ambiguous override')
        value = 'true' if args.cpu_saving_wait == 'on' else 'false'
        result = text + '\n[tweaks]\ncpu_saving_wait = ' + value + '\n'
        expected = dict(parsed, tweaks={'cpu_saving_wait': value == 'true'})
        if tomllib.loads(result) != expected:
            raise ValueError('Configuration changed outside the requested tweak')
        return result

    module.xemu_config_addend = configured
    forwarded = args.runner_args
    if forwarded[:1] == ['--']:
        forwarded = forwarded[1:]
    sys.argv = [str(runner), *forwarded]
    logging.basicConfig(level=logging.INFO,
                        format='%(asctime)s %(levelname)s %(message)s')
    return module.main()


if __name__ == '__main__':
    raise SystemExit(main())

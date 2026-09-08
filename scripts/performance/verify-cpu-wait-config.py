#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Verify a saved xemu config for a build with cpu_saving_wait default false.

Usage: python verify-cpu-wait-config.py --expected off path/to/xemu.toml

xemu saves delta TOML: omitted values retain build defaults. This verifier is
for the opt-in wait implementation, not arbitrary historical builds. Bind the
executable's source/hash separately in the campaign manifest. It proves saved
configuration, not successful high-resolution backend availability or gameplay.
"""
import argparse
import json
from pathlib import Path
import tomllib


def cpu_saving_wait(text):
    table = tomllib.loads(text).get('tweaks', {})
    if not isinstance(table, dict):
        raise ValueError('Invalid tweaks table')
    value = table.get('cpu_saving_wait', False)
    if type(value) is not bool:
        raise ValueError('cpu_saving_wait must be boolean')
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--expected', choices=('off', 'on'), required=True)
    parser.add_argument('config', type=Path)
    args = parser.parse_args()
    value = cpu_saving_wait(args.config.read_text(encoding='utf-8-sig'))
    matches = value == (args.expected == 'on')
    print(json.dumps({'status': 'PASSED' if matches else 'FAILED',
                      'effective_cpu_saving_wait': value,
                      'default_cpu_saving_wait': False}))
    return 0 if matches else 1


if __name__ == '__main__':
    raise SystemExit(main())

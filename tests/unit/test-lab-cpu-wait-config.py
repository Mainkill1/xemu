#!/usr/bin/env python3
"""Run with Python 3.11+: verify the optional-wait runner adapter."""
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import tomllib
import unittest

ADAPTER = Path(__file__).resolve().parents[2] / 'scripts/performance/run-suite-with-tweaks.py'

class AdapterTests(unittest.TestCase):
    def invoke(self, mode, extra='', wrong_hash=False):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            runner = root / 'runner.py'
            runner.write_text('import pathlib, sys\n'
                'def xemu_config_addend(*args, **kwargs):\n'
                f'    return "[display]\\nrenderer = \'VULKAN\'\\n" + {extra!r}\n'
                'def main():\n'
                '    pathlib.Path(sys.argv[1]).write_text(xemu_config_addend())\n'
                '    return 0\n')
            output = root / 'config.toml'
            digest = hashlib.sha256(runner.read_bytes()).hexdigest()
            result = subprocess.run([sys.executable, str(ADAPTER),
                '--runner', str(runner), '--runner-sha256', '0'*64 if wrong_hash else digest,
                '--cpu-saving-wait', mode, '--', str(output)], capture_output=True, text=True)
            return result, tomllib.loads(output.read_text()) if output.exists() else None

    def test_on_and_off_preserve_existing_configuration(self):
        for mode in ('on', 'off'):
            result, config = self.invoke(mode)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(config, {'display': {'renderer': 'VULKAN'},
                'tweaks': {'cpu_saving_wait': mode == 'on'}})

    def test_rejects_changed_runner_before_execution(self):
        result, config = self.invoke('on', wrong_hash=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIsNone(config)

    def test_rejects_ambiguous_existing_tweak(self):
        result, config = self.invoke('on', '[tweaks]\ncpu_saving_wait=false\n')
        self.assertNotEqual(result.returncode, 0)
        self.assertIsNone(config)

if __name__ == '__main__':
    unittest.main()

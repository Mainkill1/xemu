#!/usr/bin/env python3
"""Validate xemu delta-config receipts without confusing omitted defaults."""
import importlib.util
from pathlib import Path
import unittest
spec = importlib.util.spec_from_file_location('receipt', Path(__file__).resolve().parents[2] / 'scripts/performance/verify-cpu-wait-config.py')

class ReceiptTests(unittest.TestCase):
    def test_default_and_explicit_values(self):
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        for text, expected in [('', False), ('[tweaks]\ncpu_saving_wait=false', False),
                               ('[tweaks]\ncpu_saving_wait=true', True)]:
            self.assertIs(module.cpu_saving_wait(text), expected)

    def test_invalid_type_and_duplicate_rejected(self):
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        for text in ['[tweaks]\ncpu_saving_wait="false"',
                     '[tweaks]\ncpu_saving_wait=1',
                     '[tweaks]\ncpu_saving_wait=true\ncpu_saving_wait=false']:
            with self.assertRaises(ValueError):
                module.cpu_saving_wait(text)

if __name__ == '__main__': unittest.main()

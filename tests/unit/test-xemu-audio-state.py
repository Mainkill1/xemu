#!/usr/bin/env python3
"""Checks for bounded reads in the actual GDB audio inspector helpers."""
import ast
import hashlib
import io
import json
from pathlib import Path
import tempfile
import types
import unittest
from unittest.mock import patch


SOURCE = Path(__file__).resolve().parents[2] / 'scripts/xemu-audio-state.py'


class InspectorChecks(unittest.TestCase):
    def setUp(self):
        # Loading helpers must not attach to, resume or inspect an inferior.
        tree = ast.parse(SOURCE.read_text())
        tree.body = [node for node in tree.body
                     if isinstance(node, ast.FunctionDef)]
        self.gdb = types.SimpleNamespace()
        self.helpers = dict(gdb=self.gdb, hashlib=hashlib, json=json, Path=Path)
        exec(compile(tree, str(SOURCE), 'exec'), self.helpers)

    def memory(self, payload):
        self.reads = []

        def read(address, size):
            self.reads.append((address, size))
            self.assertEqual(size, 1)
            self.assertLess(address, 1000 + len(payload))
            return memoryview(payload[address - 1000:address - 1000 + size])

        self.gdb.selected_inferior = lambda: types.SimpleNamespace(
            read_memory=read)

    def test_null_string_does_not_read(self):
        self.memory(b'')
        self.assertIsNone(self.helpers['optional_string'](0, 'driver.name'))
        self.assertEqual(self.reads, [])

    def test_string_stops_at_nul(self):
        self.memory(b'sdl\0')
        self.assertEqual(self.helpers['optional_string'](1000, 'driver.name'),
                         'sdl')
        self.assertEqual(len(self.reads), 4)

    def test_unterminated_string_is_bounded_and_named(self):
        self.memory(b'x' * 256)
        with self.assertRaisesRegex(ValueError, 'driver.name.*256'):
            self.helpers['optional_string'](1000, 'driver.name')
        self.assertEqual(len(self.reads), 256)

    def test_unreadable_string_identifies_field(self):
        def read(address, size):
            raise RuntimeError('invalid inferior address')

        self.gdb.selected_inferior = lambda: types.SimpleNamespace(
            read_memory=read)
        with self.assertRaisesRegex(ValueError, 'voice.name.*invalid'):
            self.helpers['optional_string'](1000, 'voice.name')

    def test_executable_hash_is_streamed(self):
        data = b'abc' * 1000000

        class BoundedStream(io.BytesIO):
            def read(self, size=-1):
                if not 0 < size <= 1024 * 1024:
                    raise AssertionError('unbounded executable read')
                return super().read(size)

        with patch.object(Path, 'open', return_value=BoundedStream(data)):
            actual = self.helpers['executable_hash'](Path('/proc/123/exe'))
        self.assertEqual(actual, hashlib.sha256(data).hexdigest())

    def test_executable_error_is_actionable(self):
        with patch.object(Path, 'open', side_effect=PermissionError('denied')):
            with self.assertRaisesRegex(ValueError, '/proc/123/exe.*denied'):
                self.helpers['executable_hash'](Path('/proc/123/exe'))

    def test_missing_layout_field_is_named(self):
        self.gdb.lookup_type = lambda name: types.SimpleNamespace(fields=lambda: [])
        with self.assertRaisesRegex(ValueError, 'AudioBackend.*drv'):
            self.helpers['check_audio_fields']()

    def test_matching_layout_is_reported(self):
        expected = {
            'AudioBackend': ['drv', 'dev', 'period_ticks', 'timer_running',
                             'vm_running', 'hw_head_out'],
            'HWVoiceOut': ['enabled', 'info', 'ts_helper', 'sw_head', 'entries'],
            'SWVoiceOut': ['name', 'active', 'empty', 'info',
                           'total_hw_samples_mixed', 'vol', 'entries'],
        }
        self.gdb.lookup_type = lambda name: types.SimpleNamespace(
            fields=lambda: [types.SimpleNamespace(name=field)
                            for field in expected[name]])
        self.assertEqual(self.helpers['check_audio_fields'](), expected)

    def test_report_is_created_once(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'report.json'
            self.helpers['write_report'](path, dict(schema=1))
            self.assertEqual(json.loads(path.read_text()), dict(schema=1))
            with self.assertRaises(FileExistsError):
                self.helpers['write_report'](path, dict(schema=99))
            self.assertEqual(json.loads(path.read_text()), dict(schema=1))

    def test_report_does_not_follow_dangling_symlink(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'report.json'
            target = Path(directory) / 'absent.json'
            path.symlink_to(target)
            with self.assertRaises(FileExistsError):
                self.helpers['write_report'](path, dict(schema=1))
            self.assertFalse(target.exists())


if __name__ == '__main__':
    unittest.main()

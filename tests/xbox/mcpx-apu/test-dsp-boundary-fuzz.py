#!/usr/bin/env python3
"""Exercise seeded boundary campaigns against the real replay worker."""
import json
import os
from pathlib import Path
import subprocess
import signal
import sys
import tempfile
import time
import unittest

WORKER = sys.argv.pop(1)
TOOL = Path(__file__).with_name('dsp-boundary-fuzz.py')
ENGINE_TOOL = Path(__file__).with_name('dsp-engine-fuzz.py')


class BoundaryFuzzTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)

    def run_tool(self, *args):
        return subprocess.run([sys.executable, str(TOOL), *args],
                              capture_output=True, text=True, timeout=60)

    def run_engines(self, *args):
        return subprocess.run([sys.executable, str(ENGINE_TOOL), *args],
                              capture_output=True, text=True, timeout=60)

    def test_engine_campaign_is_seeded_and_covers_three_fixtures(self):
        reports = []
        for folder in ('engine-first', 'engine-second'):
            result = self.run_engines('run', '--worker', WORKER, '--seed', '89',
                                      '--count', '15', '--output', str(self.root / folder))
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            report = json.loads(result.stdout)
            self.assertEqual(report['status'], 'BASELINE_MATCH')
            self.assertEqual(report['completed'], 15)
            self.assertEqual(report['counts']['BASELINE_MATCH'], 15)
            self.assertEqual(sum(report['counts'].values()), report['attempted'])
            self.assertEqual(set(report['coverage']['fixtures']), {'scratch', 'fifo', 'ep-sink'})
            self.assertEqual(set(report['coverage']['dimensions']),
                             {'value', 'frames', 'signal', 'interaction', 'control'})
            self.assertTrue({'impulse', 'silence', 'alternating'} <= set(report['coverage']['signals']))
            self.assertTrue(set(report['coverage']['signals']) <= {
                'impulse', 'silence', 'alternating', 'ramp', 'left', 'right',
                'extremes', 'near-zero', 'deterministic-noise', 'swept-sine'})
            self.assertGreaterEqual(report['unique_inputs'], 6)
            self.assertEqual(report['reference_authority'], 'UNVERIFIED')
            reports.append(report)
        self.assertEqual(reports[0]['input_sequence_sha256'], reports[1]['input_sequence_sha256'])

    def test_engine_campaign_reduces_and_replays_injected_failure(self):
        output = self.root / 'engine-failure'
        result = self.run_engines('run', '--worker', WORKER, '--seed', '89', '--count', '1',
                                  '--output', str(output), '--inject-fault')
        self.assertNotEqual(result.returncode, 0)
        report = json.loads(result.stdout)
        self.assertEqual(report['status'], 'MISMATCH')
        self.assertEqual(report['failure']['difference']['field'], 'gp.arch.register')
        self.assertEqual(report['reduction']['frames'], 1)
        replay = self.run_engines('replay', '--worker', WORKER, '--case', str(output / 'minimized.json'))
        self.assertNotEqual(replay.returncode, 0)
        self.assertEqual(json.loads(replay.stdout)['difference']['field'], 'gp.arch.register')
        other = self.root / 'other-worker'
        other.write_bytes(b'not the recorded worker')
        replay = self.run_engines('replay', '--worker', str(other), '--case', str(output / 'minimized.json'))
        self.assertEqual(json.loads(replay.stdout)['status'], 'INCONCLUSIVE')
        self.assertEqual(json.loads(replay.stdout)['reason'], 'IDENTITY_MISMATCH')
        case = output / 'minimized.json'
        data = json.loads(case.read_text())
        data['parameters']['value'] ^= 1
        case.write_text(json.dumps(data))
        replay = self.run_engines('replay', '--worker', WORKER, '--case', str(case))
        self.assertNotEqual(replay.returncode, 0)
        self.assertEqual(json.loads(replay.stdout)['status'], 'INVALID')

    @unittest.skipUnless(os.name == 'posix', 'POSIX signal required')
    def test_cancelled_engine_campaign_is_not_a_pass(self):
        output = self.root / 'engine-cancelled'
        proc = subprocess.Popen([sys.executable, str(ENGINE_TOOL), 'run', '--worker', WORKER,
                                 '--seed', '89', '--count', '10000', '--output', str(output)],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            deadline = time.monotonic() + 10
            while not output.is_dir() and proc.poll() is None and time.monotonic() < deadline:
                time.sleep(0.01)
            self.assertTrue(output.is_dir())
            proc.send_signal(signal.SIGTERM)
            stdout, stderr = proc.communicate(timeout=45)
            self.assertNotEqual(proc.returncode, 0, stderr)
            report = json.loads(stdout)
            self.assertEqual(report['status'], 'CANCELLED')
            self.assertLess(report['completed'], report['requested'])
            self.assertEqual(json.loads((output / 'report.json').read_text()), report)
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.communicate()

    def test_seeded_cases_pass_and_are_repeatable(self):
        reports = []
        for folder in ('first', 'second'):
            result = self.run_tool('run', '--worker', WORKER, '--seed', '47',
                                   '--count', '6', '--output', str(self.root / folder))
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            report = json.loads(result.stdout)
            self.assertEqual(report['status'], 'BASELINE_MATCH')
            self.assertEqual(report['completed'], 6)
            self.assertGreaterEqual(report['unique_inputs'], 4)
            self.assertEqual(report['reference_authority'], 'EMULATOR_BASELINE')
            self.assertEqual(set(report['modes']), {'jit-mapped-scratch', 'jit-mapped-fifo'})
            self.assertEqual(set(report['coverage']['dimensions']),
                             {'value', 'frames', 'page', 'interaction'})
            reports.append(report)
        self.assertEqual(reports[0]['input_sequence_sha256'], reports[1]['input_sequence_sha256'])

    def test_injected_ram_mismatch_is_reduced_and_replayable(self):
        output = self.root / 'injected'
        result = self.run_tool('run', '--worker', WORKER, '--seed', '47',
                               '--count', '1', '--output', str(output), '--inject-fault')
        self.assertNotEqual(result.returncode, 0)
        report = json.loads(result.stdout)
        self.assertEqual(report['status'], 'MISMATCH')
        self.assertEqual(report['failure']['difference']['field'], 'ram')
        self.assertEqual(report['reduction']['value'], 0)
        replay = self.run_tool('replay', '--worker', WORKER,
                               '--case', str(output / 'minimized'))
        self.assertNotEqual(replay.returncode, 0)
        self.assertEqual(json.loads(replay.stdout)['difference']['field'], 'ram')
        payload = output / 'minimized' / 'ram.bin'
        data = bytearray(payload.read_bytes())
        data[-1] ^= 1
        payload.write_bytes(data)
        replay = self.run_tool('replay', '--worker', WORKER,
                               '--case', str(output / 'minimized'))
        self.assertNotEqual(replay.returncode, 0)
        self.assertEqual(json.loads(replay.stdout)['status'], 'INVALID')

    def test_both_dma_modes_reach_1024_frame_sequences(self):
        result = self.run_tool('run', '--worker', WORKER, '--seed', '47',
                               '--count', '24', '--output', str(self.root / 'coverage'))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        coverage = json.loads(result.stdout)['coverage']['mode_frames']
        self.assertGreater(coverage['scratch']['1024'], 0)
        self.assertGreater(coverage['fifo']['1024'], 0)

    def test_reducer_shortens_a_late_sequence_without_changing_failure_location(self):
        output = self.root / 'late'
        result = self.run_tool('run', '--worker', WORKER, '--seed', '47',
                               '--start', '11', '--count', '1', '--output', str(output),
                               '--inject-fault')
        self.assertNotEqual(result.returncode, 0)
        report = json.loads(result.stdout)
        self.assertEqual(report['status'], 'MISMATCH')
        original = json.loads((output / 'original' / 'case.json').read_text())
        self.assertEqual(original['parameters']['frames'], 9)
        self.assertEqual(report['reduction']['frames'], 1)
        replay = self.run_tool('replay', '--worker', WORKER, '--case', str(output / 'minimized'))
        self.assertEqual(json.loads(replay.stdout)['difference']['index'],
                         report['failure']['difference']['index'])

    @unittest.skipUnless(os.name == 'posix', 'POSIX signal required')
    def test_cancelled_campaign_reports_completed_work(self):
        output = self.root / 'cancelled'
        proc = subprocess.Popen([sys.executable, str(TOOL), 'run', '--worker', WORKER,
                                 '--seed', '47', '--count', '10000', '--output', str(output)],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            deadline = time.monotonic() + 10
            while not output.is_dir() and proc.poll() is None and time.monotonic() < deadline:
                time.sleep(0.01)
            self.assertTrue(output.is_dir())
            proc.send_signal(signal.SIGTERM)
            stdout, stderr = proc.communicate(timeout=20)
            self.assertNotEqual(proc.returncode, 0, stderr)
            report = json.loads(stdout)
            self.assertEqual(report['status'], 'CANCELLED')
            self.assertLess(report['completed'], report['requested'])
            self.assertEqual(json.loads((output / 'report.json').read_text()), report)
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.communicate()


if __name__ == '__main__':
    unittest.main()

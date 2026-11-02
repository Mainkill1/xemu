#!/usr/bin/env python3
"""Exercise the replay command as a process, including its watchdog."""

import json
import hashlib
import os
from pathlib import Path
import subprocess
import struct
import sys
import tempfile
import unittest

WORKER = sys.argv.pop(1)
FALLBACK_ARCHIVE = sys.argv.pop(1) if len(sys.argv) > 1 else ""
WRAPPER = Path(__file__).with_name("dsp-replay.py")


class ReplayCommandTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        state = bytearray(41554)
        state[:4] = b"XDJT"
        state[4:8] = (1).to_bytes(4, "little")
        state[26] = 1  # already halted: no instruction or I/O is needed
        state[-1] = 1  # GP
        self.initial = self.root / "initial.bin"
        self.expected = self.root / "expected.bin"
        self.initial.write_bytes(state)
        self.expected.write_bytes(state)

    def run_wrapper(self, *extra, worker=WORKER):
        return subprocess.run([sys.executable, str(WRAPPER), "verify-core",
                               "--worker", worker, "--initial", str(self.initial),
                               "--expected", str(self.expected), "--steps", "5",
                               *extra], capture_output=True, text=True, timeout=5)

    def test_production_engines_compare_synthetic_scratch_and_fifo(self):
        for fixture in ('scratch', 'fifo', 'ep-sink'):
            run = subprocess.run([sys.executable, str(WRAPPER), 'verify-engines',
                                  '--worker', WORKER, '--fixture', fixture,
                                  '--frames', '9', '--value', '1193046'],
                                 capture_output=True, text=True, timeout=15)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            report = json.loads(run.stdout)
            self.assertEqual(report['status'], 'BASELINE_MATCH')
            self.assertEqual(report['reference_authority'], 'UNVERIFIED')
            self.assertEqual(report['engines'], ['jit', 'interpreter'])
            self.assertEqual(report['frames'], 9)
            self.assertGreater(report['observations']['io_events'], 0)
            self.assertGreater(report['observations']['pcm_nonzero_comparisons'], 0)
            self.assertGreater(report['observations']['dsp_memory_word_comparisons'], 0)
            self.assertGreater(report['observations']['architectural_comparisons'], 0)
            self.assertEqual(report['observations']['native_checkpoint_comparisons'], 0)
            self.assertTrue(report['observations']['initial_subset_checked'])

    def test_gp_signal_patterns_match_and_shared_silence_is_detected(self):
        for fixture in ('scratch', 'fifo'):
            for signal in ('impulse', 'silence', 'alternating', 'ramp', 'left', 'right',
                           'extremes', 'near-zero', 'deterministic-noise', 'swept-sine'):
                with self.subTest(fixture=fixture, signal=signal):
                    run = subprocess.run(
                        [sys.executable, str(WRAPPER), 'verify-engines', '--worker', WORKER,
                         '--fixture', fixture, '--frames', '9', '--value', '1193046',
                         '--signal', signal], capture_output=True, text=True, timeout=15)
                    self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                    report = json.loads(run.stdout)
                    self.assertEqual(report['signal'], signal)
                    if signal == 'silence':
                        self.assertEqual(report['observations']['pcm_nonzero_comparisons'], 0)
                    else:
                        self.assertGreater(report['observations']['pcm_nonzero_comparisons'], 0)
        run = subprocess.run(
            [sys.executable, str(WRAPPER), 'verify-engines', '--worker', WORKER,
             '--fixture', 'scratch', '--frames', '1', '--value', '0',
             '--signal', 'alternating', '--inject-both-pcm-silence'],
            capture_output=True, text=True, timeout=15)
        self.assertNotEqual(run.returncode, 0)
        self.assertEqual(json.loads(run.stdout)['difference']['field'], 'fixture.pcm')

    def test_ep_sink_rejects_gp_only_signal_selection(self):
        run = subprocess.run(
            [sys.executable, str(WRAPPER), 'verify-engines', '--worker', WORKER,
             '--fixture', 'ep-sink', '--frames', '1', '--value', '0',
             '--signal', 'near-zero'], capture_output=True, text=True, timeout=15)
        self.assertNotEqual(run.returncode, 0)
        report = json.loads(run.stdout)
        self.assertEqual((report['status'], report['reason']),
                         ('INVALID', 'unsupported fixture or signal'))

    def test_production_engine_comparator_reports_injected_divergence(self):
        run = subprocess.run([sys.executable, str(WRAPPER), 'verify-engines',
                              '--worker', WORKER, '--fixture', 'scratch',
                              '--frames', '2', '--value', '1193046', '--inject-fault'],
                             capture_output=True, text=True, timeout=15)
        self.assertNotEqual(run.returncode, 0)
        report = json.loads(run.stdout)
        self.assertEqual(report['status'], 'MISMATCH')
        self.assertEqual(report['failure_frame'], 0)
        self.assertEqual(report['difference']['field'], 'io_event')

    def test_engine_comparator_reports_first_pcm_sample_and_channel(self):
        run = subprocess.run([sys.executable, str(WRAPPER), 'verify-engines',
                              '--worker', WORKER, '--fixture', 'scratch', '--frames', '2',
                              '--value', '1193046', '--inject-pcm-fault'],
                             capture_output=True, text=True, timeout=15)
        self.assertNotEqual(run.returncode, 0)
        report = json.loads(run.stdout)
        self.assertEqual(report['status'], 'MISMATCH')
        self.assertEqual(report['failure_frame'], 0)
        self.assertEqual((report['difference']['field'], report['difference']['sample'],
                          report['difference']['channel']), ('pcm', 0, 0))
        self.assertEqual((report['difference']['expected'], report['difference']['actual']),
                         (16384, 16385))

    def test_engine_comparator_reports_changed_dma_request(self):
        run = subprocess.run([sys.executable, str(WRAPPER), 'verify-engines',
                              '--worker', WORKER, '--fixture', 'scratch', '--frames', '1',
                              '--value', '1193046', '--inject-request-fault'],
                             capture_output=True, text=True, timeout=15)
        self.assertNotEqual(run.returncode, 0)
        report = json.loads(run.stdout)
        difference = report['difference']
        self.assertEqual((report['status'], report['failure_frame']), ('MISMATCH', 0))
        self.assertEqual((difference['field'], difference['component']), ('io_event', 'address'))
        self.assertEqual((difference['expected'], difference['actual']), (0, 4))
        self.assertEqual(difference['sample'], 0)
        self.assertEqual(set(report['post_subframe_pc']),
                         {'reference_gp', 'reference_ep', 'candidate_gp', 'candidate_ep'})
        self.assertTrue(all(isinstance(pc, int) for pc in report['post_subframe_pc'].values()))

    def test_engine_comparator_checks_guest_readable_dsp_memory(self):
        run = subprocess.run([sys.executable, str(WRAPPER), 'verify-engines',
                              '--worker', WORKER, '--fixture', 'scratch', '--frames', '1',
                              '--value', '1193046', '--inject-memory-fault'],
                             capture_output=True, text=True, timeout=15)
        self.assertNotEqual(run.returncode, 0)
        report = json.loads(run.stdout)
        difference = report['difference']
        self.assertEqual((difference['field'], difference['index']), ('gp.xram', 0x300))
        self.assertEqual((difference['expected'], difference['actual']), (0, 1))

    def test_engine_comparator_checks_gp_mmio_registers(self):
        run = subprocess.run([sys.executable, str(WRAPPER), 'verify-engines',
                              '--worker', WORKER, '--fixture', 'scratch', '--frames', '1',
                              '--value', '1193046', '--inject-register-fault'],
                             capture_output=True, text=True, timeout=15)
        self.assertNotEqual(run.returncode, 0)
        difference = json.loads(run.stdout)['difference']
        self.assertEqual((difference['field'], difference['index']), ('gp.register', 0x80))
        self.assertEqual((difference['expected'], difference['actual']), (0, 1))

    def test_engine_comparator_checks_data_registers(self):
        run = subprocess.run([sys.executable, str(WRAPPER), 'verify-engines',
                              '--worker', WORKER, '--fixture', 'scratch', '--frames', '1',
                              '--value', '1193046', '--inject-architectural-fault'],
                             capture_output=True, text=True, timeout=15)
        self.assertNotEqual(run.returncode, 0)
        difference = json.loads(run.stdout)['difference']
        self.assertEqual((difference['field'], difference['index']), ('gp.arch.register', 4))
        self.assertEqual((difference['expected'], difference['actual']), (0, 1))

    def test_engine_comparator_checks_address_registers(self):
        for register, index, reference, candidate in [
                (reg, idx, ref, cand)
                for reg, idx in [('r0', 16), ('n0', 24), ('m0', 32), ('sr', 57)]
                for ref, cand in [('jit', 'interpreter'), ('interpreter', 'jit')]]:
            with self.subTest(register=register, reference=reference):
                run = subprocess.run([sys.executable, str(WRAPPER), 'verify-engines',
                                      '--worker', WORKER, '--fixture', 'scratch',
                                      '--frames', '1', '--value', '0',
                                      '--reference', reference, '--candidate', candidate,
                                      '--inject-address-fault', register],
                                     capture_output=True, text=True, timeout=15)
                self.assertNotEqual(run.returncode, 0)
                report = json.loads(run.stdout)
                self.assertEqual(report['status'], 'MISMATCH')
                self.assertEqual((report['difference']['field'], report['difference']['index']),
                                 ('gp.arch.register', index))

    def test_engine_comparator_checks_both_dsp_stack_banks(self):
        for dsp in ('gp', 'ep'):
            for bank in (0, 1):
                for reference, candidate in (('jit', 'jit'), ('interpreter', 'interpreter'),
                                             ('jit', 'interpreter'), ('interpreter', 'jit')):
                    with self.subTest(dsp=dsp, bank=bank, reference=reference,
                                      candidate=candidate):
                        run = subprocess.run(
                            [sys.executable, str(WRAPPER), 'verify-engines',
                             '--worker', WORKER, '--fixture', 'scratch', '--frames', '1',
                             '--value', '0', '--reference', reference, '--candidate', candidate,
                             '--inject-stack-fault', f'{dsp}{bank}'],
                            capture_output=True, text=True, timeout=15)
                        self.assertNotEqual(run.returncode, 0)
                        report = json.loads(run.stdout)
                        self.assertEqual(report['status'], 'MISMATCH')
                        self.assertEqual((report['difference']['field'], report['difference']['index']),
                                         (f'{dsp}.arch.stack', bank * 16 + 7))

    def test_same_engine_comparison_checks_native_interrupt_state(self):
        for engine in ('jit', 'interpreter'):
            for dsp in ('gp', 'ep'):
                with self.subTest(engine=engine, dsp=dsp):
                    run = subprocess.run(
                        [sys.executable, str(WRAPPER), 'verify-engines', '--worker', WORKER,
                         '--fixture', 'scratch', '--frames', '1', '--value', '0', '--reference', engine,
                         '--candidate', engine, '--inject-native-interrupt-fault', dsp],
                        capture_output=True, text=True, timeout=15)
                    self.assertNotEqual(run.returncode, 0)
                    report = json.loads(run.stdout)
                    self.assertEqual(report['status'], 'MISMATCH')
                    self.assertEqual(report['difference']['field'], f'{dsp}.native')
                    self.assertEqual(report['difference']['component'],
                                     'interrupt.pending_bits' if engine == 'jit'
                                     else 'core.interrupt_is_pending')

    def test_different_initial_state_is_not_called_execution_divergence(self):
        run = subprocess.run([sys.executable, str(WRAPPER), 'verify-engines',
                              '--worker', WORKER, '--fixture', 'scratch', '--frames', '1',
                              '--value', '0', '--inject-initial-state-fault'],
                             capture_output=True, text=True, timeout=15)
        self.assertNotEqual(run.returncode, 0)
        report = json.loads(run.stdout)
        self.assertEqual((report['status'], report['reason']),
                         ('UNSUPPORTED', 'INITIAL_STATE_MISMATCH'))
        self.assertEqual(report['initial_difference']['field'], 'gp.arch.register')

    def test_matching_engines_still_require_fixture_output(self):
        for option, field in (('--inject-both-skip-dma', 'fixture.io_event_count'),
                              ('--inject-both-pcm-silence', 'fixture.pcm'),
                              ('--inject-both-corrupt-event', 'fixture.io_event'),
                              ('--inject-both-corrupt-ram', 'fixture.ram')):
            run = subprocess.run([sys.executable, str(WRAPPER), 'verify-engines',
                                  '--worker', WORKER, '--fixture', 'scratch', '--frames', '1',
                                  '--value', '1193046', option],
                                 capture_output=True, text=True, timeout=15)
            self.assertNotEqual(run.returncode, 0)
            report = json.loads(run.stdout)
            self.assertEqual(report['status'], 'MISMATCH')
            self.assertEqual(report['difference']['field'], field)

    @unittest.skipIf(os.name == 'nt', 'executable script fixture is POSIX')
    def test_ignored_fault_injection_cannot_be_reported_as_match(self):
        worker = self.root / 'ignores-faults'
        worker.write_text("#!/usr/bin/env python3\n"
                          "print('{\"status\":\"BASELINE_MATCH\",\"observations\":{"
                          "\"io_events\":2,\"pcm_nonzero_comparisons\":2,"
                          "\"dsp_memory_word_comparisons\":1}}')\n")
        worker.chmod(0o700)
        run = subprocess.run([sys.executable, str(WRAPPER), 'verify-engines',
                              '--worker', str(worker), '--fixture', 'scratch', '--frames', '1',
                              '--value', '0', '--inject-fault'],
                             capture_output=True, text=True, timeout=10)
        self.assertNotEqual(run.returncode, 0)
        self.assertEqual(json.loads(run.stdout)['status'], 'SELF_TEST_FAILED')

    @unittest.skipIf(os.name == 'nt', 'executable script fixture is POSIX')
    def test_engine_comparison_watchdog_stops_a_hung_worker(self):
        worker = self.root / 'hang-comparator'
        worker.write_text('#!/usr/bin/env python3\nimport time\ntime.sleep(10)\n')
        worker.chmod(0o700)
        run = subprocess.run([sys.executable, str(WRAPPER), 'verify-engines',
                              '--worker', str(worker), '--fixture', 'scratch', '--frames', '1',
                              '--value', '0', '--timeout', '0.1'],
                             capture_output=True, text=True, timeout=5)
        self.assertNotEqual(run.returncode, 0)
        self.assertEqual(json.loads(run.stdout)['status'], 'TIMEOUT')

    def test_synthetic_comparator_runs_each_engine_against_itself(self):
        for engine in ('jit', 'interpreter'):
            run = subprocess.run([sys.executable, str(WRAPPER), 'verify-engines',
                                  '--worker', WORKER, '--fixture', 'scratch', '--frames', '9',
                                  '--value', '0', '--reference', engine, '--candidate', engine,
                                  '--repeat', '2'], capture_output=True, text=True, timeout=15)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            report = json.loads(run.stdout)
            self.assertEqual(report['engines'], [engine, engine])
            self.assertEqual(report['completed'], 2)
            self.assertEqual(report['observations']['native_checkpoint_comparisons'], 20)

    def test_baseline_match_in_separate_process(self):
        run = self.run_wrapper("--repeat", "2")
        self.assertEqual(run.returncode, 0, run.stderr)
        result = json.loads(run.stdout)
        self.assertEqual(result["status"], "BASELINE_MATCH")
        self.assertEqual(result["completed"], 2)
        self.assertEqual(result["scope"], "jit-core-no-external-io")
        self.assertEqual(result["instruction_limit"], 5)
        self.assertEqual(result["reference_authority"], "UNVERIFIED")
        if FALLBACK_ARCHIVE:
            self.assertEqual(result["linked_dsp_library"]["status"], "BUILD_RECORDED")
            self.assertEqual(result["linked_dsp_library"]["version"], "0.1.3")
            self.assertEqual(result["linked_dsp_library"]["sha256"],
                             hashlib.sha256(Path(FALLBACK_ARCHIVE).read_bytes()).hexdigest())
        else:
            self.assertEqual(result["linked_dsp_library"]["status"], "UNVERIFIED")
        self.assertEqual(len(result["identities"]["worker_sha256"]), 64)

    def test_first_state_difference_is_reported(self):
        data = bytearray(self.expected.read_bytes())
        data[8] = 1
        self.expected.write_bytes(data)
        run = self.run_wrapper()
        self.assertNotEqual(run.returncode, 0)
        result = json.loads(run.stdout)
        self.assertEqual(result["status"], "MISMATCH")
        self.assertEqual(result["difference"]["field"], "state.pc")

    def test_mapped_trace_reports_intermediate_state_before_final_match(self):
        gp = self.initial.read_bytes()
        ep = bytearray(gp)
        ep[-1] = 0
        files = {"gp-initial": gp, "ep-initial": ep, "gp-expected": gp,
                 "ep-expected": ep, "mixbins": bytes(4096 * 2),
                 "prior-pcm": bytes(1024), "expected-pcm": bytes(1024 * 2),
                 "apu-registers": bytes(0x80000),
                 "expected-apu-registers": bytes(0x80000),
                 "ram": bytes(4096), "expected-ram": bytes(4096),
                 "control": struct.pack("<4sIIBBBB", b"XMF1", 0, 0, 0, 0, 0, 0),
                 "events": b"XIO1\0\0\0\0", "gp-frame-states": gp * 2,
                 "ep-frame-states": bytes(ep) * 2}
        command = [sys.executable, str(WRAPPER), "verify-mapped-trace",
                   "--worker", WORKER, "--frames", "2"]
        for name, contents in files.items():
            path = self.root / (name + ".bin")
            path.write_bytes(contents)
            command.extend(["--" + name, str(path)])
        run = subprocess.run(command, capture_output=True, text=True, timeout=10)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        altered = bytearray(gp * 2)
        struct.pack_into("<I", altered, 8, 1)
        (self.root / "gp-frame-states.bin").write_bytes(altered)
        run = subprocess.run(command, capture_output=True, text=True, timeout=10)
        self.assertNotEqual(run.returncode, 0)
        report = json.loads(run.stdout)
        self.assertEqual(report["status"], "MISMATCH")
        self.assertEqual(report["failure_frame"], 0)
        self.assertEqual(report["difference"]["field"], "gp.state.pc")
        altered = bytearray(gp * 2)
        struct.pack_into("<I", altered, len(gp) + 8, 1)
        (self.root / "gp-frame-states.bin").write_bytes(altered)
        pcm = bytearray(1024 * 2)
        struct.pack_into("<h", pcm, 0, 1)
        (self.root / "expected-pcm.bin").write_bytes(pcm)
        run = subprocess.run(command, capture_output=True, text=True, timeout=10)
        report = json.loads(run.stdout)
        self.assertEqual((report["failure_frame"], report["difference"]["field"]),
                         (0, "pcm"))
        (self.root / "expected-pcm.bin").write_bytes(bytes(1024 * 2))
        (self.root / "gp-frame-states.bin").write_bytes(gp * 2)
        altered_ep = bytearray(bytes(ep) * 2)
        struct.pack_into("<I", altered_ep, len(ep) + 8, 1)
        (self.root / "ep-frame-states.bin").write_bytes(altered_ep)
        run = subprocess.run(command, capture_output=True, text=True, timeout=10)
        report = json.loads(run.stdout)
        self.assertEqual((report["failure_frame"], report["difference"]["field"]),
                         (1, "ep.state.pc"))
        (self.root / "ep-frame-states.bin").write_bytes(ep)
        run = subprocess.run(command, capture_output=True, text=True, timeout=10)
        self.assertNotEqual(run.returncode, 0)
        self.assertEqual(json.loads(run.stdout)["status"], "INCOMPLETE")

    @unittest.skipIf(os.name == "nt", "executable script fixture is POSIX")
    def test_repeated_match_then_mismatch_is_nondeterministic(self):
        worker = self.root / "varying-worker"
        counter = self.root / "counter"
        worker.write_text(
            "#!/usr/bin/env python3\n"
            "import json\nfrom pathlib import Path\n"
            f"counter = Path({str(counter)!r})\n"
            "if counter.exists():\n"
            "    print(json.dumps({'status': 'MISMATCH', 'difference': "
            "{'field': 'state.pc'}}))\n"
            "    raise SystemExit(1)\n"
            "counter.touch()\n"
            "print(json.dumps({'status': 'BASELINE_MATCH'}))\n"
        )
        worker.chmod(0o700)
        run = self.run_wrapper("--repeat", "2", worker=str(worker))
        self.assertNotEqual(run.returncode, 0)
        result = json.loads(run.stdout)
        self.assertEqual(result["status"], "NONDETERMINISTIC")
        self.assertEqual(result["completed"], 1)
        self.assertEqual(result["difference"]["field"], "state.pc")

    def test_halt_instruction_executes_in_child(self):
        initial = bytearray(self.initial.read_bytes())
        initial[26] = 0
        initial[25143:25147] = (0x08F484).to_bytes(4, "little")
        initial[25147:25151] = (1).to_bytes(4, "little")
        expected = bytearray(initial)
        expected[8:12] = (2).to_bytes(4, "little")
        expected[12:16] = (2).to_bytes(4, "little")
        expected[16:20] = (3).to_bytes(4, "little")
        expected[26] = 1
        self.initial.write_bytes(initial)
        self.expected.write_bytes(expected)
        run = self.run_wrapper()
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertEqual(json.loads(run.stdout)["status"], "BASELINE_MATCH")

    def test_step_limit_is_not_a_pass(self):
        data = bytearray(self.initial.read_bytes())
        data[26] = 0
        self.initial.write_bytes(data)
        run = self.run_wrapper()
        self.assertNotEqual(run.returncode, 0)
        self.assertEqual(json.loads(run.stdout)["status"], "TIMEOUT")

    def test_truncated_input_is_incomplete(self):
        self.initial.write_bytes(b"XDJT")
        run = self.run_wrapper()
        self.assertNotEqual(run.returncode, 0)
        self.assertEqual(json.loads(run.stdout)["status"], "INCOMPLETE")

    @unittest.skipIf(os.name == "nt", "executable script fixture is POSIX")
    def test_process_timeout_stops_hung_worker(self):
        worker = self.root / "hang"
        worker.write_text("#!/usr/bin/env python3\nimport time\ntime.sleep(10)\n")
        worker.chmod(0o700)
        run = self.run_wrapper("--timeout", "0.1", worker=str(worker))
        self.assertNotEqual(run.returncode, 0)
        self.assertEqual(json.loads(run.stdout)["status"], "TIMEOUT")

    def test_scratch_memory_is_compared(self):
        memory = self.root / "memory.bin"
        expected_memory = self.root / "expected-memory.bin"
        events = self.root / "events.bin"
        memory.write_bytes(b"\x11\x22\x33")
        expected_memory.write_bytes(b"\x11\x22\x33")
        events.write_bytes(b"XIO1\0\0\0\0")
        def invoke():
            return subprocess.run(
                [sys.executable, str(WRAPPER), "verify-scratch", "--worker", WORKER,
                 "--initial", str(self.initial), "--expected", str(self.expected),
                 "--steps", "5", "--memory", str(memory), "--expected-memory",
                 str(expected_memory), "--events", str(events)],
                capture_output=True, text=True, timeout=5)
        run = invoke()
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertEqual(json.loads(run.stdout)["scope"], "jit-core-scratch-io")
        expected_memory.write_bytes(b"\x11\x99\x33")
        run = invoke()
        self.assertNotEqual(run.returncode, 0)
        result = json.loads(run.stdout)
        self.assertEqual(result["status"], "MISMATCH")
        self.assertEqual(result["difference"]["field"], "scratch_memory")
        self.assertEqual(result["difference"]["index"], 1)
        events.write_bytes(b"XIO1\1\0\0\0")  # missing promised record
        run = invoke()
        self.assertNotEqual(run.returncode, 0)
        self.assertEqual(json.loads(run.stdout)["status"], "INCOMPLETE")

    def test_scratch_dma_instruction_in_separate_process(self):
        state = bytearray(self.initial.read_bytes())
        state[26] = 0
        def put32(buf, offset, value):
            buf[offset:offset + 4] = value.to_bytes(4, "little")
        for index, value in enumerate((0x4000, 0x19e2, 1, 0x200, 4, 8, 31)):
            put32(state, 567 + (0x100 + index) * 4, value)
        put32(state, 567 + 0x200 * 4, 0x123456)
        for index, value in enumerate((0x08f496, 1, 0x08f484, 1)):
            put32(state, 25143 + index * 4, value)
        put32(state, 41539, 0x100)  # DMA next block
        expected = bytearray(state)
        put32(expected, 8, 4)      # PC after two two-word instructions
        put32(expected, 12, 2)     # last instruction length
        put32(expected, 16, 6)     # instruction cycles
        expected[26] = 1           # halted
        put32(expected, 41531, 0x10)  # DMA running until status reads
        put32(expected, 41539, 0x4000)
        expected[41552] = 1         # DMA end of list
        self.initial.write_bytes(state)
        self.expected.write_bytes(expected)
        memory = self.root / "memory.bin"
        expected_memory = self.root / "expected-memory.bin"
        events = self.root / "events.bin"
        memory.write_bytes(bytes(64))
        expected_ram = bytearray(64)
        put32(expected_ram, 12, 0x123456)
        expected_memory.write_bytes(expected_ram)
        record = struct.pack("<BBHIIQI", 0, 1, 0, 0, 12, 0, 4)
        events.write_bytes(b"XIO1\1\0\0\0" + record + b"\x56\x34\x12\0")
        def invoke():
            return subprocess.run(
                [sys.executable, str(WRAPPER), "verify-scratch", "--worker", WORKER,
                 "--initial", str(self.initial), "--expected", str(self.expected),
                 "--steps", "5", "--memory", str(memory), "--expected-memory",
                 str(expected_memory), "--events", str(events)],
                capture_output=True, text=True, timeout=5)
        run = invoke()
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        altered = bytearray(events.read_bytes())
        altered[-1] ^= 1
        events.write_bytes(altered)
        run = invoke()
        self.assertNotEqual(run.returncode, 0)
        result = json.loads(run.stdout)
        self.assertEqual(result["status"], "MISMATCH")
        self.assertEqual(result["difference"]["field"], "io_event")
        self.assertEqual(result["difference"]["index"], 0)
        events.write_bytes(b"XIO1\0\0\0\0")
        run = invoke()
        self.assertNotEqual(run.returncode, 0)
        self.assertEqual(json.loads(run.stdout)["status"], "INCOMPLETE")
        # The independent memory image must match a recorded scratch read.
        put32(state, 567 + 0x101 * 4, 0x19e0)
        self.initial.write_bytes(state)
        recorded_read = struct.pack("<BBHIIQI", 0, 0, 0, 0, 12, 0, 4)
        events.write_bytes(b"XIO1\1\0\0\0" + recorded_read + b"\x56\x34\x12\0")
        run = invoke()  # memory at 12 is zero, unlike the recorded read
        self.assertNotEqual(run.returncode, 0)
        result = json.loads(run.stdout)
        self.assertEqual(result["status"], "MISMATCH")
        self.assertEqual(result["difference"]["field"], "io_event")
        self.assertEqual(result["difference"]["index"], 0)

    def test_gp_frame_pcm_in_separate_process(self):
        gp = bytearray(self.initial.read_bytes())
        gp[26] = 0
        gp[25143:25147] = (0x08f484).to_bytes(4, "little")
        gp[25147:25151] = (1).to_bytes(4, "little")
        ep = bytearray(gp)
        ep[-1] = 0
        expected_gp = bytearray(gp)
        expected_gp[8:12] = (2).to_bytes(4, "little")
        expected_gp[12:16] = (2).to_bytes(4, "little")
        expected_gp[16:20] = (3).to_bytes(4, "little")
        expected_gp[20:24] = (997).to_bytes(4, "little")
        expected_gp[26] = 1
        expected_gp[567 + 0xc00 * 4:571 + 0xc00 * 4] = (0x400000).to_bytes(4, "little")
        expected_gp[567 + 0xc20 * 4:571 + 0xc20 * 4] = (0xc00000).to_bytes(4, "little")
        expected_gp[41547:41551] = (2).to_bytes(4, "little")
        mix = [0.0] * 1024
        mix[0], mix[32] = 0.5, -0.5
        pcm = bytearray(1024)
        struct.pack_into("<hh", pcm, 7 * 32 * 4, 16384, -16384)
        files = {
            "gp-initial": gp, "ep-initial": ep, "gp-expected": expected_gp,
            "ep-expected": ep, "mixbins": struct.pack("<1024f", *mix),
            "prior-pcm": bytes(1024), "expected-pcm": pcm,
            "gp-memory": b"", "ep-memory": b"", "gp-expected-memory": b"",
            "ep-expected-memory": b"", "events": b"XIO1\0\0\0\0",
        }
        command = [sys.executable, str(WRAPPER), "verify-frame", "--worker", WORKER,
                   "--phase", "7"]
        for name, content in files.items():
            path = self.root / (name + ".bin")
            path.write_bytes(content)
            command.extend(["--" + name, str(path)])
        run = subprocess.run(command, capture_output=True, text=True, timeout=5)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        report = json.loads(run.stdout)
        self.assertEqual(report["scope"], "gp-ep-frame-gp-monitor")
        self.assertEqual(report["phase"], 7)
        mapped_files = {"apu-registers": bytes(0x80000),
                        "expected-apu-registers": bytes(0x80000),
                        "ram": bytes(0x10000), "expected-ram": bytes(0x10000),
                        "control": struct.pack("<4sIIBBBB", b"XMF1", 3, 0,
                                               2, 7, 0, 0)}
        mapped_command = [sys.executable, str(WRAPPER), "verify-mapped-frame",
                          "--worker", WORKER]
        mapped_names = ("gp-initial", "ep-initial", "gp-expected", "ep-expected",
                        "mixbins", "prior-pcm", "expected-pcm", *mapped_files)
        for name, content in mapped_files.items():
            (self.root / (name + ".bin")).write_bytes(content)
        for name in mapped_names:
            mapped_command.extend(["--" + name, str(self.root / (name + ".bin"))])
        run = subprocess.run(mapped_command, capture_output=True, text=True, timeout=10)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertEqual(json.loads(run.stdout)["scope"], "gp-ep-mapped-frame")
        mapped_files["expected-ram"] = b"\x01" + bytes(0xffff)
        (self.root / "expected-ram.bin").write_bytes(mapped_files["expected-ram"])
        run = subprocess.run(mapped_command, capture_output=True, text=True, timeout=10)
        self.assertNotEqual(run.returncode, 0)
        self.assertEqual(json.loads(run.stdout)["difference"]["field"], "ram")
        # The first frame halts; the zero program following it does not halt
        # within the next production run. Report the failing subframe.
        mapped_sequence = mapped_command.copy()
        mapped_sequence[2] = "verify-mapped-sequence"
        mapped_sequence.extend(["--frames", "2"])
        (self.root / "mixbins.bin").write_bytes(struct.pack("<1024f", *mix) * 2)
        (self.root / "expected-pcm.bin").write_bytes(bytes(2048))
        run = subprocess.run(mapped_sequence, capture_output=True, text=True, timeout=10)
        self.assertNotEqual(run.returncode, 0)
        report = json.loads(run.stdout)
        self.assertEqual(report["status"], "TIMEOUT")
        self.assertEqual(report["failure_frame"], 1)
        (self.root / "mixbins.bin").write_bytes(struct.pack("<1024f", *mix))
        (self.root / "expected-pcm.bin").write_bytes(pcm)
        for name in ("ram", "expected-ram"):
            with (self.root / (name + ".bin")).open("wb") as stream:
                stream.truncate(64 * 1024 * 1024)
        run = subprocess.run(mapped_command, capture_output=True, text=True, timeout=20)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertEqual(json.loads(run.stdout)["status"], "BASELINE_MATCH")
        sequence = command.copy()
        sequence[2] = "verify-sequence"
        sequence.extend(["--frames", "1"])
        sequence_pcm = pcm[7 * 32 * 4:8 * 32 * 4]
        (self.root / "expected-pcm.bin").write_bytes(sequence_pcm)
        (self.root / "events.bin").write_bytes(
            b"XIS1" + struct.pack("<II", 1, 8) + b"XIO1\0\0\0\0")
        run = subprocess.run(sequence, capture_output=True, text=True, timeout=5)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertEqual(json.loads(run.stdout)["scope"], "gp-ep-sequence-gp-monitor")
        sequence_pcm[0] ^= 1
        (self.root / "expected-pcm.bin").write_bytes(sequence_pcm)
        run = subprocess.run(sequence, capture_output=True, text=True, timeout=5)
        self.assertNotEqual(run.returncode, 0)
        difference = json.loads(run.stdout)["difference"]
        self.assertEqual((difference["field"], difference["sample"],
                          difference["channel"]), ("pcm", 0, 0))
        (self.root / "events.bin").write_bytes(b"XIS1" + struct.pack("<II", 1, 9) + b"XIO1")
        run = subprocess.run(sequence, capture_output=True, text=True, timeout=5)
        self.assertNotEqual(run.returncode, 0)
        self.assertEqual(json.loads(run.stdout)["status"], "INCOMPLETE")
        # Two frames cross from phase 7 into phase 0, starting EP once.
        for state in (gp, ep, expected_gp):
            struct.pack_into("<I", state, 25151, 0x0c0000)  # branch to halt
        expected_sequence_gp = bytearray(expected_gp)
        struct.pack_into("<II", expected_sequence_gp, 16, 6, 1991)
        expected_sequence_ep = bytearray(ep)
        for offset, value in ((8, 2), (12, 2), (16, 3), (20, 997), (41547, 2)):
            struct.pack_into("<I", expected_sequence_ep, offset, value)
        expected_sequence_ep[26] = 1
        for name, content in (("gp-initial", gp), ("ep-initial", ep),
                              ("gp-expected", expected_sequence_gp),
                              ("ep-expected", expected_sequence_ep)):
            (self.root / (name + ".bin")).write_bytes(content)
        (self.root / "mixbins.bin").write_bytes(struct.pack("<1024f", *mix) * 2)
        sequence_pcm = pcm[7 * 32 * 4:8 * 32 * 4] * 2
        (self.root / "expected-pcm.bin").write_bytes(sequence_pcm)
        (self.root / "events.bin").write_bytes(
            b"XIS1" + struct.pack("<I", 2) +
            (struct.pack("<I", 8) + b"XIO1\0\0\0\0") * 2)
        sequence[-1] = "2"
        run = subprocess.run(sequence, capture_output=True, text=True, timeout=5)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        sequence_pcm[128] ^= 1
        (self.root / "expected-pcm.bin").write_bytes(sequence_pcm)
        run = subprocess.run(sequence, capture_output=True, text=True, timeout=5)
        self.assertNotEqual(run.returncode, 0)
        difference = json.loads(run.stdout)["difference"]
        self.assertEqual((difference["field"], difference["sample"],
                          difference["channel"]), ("pcm", 32, 0))
        # Carry the same restartable program through 1,024 GP subframes and
        # 128 EP frames, checking the full sequence output and final states.
        struct.pack_into("<I", expected_sequence_gp, 20, 997 + 1023 * 994)
        struct.pack_into("<II", expected_sequence_ep, 16, 6, 997 + 127 * 994)
        (self.root / "gp-expected.bin").write_bytes(expected_sequence_gp)
        (self.root / "ep-expected.bin").write_bytes(expected_sequence_ep)
        (self.root / "mixbins.bin").write_bytes(struct.pack("<1024f", *mix) * 1024)
        (self.root / "expected-pcm.bin").write_bytes(pcm[7 * 32 * 4:8 * 32 * 4] * 1024)
        (self.root / "events.bin").write_bytes(
            b"XIS1" + struct.pack("<I", 1024) +
            (struct.pack("<I", 8) + b"XIO1\0\0\0\0") * 1024)
        sequence[-1] = "1024"
        run = subprocess.run(sequence, capture_output=True, text=True, timeout=10)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertEqual(json.loads(run.stdout)["frames"], 1024)
        # Restore the original one-frame fixture for the following checks.
        for state in (gp, ep, expected_gp):
            struct.pack_into("<I", state, 25151, 0)
        for name, content in (("gp-initial", gp), ("ep-initial", ep),
                              ("gp-expected", expected_gp), ("ep-expected", ep)):
            (self.root / (name + ".bin")).write_bytes(content)
        (self.root / "mixbins.bin").write_bytes(struct.pack("<1024f", *mix))
        (self.root / "events.bin").write_bytes(b"XIO1\0\0\0\0")
        pcm[7 * 32 * 4] ^= 1
        (self.root / "expected-pcm.bin").write_bytes(pcm)
        run = subprocess.run(command, capture_output=True, text=True, timeout=5)
        self.assertNotEqual(run.returncode, 0)
        result = json.loads(run.stdout)
        self.assertEqual(result["status"], "MISMATCH")
        self.assertEqual(result["difference"]["field"], "pcm")
        self.assertEqual(result["difference"]["index"], 7 * 32 * 2)
        self.assertEqual(result["difference"]["sample"], 7 * 32)
        self.assertEqual(result["difference"]["channel"], 0)
        # At phase zero the EP runs too; its independent state must match.
        expected_ep = bytearray(ep)
        expected_ep[8:12] = (2).to_bytes(4, "little")
        expected_ep[12:16] = (2).to_bytes(4, "little")
        expected_ep[16:20] = (3).to_bytes(4, "little")
        expected_ep[20:24] = (997).to_bytes(4, "little")
        expected_ep[26] = 1
        expected_ep[41547:41551] = (2).to_bytes(4, "little")
        (self.root / "ep-expected.bin").write_bytes(expected_ep)
        pcm = bytearray(1024)
        struct.pack_into("<hh", pcm, 0, 16384, -16384)
        (self.root / "expected-pcm.bin").write_bytes(pcm)
        command[command.index("--phase") + 1] = "0"
        run = subprocess.run(command, capture_output=True, text=True, timeout=5)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        expected_ep[8] ^= 1
        (self.root / "ep-expected.bin").write_bytes(expected_ep)
        run = subprocess.run(command, capture_output=True, text=True, timeout=5)
        self.assertNotEqual(run.returncode, 0)
        result = json.loads(run.stdout)
        self.assertEqual(result["status"], "MISMATCH")
        self.assertEqual(result["difference"]["field"], "ep.state.pc")

    def test_mapped_ep_fifo_sink_in_separate_process(self):
        gp = bytearray(self.initial.read_bytes())
        ep = bytearray(gp)
        ep[-1] = 0
        ep[26] = 0
        for index, value in enumerate((0x4000, 0x402, 512, 0x200, 0, 0, 0)):
            struct.pack_into("<I", ep, 567 + (0x100 + index) * 4, value)
        for index in range(512):
            struct.pack_into("<I", ep, 567 + (0x200 + index) * 4,
                             0xe00000 if index & 1 else 0x200000)
        for index, value in enumerate((0x08f496, 1, 0x08f484, 1)):
            struct.pack_into("<I", ep, 25143 + index * 4, value)
        struct.pack_into("<I", ep, 41539, 0x100)
        expected_ep = bytearray(ep)
        for offset, value in ((8, 4), (12, 2), (16, 6), (20, 994),
                              (41531, 0x10), (41539, 0x4000), (41547, 2)):
            struct.pack_into("<I", expected_ep, offset, value)
        expected_ep[26] = expected_ep[41552] = 1
        regs = bytearray(0x80000)
        struct.pack_into("<I", regs, 0x204c * 4, 0x1000)
        struct.pack_into("<I", regs, 0x4028 * 4, 1024)
        ram = bytearray(0x10000)
        struct.pack_into("<I", ram, 0x1000, 0x4000)
        ram[0x4000:0x4400] = b"\x77" * 1024
        expected_ram = bytearray(ram)
        expected_ram[0x4000:0x4400] = bytes(1024)
        pcm = struct.pack("<hh", 8192, -8192) * 256
        files = {"gp-initial": gp, "ep-initial": ep, "gp-expected": gp,
                 "ep-expected": expected_ep, "mixbins": bytes(4096),
                 "prior-pcm": bytes(1024), "expected-pcm": pcm,
                 "apu-registers": regs, "expected-apu-registers": regs,
                 "ram": ram, "expected-ram": expected_ram,
                 "control": struct.pack("<4sIIBBBB", b"XMF1", 0, 3, 3, 0, 0, 0)}
        command = [sys.executable, str(WRAPPER), "verify-mapped-frame", "--worker", WORKER]
        for name, contents in files.items():
            path = self.root / (name + ".bin")
            path.write_bytes(contents)
            command.extend(["--" + name, str(path)])
        run = subprocess.run(command, capture_output=True, text=True, timeout=10)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertEqual(json.loads(run.stdout)["status"], "BASELINE_MATCH")
        sequence = command.copy()
        sequence[2] = "verify-mapped-sequence"
        sequence.extend(["--frames", "2"])
        (self.root / "mixbins.bin").write_bytes(bytes(8192))
        (self.root / "expected-pcm.bin").write_bytes(pcm * 2)
        run = subprocess.run(sequence, capture_output=True, text=True, timeout=10)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        report = json.loads(run.stdout)
        self.assertEqual((report["scope"], report["frames"]),
                         ("gp-ep-mapped-sequence", 2))
        altered_sequence = bytearray(pcm * 2)
        altered_sequence[1024 + 4] ^= 1
        (self.root / "expected-pcm.bin").write_bytes(altered_sequence)
        run = subprocess.run(sequence, capture_output=True, text=True, timeout=10)
        self.assertNotEqual(run.returncode, 0)
        difference = json.loads(run.stdout)["difference"]
        self.assertEqual((difference["field"], difference["frame"],
                          difference["sample"], difference["channel"]),
                         ("pcm", 1, 1, 0))
        (self.root / "mixbins.bin").write_bytes(bytes(4096))
        altered = bytearray(pcm)
        altered[4] ^= 1
        (self.root / "expected-pcm.bin").write_bytes(altered)
        run = subprocess.run(command, capture_output=True, text=True, timeout=10)
        self.assertNotEqual(run.returncode, 0)
        difference = json.loads(run.stdout)["difference"]
        self.assertEqual((difference["field"], difference["sample"],
                          difference["channel"]), ("pcm", 1, 0))

    def test_mapped_gp_fifo_sge_wrap_in_separate_process(self):
        gp = bytearray(self.initial.read_bytes())
        gp[26] = 0
        ep = bytearray(gp)
        ep[-1] = 0
        for index, value in enumerate((0x4000, 0x1802, 2, 0x200, 0, 0, 0)):
            struct.pack_into("<I", gp, 567 + (0x100 + index) * 4, value)
        for index, value in enumerate((0x123456, 0x654321)):
            struct.pack_into("<I", gp, 567 + (0x200 + index) * 4, value)
        for index, value in enumerate((0x08f496, 1, 0x08f484, 1)):
            struct.pack_into("<I", gp, 25143 + index * 4, value)
        struct.pack_into("<I", gp, 41539, 0x100)
        expected_gp = bytearray(gp)
        for offset, value in ((8, 4), (12, 2), (16, 6), (20, 994),
                              (41531, 0x10), (41539, 0x4000), (41547, 2)):
            struct.pack_into("<I", expected_gp, offset, value)
        expected_gp[26] = expected_gp[41552] = 1
        regs = bytearray(0x80000)
        for address, value in ((0x2044, 0x1000), (0x20d8, 1), (0x3024, 4092),
                               (0x3028, 4100), (0x302c, 4096)):
            struct.pack_into("<I", regs, address * 4, value)
        ram = bytearray(0x10000)
        struct.pack_into("<I", ram, 0x1000, 0x4000)
        struct.pack_into("<I", ram, 0x1008, 0x6000)
        expected_ram = bytearray(ram)
        struct.pack_into("<I", expected_ram, 0x6000, 0x123456)
        struct.pack_into("<I", expected_ram, 0x4ffc, 0x654321)
        files = {"gp-initial": gp, "ep-initial": ep, "gp-expected": expected_gp,
                 "ep-expected": ep, "mixbins": bytes(4096),
                 "prior-pcm": bytes(1024), "expected-pcm": bytes(1024),
                 "apu-registers": regs, "expected-apu-registers": regs,
                 "ram": ram, "expected-ram": expected_ram,
                 "control": struct.pack("<4sIIBBBB", b"XMF1", 3, 0, 2, 0, 0, 0)}
        command = [sys.executable, str(WRAPPER), "verify-mapped-frame", "--worker", WORKER]
        for name, contents in files.items():
            path = self.root / (name + ".bin")
            path.write_bytes(contents)
            command.extend(["--" + name, str(path)])
        run = subprocess.run(command, capture_output=True, text=True, timeout=10)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertEqual(json.loads(run.stdout)["status"], "BASELINE_MATCH")
        # After the FIFO transfer, each following GP frame branches back to
        # halt; the first frame's aliased RAM writes must remain present.
        struct.pack_into("<I", gp, 25143 + 4 * 4, 0x0c0002)
        struct.pack_into("<I", expected_gp, 25143 + 4 * 4, 0x0c0002)
        (self.root / "gp-initial.bin").write_bytes(gp)
        sequence = command.copy()
        sequence[2] = "verify-mapped-sequence"
        sequence.extend(["--frames", "8"])
        for count in (8, 128, 1024):
            struct.pack_into("<I", expected_gp, 20, 994 * count)
            (self.root / "gp-expected.bin").write_bytes(expected_gp)
            (self.root / "mixbins.bin").write_bytes(bytes(4096 * count))
            (self.root / "expected-pcm.bin").write_bytes(bytes(1024 * count))
            sequence[-1] = str(count)
            run = subprocess.run(sequence, capture_output=True, text=True, timeout=20)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertEqual(json.loads(run.stdout)["frames"], count)
        (self.root / "mixbins.bin").write_bytes(bytes(4096))
        (self.root / "expected-pcm.bin").write_bytes(bytes(1024))
        struct.pack_into("<I", expected_gp, 20, 994)
        (self.root / "gp-expected.bin").write_bytes(expected_gp)
        struct.pack_into("<I", expected_ram, 0x4ffc, 0)
        (self.root / "expected-ram.bin").write_bytes(expected_ram)
        run = subprocess.run(command, capture_output=True, text=True, timeout=10)
        self.assertNotEqual(run.returncode, 0)
        difference = json.loads(run.stdout)["difference"]
        self.assertEqual((difference["field"], difference["index"]), ("ram", 0x4ffc))

    def test_mapped_gp_write_is_visible_to_ep_in_same_frame(self):
        gp = bytearray(self.initial.read_bytes())
        gp[26] = 0
        ep = bytearray(gp)
        ep[-1] = 0
        for state, control in ((gp, 0x19e2), (ep, 0x19e0)):
            for index, value in enumerate((0x4000, control, 1, 0x200, 0, 0, 31)):
                struct.pack_into("<I", state, 567 + (0x100 + index) * 4, value)
            for index, value in enumerate((0x08f496, 1, 0x08f484, 1, 0x0c0002)):
                struct.pack_into("<I", state, 25143 + index * 4, value)
            struct.pack_into("<I", state, 41539, 0x100)
        struct.pack_into("<I", gp, 567 + 0x200 * 4, 0x123456)
        expected_gp, expected_ep = bytearray(gp), bytearray(ep)
        for state in (expected_gp, expected_ep):
            for offset, value in ((8, 4), (12, 2), (16, 6), (20, 994),
                                  (41531, 0x10), (41539, 0x4000), (41547, 2)):
                struct.pack_into("<I", state, offset, value)
            state[26] = state[41552] = 1
        struct.pack_into("<I", expected_ep, 567 + 0x200 * 4, 0x123456)
        regs = bytearray(0x80000)
        struct.pack_into("<I", regs, 0x2040 * 4, 0x1000)
        struct.pack_into("<I", regs, 0x2048 * 4, 0x1008)
        ram = bytearray(0x10000)
        struct.pack_into("<I", ram, 0x1000, 0x4000)
        struct.pack_into("<I", ram, 0x1008, 0x4000)
        expected_ram = bytearray(ram)
        struct.pack_into("<I", expected_ram, 0x4000, 0x123456)
        files = {"gp-initial": gp, "ep-initial": ep, "gp-expected": expected_gp,
                 "ep-expected": expected_ep, "mixbins": bytes(4096),
                 "prior-pcm": bytes(1024), "expected-pcm": bytes(1024),
                 "apu-registers": regs, "expected-apu-registers": regs,
                 "ram": ram, "expected-ram": expected_ram,
                 "control": struct.pack("<4sIIBBBB", b"XMF1", 3, 3, 2, 0, 0, 0)}
        command = [sys.executable, str(WRAPPER), "verify-mapped-frame", "--worker", WORKER]
        for name, contents in files.items():
            path = self.root / (name + ".bin")
            path.write_bytes(contents)
            command.extend(["--" + name, str(path)])
        run = subprocess.run(command, capture_output=True, text=True, timeout=10)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertEqual(json.loads(run.stdout)["status"], "BASELINE_MATCH")
        write_event = struct.pack("<BBHIIQI", 0, 1, 0, 0, 0, 0, 4) + b"\x56\x34\x12\0"
        read_event = struct.pack("<BBHIIQI", 0, 0, 0, 1, 0, 0, 4) + b"\x56\x34\x12\0"
        events = self.root / "mapped-events.bin"
        events.write_bytes(b"XIO1" + struct.pack("<I", 2) + write_event + read_event)
        trace = command.copy()
        trace[2] = "verify-mapped-trace"
        trace.extend(["--frames", "1", "--events", str(events)])
        run = subprocess.run(trace, capture_output=True, text=True, timeout=10)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertEqual(json.loads(run.stdout)["scope"], "gp-ep-mapped-trace")
        events.write_bytes(b"XIO1" + struct.pack("<I", 2) + read_event + write_event)
        run = subprocess.run(trace, capture_output=True, text=True, timeout=10)
        self.assertNotEqual(run.returncode, 0)
        report = json.loads(run.stdout)
        self.assertEqual((report["status"], report["difference"]["index"]), ("MISMATCH", 0))
        self.assertEqual(report["failure_frame"], 0)
        corrupted = bytearray(read_event)
        corrupted[-1] ^= 1
        events.write_bytes(b"XIO1" + struct.pack("<I", 2) + write_event + corrupted)
        run = subprocess.run(trace, capture_output=True, text=True, timeout=10)
        self.assertNotEqual(run.returncode, 0)
        report = json.loads(run.stdout)
        self.assertEqual((report["status"], report["difference"]["index"]), ("MISMATCH", 1))
        events.write_bytes(b"XIO1" + struct.pack("<I", 1) + write_event)
        run = subprocess.run(trace, capture_output=True, text=True, timeout=10)
        self.assertNotEqual(run.returncode, 0)
        self.assertEqual(json.loads(run.stdout)["status"], "INCOMPLETE")
        sequence = command.copy()
        sequence[2] = "verify-mapped-sequence"
        sequence.extend(["--frames", "8"])
        struct.pack_into("<I", expected_gp, 20, 994 * 8)
        (self.root / "gp-expected.bin").write_bytes(expected_gp)
        (self.root / "mixbins.bin").write_bytes(bytes(4096 * 8))
        (self.root / "expected-pcm.bin").write_bytes(bytes(1024 * 8))
        run = subprocess.run(sequence, capture_output=True, text=True, timeout=10)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertEqual(json.loads(run.stdout)["frames"], 8)

    @unittest.skipIf(os.name == "nt", "executable script fixture is POSIX")
    def test_changed_input_cannot_be_accepted(self):
        worker = self.root / "mutate"
        worker.write_text("#!/usr/bin/env python3\nimport pathlib, sys\n"
                          "pathlib.Path(sys.argv[1]).write_bytes(b'changed')\n"
                          "print('{\"status\":\"BASELINE_MATCH\"}')\n")
        worker.chmod(0o700)
        run = self.run_wrapper(worker=str(worker))
        self.assertNotEqual(run.returncode, 0)
        self.assertEqual(json.loads(run.stdout)["status"], "INCONCLUSIVE")

    @unittest.skipIf(os.name == "nt", "executable script fixture is POSIX")
    def test_invalid_worker_output_is_crash(self):
        worker = self.root / "invalid-output"
        worker.write_text("#!/usr/bin/env python3\nimport sys\n"
                          "sys.stdout.buffer.write(b'\\xff')\n")
        worker.chmod(0o700)
        run = self.run_wrapper(worker=str(worker))
        self.assertNotEqual(run.returncode, 0)
        self.assertEqual(json.loads(run.stdout)["status"], "CRASH")

    @unittest.skipIf(os.name == "nt", "executable script fixture is POSIX")
    def test_worker_exit_code_must_match_report(self):
        worker = self.root / "bad-exit"
        worker.write_text("#!/usr/bin/env python3\nimport sys\n"
                          "print('{\"status\":\"MISMATCH\"}')\n"
                          "sys.exit(123)\n")
        worker.chmod(0o700)
        run = self.run_wrapper(worker=str(worker))
        self.assertNotEqual(run.returncode, 0)
        self.assertEqual(json.loads(run.stdout)["status"], "CRASH")

    @unittest.skipIf(os.name == "nt", "POSIX resource limits required")
    def test_worker_receives_resource_limits(self):
        worker = self.root / "limits"
        worker.write_text("#!/usr/bin/env python3\nimport resource, sys\n"
                          "a = resource.getrlimit(resource.RLIMIT_AS)[0]\n"
                          "c = resource.getrlimit(resource.RLIMIT_CPU)[0]\n"
                          "f = resource.getrlimit(resource.RLIMIT_FSIZE)[0]\n"
                          "d = resource.getrlimit(resource.RLIMIT_CORE)[0]\n"
                          "assert a == 256 * 1024 * 1024 and c == 2\n"
                          "assert f == 65536 and d == 0\n"
                          "print('{\"status\":\"BASELINE_MATCH\"}')\n")
        worker.chmod(0o700)
        run = self.run_wrapper("--memory-mib", "256", "--cpu-seconds", "2",
                               worker=str(worker))
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        result = json.loads(run.stdout)
        self.assertEqual(result["status"], "BASELINE_MATCH")
        self.assertEqual(result["limits"]["memory_bytes"], 256 * 1024 * 1024)

    @unittest.skipIf(os.name == "nt", "POSIX resource limits required")
    def test_worker_output_is_bounded(self):
        worker = self.root / "output-flood"
        worker.write_text("#!/usr/bin/env python3\nimport sys\n"
                          "sys.stdout.write('x' * 1048576)\n")
        worker.chmod(0o700)
        run = self.run_wrapper(worker=str(worker))
        self.assertNotEqual(run.returncode, 0)
        self.assertEqual(json.loads(run.stdout)["status"], "CRASH")

    @unittest.skipIf(os.name == "nt", "POSIX resource limits required")
    def test_cpu_limit_is_timeout(self):
        worker = self.root / "cpu-loop"
        worker.write_text("#!/usr/bin/env python3\nwhile True:\n    pass\n")
        worker.chmod(0o700)
        run = self.run_wrapper("--cpu-seconds", "1", "--timeout", "4",
                               worker=str(worker))
        self.assertNotEqual(run.returncode, 0)
        self.assertEqual(json.loads(run.stdout)["status"], "TIMEOUT")

    @unittest.skipIf(os.name == "nt", "POSIX resource limits required")
    def test_worker_memory_limit_is_enforced(self):
        worker = self.root / "memory-hog"
        worker.write_text("#!/usr/bin/env python3\n"
                          "a = bytearray(256 * 1024 * 1024)\n"
                          "print('{\"status\":\"BASELINE_MATCH\"}')\n")
        worker.chmod(0o700)
        run = self.run_wrapper("--memory-mib", "128", worker=str(worker))
        self.assertNotEqual(run.returncode, 0)
        self.assertEqual(json.loads(run.stdout)["status"], "CRASH")

    def test_identity_generator_uses_declared_archive_version(self):
        archive = self.root / "library.a"
        output = self.root / "identity.h"
        archive.write_bytes(b"synthetic archive")
        generator = Path(__file__).with_name("gen-dsp-build-identity.py")
        subprocess.run([sys.executable, str(generator), str(output), str(archive),
                        "9.8.7"], check=True, timeout=5)
        header = output.read_text()
        self.assertIn('DSP_REPLAY_LIBRARY_VERSION "9.8.7"', header)
        self.assertIn(hashlib.sha256(b"synthetic archive").hexdigest(), header)


if __name__ == "__main__":
    unittest.main()

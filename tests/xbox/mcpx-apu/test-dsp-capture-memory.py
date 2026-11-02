#!/usr/bin/env python3
"""Check reconstruction of observed physical memory interactions."""

import unittest

from dsp_capture_memory import MemoryModel, ModelError, TransferCoverage


class CaptureMemoryTests(unittest.TestCase):
    def test_observations_and_writes_reconstruct_initial_and_final_bytes(self):
        model = MemoryModel(64)
        model.event(5, 8, b"\x20\0\0\0")  # descriptor read
        model.event(3, 32, b"abc")             # RAM read
        model.event(2, 32, b"abc")             # pre-write contents
        model.event(4, 32, b"xyz")             # physical write
        model.event(3, 32, b"xyz")
        model.finish()
        self.assertEqual(model.initial[8:12], b"\x20\0\0\0")
        self.assertEqual(model.initial[32:35], b"abc")
        self.assertEqual(model.current[32:35], b"xyz")
        self.assertEqual(model.known_count, 7)
        self.assertFalse(model.is_known(0))
        self.assertTrue(model.is_known(33))

    def test_changed_observation_is_not_silently_accepted(self):
        model = MemoryModel(16)
        model.event(3, 4, b"ab")
        with self.assertRaisesRegex(ModelError, "inconsistent"):
            model.event(3, 4, b"ac")

    def test_unpaired_or_out_of_bounds_writes_are_rejected(self):
        model = MemoryModel(16)
        with self.assertRaisesRegex(ModelError, "prewrite"):
            model.event(4, 0, b"x")
        model = MemoryModel(16)
        model.event(2, 0, b"x")
        with self.assertRaisesRegex(ModelError, "prewrite"):
            model.finish()
        model = MemoryModel(16)
        with self.assertRaisesRegex(ModelError, "bounds"):
            model.event(3, 15, b"xx")

    def test_no_physical_context_is_incomplete(self):
        model = MemoryModel(16)
        model.event(0, 0, b"logical callback")
        with self.assertRaisesRegex(ModelError, "no physical"):
            model.finish()

    def test_transfer_coverage_checks_both_directions(self):
        trace = TransferCoverage()
        trace.event(0, True, 4)
        trace.event(5, False, 4)
        trace.event(2, False, 4)
        trace.event(4, True, 4)
        trace.event(5, False, 4)
        trace.event(3, False, 4)
        trace.event(0, False, 4)
        trace.finish()
        self.assertEqual(trace.transfers, 2)

    def test_unmodeled_logical_transfer_is_rejected(self):
        trace = TransferCoverage()
        trace.event(0, True, 4)
        with self.assertRaisesRegex(ModelError, "transfer"):
            trace.finish()


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
"""Exercise private reconstruction from a production-written stream."""

import json
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

FIXTURE = sys.argv.pop(1)
TOOL = Path(__file__).with_name("dsp_capture_reconstruct.py")


class ReconstructionTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.source = self.root / "capture.xcr"
        self.output = self.root / "private-case"
        subprocess.run([FIXTURE, "--emit-fixture", str(self.source)], check=True, timeout=5)

    def run_tool(self, *extra):
        run = subprocess.run([sys.executable, str(TOOL), str(self.source),
                              "--ram-size", "8192", "--output", str(self.output), *extra],
                             capture_output=True, text=True, timeout=10)
        return run, json.loads(run.stdout)

    @staticmethod
    def frame_layout(content):
        header_size = int.from_bytes(content[8:12], "little")
        length = int.from_bytes(content[header_size + 4:header_size + 8], "little")
        return header_size, header_size + 40, length

    @staticmethod
    def resign_frame(content):
        header, data, length = ReconstructionTests.frame_layout(content)
        content[header + 8:header + 40] = hashlib.sha256(
            content[data:data + length]).digest()

    def test_creates_private_unverified_replay_inputs(self):
        run, report = self.run_tool()
        self.assertNotEqual(run.returncode, 0)
        self.assertEqual(report["status"], "INCOMPLETE")
        self.assertTrue(report["artifacts_created"])
        self.assertEqual(report["known_bytes"], 8)
        self.assertNotIn(str(self.root), run.stdout)
        self.assertEqual(os.stat(self.output).st_mode & 0o777, 0o700)
        initial = (self.output / "ram.bin").read_bytes()
        expected = (self.output / "expected-ram.bin").read_bytes()
        self.assertEqual(initial[0x1000:0x1004], b"\x01\x02\x03\x04")
        self.assertEqual(initial[0x1400:0x1404], bytes(4))
        self.assertEqual(expected[0x1400:0x1404], b"\x01\x02\x03\x04")
        self.assertEqual((self.output / "events.bin").read_bytes()[:8], b"XIO1\x01\0\0\0")
        self.assertEqual((self.output / "gp-frame-states.bin").read_bytes(),
                         (self.output / "gp-expected.bin").read_bytes())
        self.assertEqual((self.output / "ep-frame-states.bin").read_bytes(),
                         (self.output / "ep-expected.bin").read_bytes())
        self.assertEqual(os.stat(self.output / "ram.bin").st_mode & 0o777, 0o600)
        status = json.loads((self.output / "status.json").read_text())
        self.assertEqual(status["status"], "INCOMPLETE")
        self.assertEqual(status["stream_version"], 3)
        self.assertGreater(status["created_unix_usec"], 0)
        library = status["linked_dsp_library"]
        self.assertIn(library["status"], ("BUILD_RECORDED", "UNVERIFIED"))
        if library["status"] == "BUILD_RECORDED":
            self.assertRegex(library["sha256"], r"^[0-9a-f]{64}$")
        else:
            self.assertEqual(library, {"status": "UNVERIFIED"})

    def test_existing_output_is_never_overwritten(self):
        self.output.mkdir()
        sentinel = self.output / "keep"
        sentinel.write_text("original")
        _, report = self.run_tool()
        self.assertEqual(report["status"], "INVALID")
        self.assertEqual(sentinel.read_text(), "original")

    def test_corrupt_capture_produces_no_output(self):
        data = bytearray(self.source.read_bytes())
        _, frame, _ = self.frame_layout(data)
        data[frame + 12] ^= 1
        self.source.write_bytes(data)
        _, report = self.run_tool()
        self.assertEqual(report["status"], "INVALID")
        self.assertFalse(self.output.exists())

    def test_nonram_descriptor_has_no_ram_only_reconstruction(self):
        data = bytearray(self.source.read_bytes())
        _, frame, _ = self.frame_layout(data)
        offset = frame + 4 + 8
        flags = int.from_bytes(data[offset:offset + 4], "little")
        data[offset:offset + 4] = (flags | 0x80).to_bytes(4, "little")
        self.resign_frame(data)
        self.source.write_bytes(data)
        _, report = self.run_tool()
        self.assertEqual(report["status"], "INCOMPLETE")
        self.assertEqual(report["mapping_check"], "NONRAM_DETECTED")
        self.assertFalse(self.output.exists())

    def test_legacy_mapping_needs_explicit_unverified_opt_in(self):
        data = bytearray(self.source.read_bytes())
        data = data[:8] + data[304:]
        data[4:8] = (1).to_bytes(4, "little")
        offset = 48 + 4 + 8
        flags = int.from_bytes(data[offset:offset + 4], "little")
        data[offset:offset + 4] = (flags & ~0x100).to_bytes(4, "little")
        length = int.from_bytes(data[12:16], "little")
        data[16:48] = hashlib.sha256(data[48:48 + length]).digest()
        self.source.write_bytes(data)
        _, report = self.run_tool()
        self.assertEqual(report["status"], "INCOMPLETE")
        self.assertFalse(report["artifacts_created"])
        self.assertEqual(report["mapping_check"], "NOT_RECORDED")
        self.assertFalse(self.output.exists())
        _, report = self.run_tool("--allow-legacy-unverified")
        self.assertTrue(report["artifacts_created"])
        self.assertEqual(report["mapping_check"], "NOT_RECORDED")

    def test_new_stream_missing_mapping_marker_is_incomplete(self):
        data = bytearray(self.source.read_bytes())
        _, frame, _ = self.frame_layout(data)
        offset = frame + 4 + 8
        flags = int.from_bytes(data[offset:offset + 4], "little")
        data[offset:offset + 4] = (flags & ~0x100).to_bytes(4, "little")
        self.resign_frame(data)
        self.source.write_bytes(data)
        _, report = self.run_tool()
        self.assertEqual(report["status"], "INCOMPLETE")
        self.assertFalse(report["artifacts_created"])
        self.assertEqual(report["mapping_check"], "INCOMPLETE")


if __name__ == "__main__":
    unittest.main()

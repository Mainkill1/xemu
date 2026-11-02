#!/usr/bin/env python3
"""Inspect streams emitted by the production serializer."""

import json
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

FIXTURE = sys.argv.pop(1)
SOURCE = Path(__file__).with_name("dsp_capture_stream.py")


class CaptureStreamTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.path = Path(self.tmp.name) / "capture.xcr"
        subprocess.run([FIXTURE, "--emit-fixture", str(self.path)], check=True, timeout=5)

    def inspect(self, *args):
        result = subprocess.run([sys.executable, str(SOURCE), str(self.path), *args],
                                capture_output=True, text=True, timeout=5)
        return result, json.loads(result.stdout)

    @staticmethod
    def frame_layout(content):
        header_size = int.from_bytes(content[8:12], "little")
        length = int.from_bytes(content[header_size + 4:header_size + 8], "little")
        return header_size, header_size + 40, length

    @staticmethod
    def resign_frame(content):
        header, data, length = CaptureStreamTests.frame_layout(content)
        content[header + 8:header + 40] = hashlib.sha256(
            content[data:data + length]).digest()

    def test_valid_stream_remains_incomplete(self):
        result, report = self.inspect()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(report["status"], "INCOMPLETE")
        self.assertEqual(report["frames"], 1)
        self.assertTrue(report["footer_present"])
        self.assertEqual(report["events"], 4)
        self.assertTrue(report["external_unverified"])
        self.assertEqual(report["stream_version"], 3)
        self.assertGreater(report["created_unix_usec"], 0)
        self.assertRegex(report["xemu_commit"], r"^[0-9a-f]{40}$")
        self.assertTrue(report["xemu_version"])
        library = report["linked_dsp_library"]
        self.assertIn(library["status"], ("BUILD_RECORDED", "UNVERIFIED"))
        if library["status"] == "BUILD_RECORDED":
            self.assertRegex(library["sha256"], r"^[0-9a-f]{64}$")
        else:
            self.assertEqual(library, {"status": "UNVERIFIED"})
        self.assertNotIn(str(self.path), result.stdout)

    def test_physical_model_does_not_claim_complete_ram(self):
        _, report = self.inspect("--ram-size", "8192")
        self.assertEqual(report["status"], "INCOMPLETE")
        self.assertEqual(report["memory_model"], "CONSISTENT_UNVERIFIED")
        self.assertEqual(report["known_bytes"], 8)
        self.assertEqual(report["unknown_bytes"], 8184)

    def test_payload_corruption_is_invalid(self):
        content = bytearray(self.path.read_bytes())
        _, data, _ = self.frame_layout(content)
        content[data + 12] ^= 1
        self.path.write_bytes(content)
        _, report = self.inspect()
        self.assertEqual(report["status"], "INVALID")

    def test_truncated_and_trailing_data_are_not_accepted(self):
        original = self.path.read_bytes()
        self.path.write_bytes(original[:-5])
        _, report = self.inspect()
        self.assertEqual(report["status"], "INCOMPLETE")
        self.path.write_bytes(original + b"x")
        _, report = self.inspect()
        self.assertEqual(report["status"], "INVALID")

    def test_valid_digest_does_not_hide_bad_event(self):
        content = bytearray(self.path.read_bytes())
        _, data, _ = self.frame_layout(content)
        fixed = 40 + 4 * 41554 + 2 * 0x80000 + 4096 + 2 * 1024
        content[data + fixed + 8] = 9  # invalid event kind
        self.resign_frame(content)
        self.path.write_bytes(content)
        _, report = self.inspect()
        self.assertEqual(report["status"], "INVALID")

    def test_footer_count_must_match(self):
        content = bytearray(self.path.read_bytes())
        content[-12:-8] = (2).to_bytes(4, "little")
        self.path.write_bytes(content)
        _, report = self.inspect()
        self.assertEqual(report["status"], "INVALID")

    def test_unknown_capture_flags_are_not_silently_accepted(self):
        content = bytearray(self.path.read_bytes())
        _, data, _ = self.frame_layout(content)
        flags_offset = data + 4 + 8
        flags = int.from_bytes(content[flags_offset:flags_offset + 4], "little")
        content[flags_offset:flags_offset + 4] = (flags | (1 << 31)).to_bytes(4, "little")
        self.resign_frame(content)
        self.path.write_bytes(content)
        _, report = self.inspect()
        self.assertEqual(report["status"], "INVALID")

    def test_nonram_descriptor_is_not_modeled_as_plain_ram(self):
        content = bytearray(self.path.read_bytes())
        _, data, _ = self.frame_layout(content)
        flags_offset = data + 4 + 8
        flags = int.from_bytes(content[flags_offset:flags_offset + 4], "little")
        content[flags_offset:flags_offset + 4] = (flags | 0x80).to_bytes(4, "little")
        self.resign_frame(content)
        self.path.write_bytes(content)
        _, report = self.inspect("--ram-size", "8192")
        self.assertEqual(report["status"], "INCOMPLETE")
        self.assertEqual(report["memory_model"], "INCOMPLETE")
        self.assertEqual(report["mapping_check"], "NONRAM_DETECTED")

    def test_missing_mapping_marker_is_reported_without_a_memory_model(self):
        content = bytearray(self.path.read_bytes())
        _, data, _ = self.frame_layout(content)
        flags_offset = data + 4 + 8
        flags = int.from_bytes(content[flags_offset:flags_offset + 4], "little")
        content[flags_offset:flags_offset + 4] = (flags & ~0x100).to_bytes(4, "little")
        self.resign_frame(content)
        self.path.write_bytes(content)
        _, report = self.inspect()
        self.assertEqual(report["status"], "INCOMPLETE")
        self.assertEqual(report["mapping_check"], "INCOMPLETE")

    def test_legacy_logical_only_transcript_remains_readable(self):
        content = bytearray(self.path.read_bytes())
        content = content[:8] + content[304:]
        content[4:8] = (1).to_bytes(4, "little")
        flags_offset = 48 + 4 + 8
        flags = int.from_bytes(content[flags_offset:flags_offset + 4], "little")
        content[flags_offset:flags_offset + 4] = (flags & ~0x100).to_bytes(4, "little")
        fixed = 40 + 4 * 41554 + 2 * 0x80000 + 4096 + 2 * 1024
        content[48 + fixed:48 + fixed + 4] = b"XIO1"
        for index in range(1, 4):
            content[48 + fixed + 8 + index * 28] = 1  # legacy FIFO kinds
        length = int.from_bytes(content[12:16], "little")
        content[16:48] = hashlib.sha256(content[48:48 + length]).digest()
        self.path.write_bytes(content)
        _, report = self.inspect()
        self.assertEqual(report["status"], "INCOMPLETE")
        self.assertEqual(report["events"], 4)

    def test_capture_metadata_checksum_rejects_corruption(self):
        content = bytearray(self.path.read_bytes())
        content[24] ^= 1
        self.path.write_bytes(content)
        _, report = self.inspect()
        self.assertEqual(report["status"], "INVALID")
        self.assertEqual(report["reason"], "capture metadata digest mismatch")

    def test_symlink_is_rejected(self):
        link = self.path.with_name("link.xcr")
        link.symlink_to(self.path)
        self.path = link
        _, report = self.inspect()
        self.assertEqual(report["status"], "INVALID")


if __name__ == "__main__":
    unittest.main()

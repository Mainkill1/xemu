#!/usr/bin/env python3
"""Contract tests for DSP capture integrity admission."""

import hashlib
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest

MODULE = Path(__file__).with_name("dsp_corpus.py")
spec = importlib.util.spec_from_file_location("dsp_corpus", MODULE)
corpus = importlib.util.module_from_spec(spec)
spec.loader.exec_module(corpus)


class CorpusAdmissionTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.data = {
            "initial-state.bin": b"initial",
            "input.bin": b"input",
            "external-memory.bin": b"memory",
            "events.jsonl": b"",
            "expected-state.bin": b"final",
            "expected-events.jsonl": b"",
            "expected-pcm.bin": b"pcm",
        }
        for name, value in self.data.items():
            (self.root / name).write_bytes(value)
        self.manifest = {
            "format_version": 1,
            "case_type": "gp_subframe",
            "complete": True,
            "provenance": {
                "xemu_commit": "a" * 40, "backend": "jit",
                "dsp_library": "dsp56300", "dsp_version": "0.1.3",
                "dsp_library_sha256": "b" * 64,
                "authority": "emulator", "captured_at": "2026-11-02T00:00:00Z",
                "license": "synthetic", "privacy": "public",
                "program_sha256": "c" * 64, "program_generation": 0,
            },
            "audio": {"sample_rate": 48000, "format": "s16le",
                      "channels": ["left", "right"]},
            "frame_count": 1, "sample_count": 32, "seed": 0,
            "checkpoints": ["end"], "unsupported": [],
            "files": {name: hashlib.sha256(data).hexdigest()
                      for name, data in self.data.items()},
        }
        self.write_manifest()

    def write_manifest(self):
        (self.root / "manifest.json").write_text(json.dumps(self.manifest))

    def test_complete_case_passes_integrity_admission(self):
        self.assertEqual(corpus.check_case(self.root).status, "INTEGRITY_PASS")

    def test_missing_state_is_incomplete(self):
        (self.root / "initial-state.bin").unlink()
        self.assertEqual(corpus.check_case(self.root).status, "INCOMPLETE")

    def test_corruption_is_rejected(self):
        (self.root / "input.bin").write_bytes(b"changed")
        self.assertEqual(corpus.check_case(self.root).status, "INVALID")

    def test_incomplete_flag_is_not_a_pass(self):
        self.manifest["complete"] = False
        self.write_manifest()
        self.assertEqual(corpus.check_case(self.root).status, "INCOMPLETE")

    def test_manifest_cannot_omit_a_required_file(self):
        del self.manifest["files"]["expected-pcm.bin"]
        self.write_manifest()
        self.assertEqual(corpus.check_case(self.root).status, "INCOMPLETE")

    def test_unsupported_format_is_not_a_pass(self):
        self.manifest["format_version"] = 2
        self.write_manifest()
        self.assertEqual(corpus.check_case(self.root).status, "UNSUPPORTED")

    def test_symlinked_payload_is_rejected(self):
        (self.root / "input.bin").unlink()
        (self.root / "input.bin").symlink_to(self.root / "expected-pcm.bin")
        self.assertEqual(corpus.check_case(self.root).status, "INVALID")

    def test_duplicate_manifest_keys_are_rejected(self):
        (self.root / "manifest.json").write_text('{"complete": true, "complete": false}')
        self.assertEqual(corpus.check_case(self.root).status, "INVALID")

    def test_missing_provenance_is_incomplete(self):
        del self.manifest["provenance"]["dsp_library_sha256"]
        self.write_manifest()
        self.assertEqual(corpus.check_case(self.root).status, "INCOMPLETE")

    def test_emulator_baseline_cannot_claim_hardware_authority_implicitly(self):
        del self.manifest["provenance"]["authority"]
        self.write_manifest()
        self.assertEqual(corpus.check_case(self.root).status, "INCOMPLETE")

    def test_invalid_program_digest_is_rejected(self):
        self.manifest["provenance"]["program_sha256"] = "not-a-digest"
        self.write_manifest()
        self.assertEqual(corpus.check_case(self.root).status, "INVALID")

    def test_invalid_audio_description_is_rejected(self):
        self.manifest["audio"]["sample_rate"] = 0
        self.write_manifest()
        self.assertEqual(corpus.check_case(self.root).status, "INVALID")

    def test_boolean_sample_count_is_rejected(self):
        self.manifest["sample_count"] = True
        self.write_manifest()
        self.assertEqual(corpus.check_case(self.root).status, "INVALID")

    def test_oversized_payload_is_rejected(self):
        with (self.root / "input.bin").open("wb") as stream:
            stream.truncate(256 * 1024 * 1024 + 1)
        digest = hashlib.sha256()
        for _ in range(256):
            digest.update(bytes(1024 * 1024))
        digest.update(b"\0")
        self.manifest["files"]["input.bin"] = digest.hexdigest()
        self.write_manifest()
        self.assertEqual(corpus.check_case(self.root).status, "INVALID")

    def test_oversized_manifest_is_rejected(self):
        with (self.root / "manifest.json").open("a") as stream:
            stream.write(" " * (1024 * 1024 + 1))
        self.assertEqual(corpus.check_case(self.root).status, "INVALID")

    @unittest.skipUnless(hasattr(os, "mkfifo"), "FIFO requires POSIX")
    def test_fifo_payload_is_rejected_without_waiting_for_writer(self):
        (self.root / "input.bin").unlink()
        os.mkfifo(self.root / "input.bin")
        self.assertEqual(corpus.check_case(self.root).status, "INVALID")


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
"""Check that the upstream runner cannot mistake incomplete output for success."""
import unittest
import json
from pathlib import Path
import subprocess
import sys
import tempfile

from dsp_upstream_goldens import summarize


class UpstreamGoldenSummaryTests(unittest.TestCase):
    def test_complete_reports_include_every_pinned_bank(self):
        banks = [f"b{i}" for i in range(24)]
        output = "\n539/539 cases match (0 differ)\n"
        output += "".join("\n1/1 cases match (0 differ)\n" for _ in banks[1:])
        output += "".join(f"bank {bank}:\n" for bank in banks)
        self.assertEqual(summarize(output), {"banks": 24, "cases": 562, "differences": 0})

    def test_empty_truncated_unsealed_and_differing_results_fail(self):
        banks = [f"b{i}" for i in range(24)]
        counts = "".join("1/1 cases match (0 differ)\n" for _ in banks)
        names = "".join(f"bank {bank}:\n" for bank in banks)
        for output in ("", counts + names.replace("bank b23:\n", ""),
                       counts + names,
                       counts + names + "bank b2: UNSEALED\n",
                       counts.replace("1/1 cases match (0 differ)",
                                      "0/1 cases match (1 differ)", 1) + names):
            with self.subTest(output=output):
                with self.assertRaises(ValueError):
                    summarize(output)

    def test_wrong_checkout_is_incomplete_and_report_is_not_overwritten(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            subprocess.run(["git", "init", "-q", str(root / "upstream")], check=True)
            checkout = root / "upstream"
            (checkout / "file").write_text("fixture")
            subprocess.run(["git", "add", "file"], cwd=checkout, check=True)
            subprocess.run(["git", "-c", "user.name=test", "-c", "user.email=test@example.invalid",
                            "commit", "-qm", "fixture"], cwd=checkout, check=True)
            binary = root / "binary"
            binary.write_bytes(b"fixture")
            report = root / "report.json"
            command = [sys.executable, str(Path(__file__).with_name("dsp_upstream_goldens.py")),
                       "--checkout", str(checkout), "--difftest", str(binary),
                       "--report", str(report)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=10)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(json.loads(report.read_text())["status"], "INCOMPLETE")
            report.write_text("sentinel")
            result = subprocess.run(command, capture_output=True, text=True, timeout=10)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(report.read_text(), "sentinel")


if __name__ == "__main__":
    unittest.main()

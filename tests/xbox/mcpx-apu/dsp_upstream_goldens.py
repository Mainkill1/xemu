#!/usr/bin/env python3
"""Run the pinned upstream ISA golden checker and record bounded evidence."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

PINNED_COMMIT = "b36db2c23ee2dd6e643f48e4dc6288074030e1a0"
EXPECTED_BANKS = {f"b{i}" for i in range(24)}


def summarize(output):
    counts = re.findall(r"^(\d+)/(\d+) cases match \((\d+) differ\)$", output, re.M)
    banks = re.findall(r"^bank (b\d+):$", output, re.M)
    if ("UNSEALED" in output or len(counts) != len(EXPECTED_BANKS)
            or len(banks) != len(EXPECTED_BANKS) or set(banks) != EXPECTED_BANKS):
        raise ValueError("incomplete or unsealed upstream results")
    totals = [(int(a), int(b), int(c)) for a, b, c in counts]
    if (sum(total for _, total, _ in totals) != 562 or
            any(matched != total or total <= 0 or different != 0
                for matched, total, different in totals)):
        raise ValueError("upstream cases did not all match")
    return {"banks": len(banks), "cases": sum(total for _, total, _ in totals),
            "differences": 0}


def command(args, cwd):
    return subprocess.run(args, cwd=cwd, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, text=True, timeout=120, check=True).stdout


def checkout_identity(root):
    commit = command(["git", "rev-parse", "HEAD"], root).strip()
    dirty = command(["git", "status", "--porcelain", "--untracked-files=no"], root)
    if commit != PINNED_COMMIT or dirty:
        raise ValueError("upstream checkout is not clean at the pinned commit")
    return commit


def file_hash(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--checkout", required=True, type=Path)
    parser.add_argument("--difftest", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    args = parser.parse_args()
    root, binary = args.checkout.resolve(), args.difftest.resolve()
    report = {"status": "INCOMPLETE", "upstream_commit": PINNED_COMMIT,
              "scope": "upstream-isa-only", "modes": {}}
    try:
        checkout_identity(root)
        digest = file_hash(binary)
        checker = root / "tools/difftest/check_goldens.py"
        for mode in ("step", "block"):
            output = command([sys.executable, str(checker), "--difftest", str(binary),
                              "--mode", mode], root)
            report["modes"][mode] = summarize(output)
        if checkout_identity(root) != PINNED_COMMIT or file_hash(binary) != digest:
            raise ValueError("upstream inputs changed during verification")
        report.update(status="PASS", difftest_sha256=digest)
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        report["error"] = type(exc).__name__
    try:
        fd = os.open(args.report, os.O_WRONLY | os.O_CREAT | os.O_EXCL |
                     getattr(os, "O_NOFOLLOW", 0), 0o600)
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            json.dump(report, stream, sort_keys=True)
            stream.write("\n")
    except OSError:
        print("report unavailable or already exists", file=sys.stderr)
        return 1
    print(json.dumps(report, sort_keys=True))
    return 0 if report["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())

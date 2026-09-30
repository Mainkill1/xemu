#!/usr/bin/env python3
"""Verify archived source bytes and the complete product evidence inventory."""
from pathlib import Path
import hashlib, json
ROOT = Path(__file__).resolve().parent

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def read_sums(path):
    return dict((name, sha) for sha, name in (line.split("  ", 1) for line in path.read_text().splitlines()))

def main():
    source = ROOT / "SOURCE-SHA256SUMS"
    original = read_sums(source) if source.exists() else json.loads((ROOT / "source-artifact-hashes.json").read_text())
    for name, expected in original.items():
        actual = "ORIGINAL-REPORT.md" if name == "REPORT.md" else name
        assert digest(ROOT / actual) == expected, ("original changed", name)
    current = ROOT / "SHA256SUMS"
    manifest = read_sums(current) if current.exists() else json.loads((ROOT / "artifact-hashes.json").read_text())
    excluded = "SHA256SUMS" if current.exists() else "artifact-hashes.json"
    actual_files = {p.relative_to(ROOT).as_posix() for p in ROOT.rglob("*") if p.is_file() and p.name != excluded and "__pycache__" not in p.parts}
    assert set(manifest) == actual_files, ("inventory differs", sorted(set(manifest) ^ actual_files))
    for name, expected in manifest.items():
        assert digest(ROOT / name) == expected, ("current changed", name)
    print(f"Verified {len(original)} original artifacts and {len(manifest)} current artifacts in {ROOT.name}.")

if __name__ == "__main__":
    main()

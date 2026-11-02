#!/usr/bin/env python3
"""Preliminary integrity admission for DSP cases; does not replay them."""

import argparse
import hashlib
import json
import os
import re
import stat
from collections import namedtuple
from pathlib import Path

Result = namedtuple("Result", "status detail")
MAX_MANIFEST_BYTES = 1024 * 1024
MAX_PAYLOAD_BYTES = 256 * 1024 * 1024
REQUIRED = frozenset({
    "initial-state.bin", "input.bin", "external-memory.bin", "events.jsonl",
    "expected-state.bin", "expected-events.jsonl", "expected-pcm.bin",
})
CASE_TYPES = {"gp_subframe", "ep_frame", "gp_ep_sequence", "whole_apu_sequence"}
PROVENANCE = {"xemu_commit", "backend", "dsp_library", "dsp_version",
              "dsp_library_sha256", "authority", "captured_at", "license",
              "privacy", "program_sha256", "program_generation"}


def _unique_pairs(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate JSON key")
        result[key] = value
    return result


def _open_regular(path, limit):
    """Reject links, special files and oversized inputs before reading."""
    before = path.lstat()
    if not stat.S_ISREG(before.st_mode) or before.st_size > limit:
        raise ValueError("not a bounded regular file")
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0)
    flags |= getattr(os, "O_BINARY", 0)
    fd = os.open(path, flags)
    try:
        opened = os.fstat(fd)
        if (not stat.S_ISREG(opened.st_mode) or opened.st_size > limit or
                (before.st_dev, before.st_ino) != (opened.st_dev, opened.st_ino)):
            raise ValueError("file changed during open")
        return os.fdopen(fd, "rb")
    except BaseException:
        os.close(fd)
        raise


def check_case(root):
    """Check presence and hashes only; INTEGRITY_PASS is not replay PASS."""
    root = Path(root)
    manifest = root / "manifest.json"
    if manifest.is_symlink():
        return Result("INVALID", "symlinked manifest")
    try:
        with _open_regular(manifest, MAX_MANIFEST_BYTES) as stream:
            content = stream.read(MAX_MANIFEST_BYTES + 1)
        if len(content) > MAX_MANIFEST_BYTES:
            raise ValueError("manifest grew beyond limit")
        data = json.loads(content.decode("utf-8"), object_pairs_hook=_unique_pairs)
    except FileNotFoundError:
        return Result("INCOMPLETE", "missing manifest")
    except (OSError, ValueError, UnicodeError):
        return Result("INVALID", "unreadable or malformed manifest")
    if not isinstance(data, dict):
        return Result("INVALID", "manifest must be an object")
    if type(data.get("format_version")) is not int:
        return Result("INVALID", "invalid format version")
    if data["format_version"] != 1:
        return Result("UNSUPPORTED", "unsupported format version")
    if not isinstance(data.get("case_type"), str) or data["case_type"] not in CASE_TYPES:
        return Result("UNSUPPORTED", "unsupported case type")
    if data.get("complete") is not True:
        return Result("INCOMPLETE", "case not declared complete")
    provenance = data.get("provenance")
    if not isinstance(provenance, dict) or not PROVENANCE.issubset(provenance):
        return Result("INCOMPLETE", "missing provenance")
    for key in PROVENANCE - {"program_generation"}:
        if not isinstance(provenance[key], str) or not provenance[key]:
            return Result("INVALID", "invalid provenance: " + key)
    for key in ("dsp_library_sha256", "program_sha256"):
        if not re.fullmatch(r"[0-9a-f]{64}", provenance[key]):
            return Result("INVALID", "invalid provenance digest: " + key)
    if not re.fullmatch(r"[0-9a-f]{40,64}", provenance["xemu_commit"]):
        return Result("INVALID", "invalid xemu commit")
    if provenance["authority"] not in ("hardware", "simulator", "emulator"):
        return Result("INVALID", "invalid reference authority")
    if type(provenance["program_generation"]) is not int or provenance["program_generation"] < 0:
        return Result("INVALID", "invalid program generation")
    if not {"audio", "frame_count", "sample_count", "seed", "checkpoints", "unsupported"}.issubset(data):
        return Result("INCOMPLETE", "missing capture metadata")
    audio = data["audio"]
    if not isinstance(audio, dict) or not {"sample_rate", "format", "channels"}.issubset(audio):
        return Result("INCOMPLETE", "missing audio description")
    if (type(audio["sample_rate"]) is not int or audio["sample_rate"] <= 0
            or not isinstance(audio["format"], str) or not audio["format"]
            or not isinstance(audio["channels"], list) or not audio["channels"]
            or any(not isinstance(c, str) or not c for c in audio["channels"])):
        return Result("INVALID", "invalid audio description")
    for key in ("frame_count", "sample_count", "seed"):
        if type(data[key]) is not int or data[key] < 0:
            return Result("INVALID", "invalid " + key)
    for key in ("checkpoints", "unsupported"):
        if not isinstance(data[key], list) or any(
                not isinstance(item, str) or not item for item in data[key]):
            return Result("INVALID", "invalid " + key)
    files = data.get("files")
    if not isinstance(files, dict) or not REQUIRED.issubset(files):
        return Result("INCOMPLETE", "required file digest missing")
    if set(files) != REQUIRED:
        return Result("UNSUPPORTED", "unknown payload")
    for name in sorted(REQUIRED):
        digest = files[name]
        if not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{64}", digest):
            return Result("INVALID", "invalid digest: " + name)
        path = root / name
        if path.is_symlink():
            return Result("INVALID", "symlinked payload: " + name)
        try:
            with _open_regular(path, MAX_PAYLOAD_BYTES) as stream:
                hasher = hashlib.sha256()
                size = 0
                for block in iter(lambda: stream.read(1024 * 1024), b""):
                    size += len(block)
                    if size > MAX_PAYLOAD_BYTES:
                        raise ValueError("payload grew beyond limit")
                    hasher.update(block)
                actual = hasher.hexdigest()
        except FileNotFoundError:
            return Result("INCOMPLETE", "missing payload: " + name)
        except (OSError, ValueError):
            return Result("INVALID", "unreadable payload: " + name)
        if actual != digest:
            return Result("INVALID", "digest mismatch: " + name)
    return Result("INTEGRITY_PASS", "payload hashes match; replay not performed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("case", type=Path)
    args = parser.parse_args()
    result = check_case(args.case)
    print(json.dumps(result._asdict()))
    return 0 if result.status == "INTEGRITY_PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())

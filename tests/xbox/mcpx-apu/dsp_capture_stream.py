#!/usr/bin/env python3
"""Inspect partial XCR1 DSP capture streams; this does not replay them."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

from dsp_corpus import _open_regular
from dsp_capture_memory import MemoryModel, ModelError, TransferCoverage

MAX_STREAM_BYTES = 2 * 1024 * 1024 * 1024
MAX_RECORD_BYTES = 2 * 1024 * 1024
MAX_FRAMES = 1024
STATE_BYTES = 41554
EXTERNAL_UNVERIFIED = 1
KNOWN_FRAME_FLAGS = 0x1ff
NONRAM_DESCRIPTOR = 0x80
MAPPING_OBSERVED = 0x100
STREAM_V3_HEADER_SIZE = 304


def _fixed_ascii(data, start, size, name):
    field = data[start:start + size]
    end = field.find(b"\0")
    if end < 0 or any(field[end + 1:]):
        raise ValueError(f"invalid {name}")
    try:
        value = field[:end].decode("ascii")
    except UnicodeDecodeError as exc:
        raise ValueError(f"invalid {name}") from exc
    if any(ord(char) < 0x20 or ord(char) > 0x7e for char in value):
        raise ValueError(f"invalid {name}")
    return value


def parse_capture_header(data):
    if len(data) < 8 or data[:4] != b"XCR1":
        raise ValueError("invalid stream header")
    version = struct.unpack_from("<I", data, 4)[0]
    if version in (1, 2):
        if len(data) != 8:
            raise ValueError("invalid legacy stream header")
        return {"stream_version": version}
    if version != 3:
        raise ValueError("unsupported stream version")
    if len(data) != STREAM_V3_HEADER_SIZE or struct.unpack_from("<I", data, 8)[0] != len(data):
        raise ValueError("invalid capture metadata size")
    if hashlib.sha256(data[:272]).digest() != data[272:]:
        raise ValueError("capture metadata digest mismatch")
    flags = struct.unpack_from("<I", data, 12)[0]
    created = struct.unpack_from("<Q", data, 16)[0]
    if flags & ~7 or not created or any(data[266:272]):
        raise ValueError("invalid capture metadata")
    commit = _fixed_ascii(data, 24, 65, "xemu commit")
    version_text = _fixed_ascii(data, 89, 64, "xemu version")
    dsp_status = _fixed_ascii(data, 153, 16, "DSP library status")
    dsp_version = _fixed_ascii(data, 169, 32, "DSP library version")
    dsp_sha = _fixed_ascii(data, 201, 65, "DSP library SHA-256")
    commit_recorded = bool(flags & 1)
    version_recorded = bool(flags & 2)
    dsp_recorded = bool(flags & 4)
    if commit_recorded != bool(commit) or (commit and not re.fullmatch(r"[0-9a-f]{40}|[0-9a-f]{64}", commit)):
        raise ValueError("invalid xemu commit identity")
    if not version_recorded or not version_text:
        raise ValueError("missing xemu version identity")
    if dsp_recorded:
        if (dsp_status != "BUILD_RECORDED" or
                not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:[-+][0-9A-Za-z.-]+)?", dsp_version) or
                not re.fullmatch(r"[0-9a-f]{64}", dsp_sha)):
            raise ValueError("invalid DSP library identity")
    elif dsp_status != "UNVERIFIED" or dsp_version or dsp_sha:
        raise ValueError("invalid unverified DSP library identity")
    library = {"status": dsp_status}
    if dsp_recorded:
        library.update(version=dsp_version, sha256=dsp_sha, api_revision="UNVERIFIED")
    return {"stream_version": version, "created_unix_usec": created,
            "xemu_commit": commit, "xemu_version": version_text,
            "linked_dsp_library": library}


def _record(data):
    """Check bounds and fixed fields; do not infer DSP or hardware validity."""
    fixed = 40 + 4 * STATE_BYTES + 2 * 0x80000 + 4096 + 2 * 1024
    if len(data) < fixed + 8 or data[:4] != b"XFR1":
        raise ValueError("invalid frame layout")
    frame_id, flags, dropped, phase, _, _, monitor = struct.unpack_from("<Q6I", data, 4)
    if (not flags & EXTERNAL_UNVERIFIED or flags & ~KNOWN_FRAME_FLAGS or
            phase > 7 or monitor > 4):
        raise ValueError("invalid frame metadata")
    if any(value > 1 for value in data[36:40]):
        raise ValueError("invalid control boolean")
    for index in range(4):
        offset = 40 + index * STATE_BYTES
        if data[offset:offset + 8] != b"XDJT\x01\x00\x00\x00":
            raise ValueError("invalid checkpoint header")
    transcript = data[fixed:fixed + 4]
    if transcript not in (b"XIO1", b"XCT1"):
        raise ValueError("invalid transcript header")
    count = struct.unpack_from("<I", data, fixed + 4)[0]
    if count > 1024:
        raise ValueError("too many events")
    offset = fixed + 8
    events = []
    for _ in range(count):
        if len(data) - offset < 24:
            raise ValueError("truncated event")
        kind, write, reserved, index, address, sample, size = struct.unpack_from("<BBHIIQI", data, offset)
        offset += 24
        if (kind > (5 if transcript == b"XCT1" else 1) or write > 1 or reserved
                or size > len(data) - offset or (kind in (2, 3, 5) and write)
                or (kind == 4 and not write)):
            raise ValueError("invalid event")
        events.append((kind, bool(write), index, address, sample, data[offset:offset + size]))
        offset += size
    if offset != len(data):
        raise ValueError("unexpected frame bytes")
    return frame_id, flags, dropped, events


def inspect(path, ram_size=None):
    report = {"status": "INCOMPLETE", "frames": 0, "events": 0,
              "footer_present": False, "external_unverified": True,
              "authority": "UNVERIFIED"}
    try:
        model = MemoryModel(ram_size) if ram_size is not None else None
        coverage = TransferCoverage() if model is not None else None
        model_error = None
        with _open_regular(Path(path), MAX_STREAM_BYTES) as stream:
            header = stream.read(8)
            if len(header) < 8:
                report["reason"] = "truncated header"
                return report
            if header[:4] != b"XCR1":
                raise ValueError("invalid stream header")
            version = struct.unpack_from("<I", header, 4)[0]
            if version == 3:
                rest = stream.read(STREAM_V3_HEADER_SIZE - 8)
                if len(rest) != STREAM_V3_HEADER_SIZE - 8:
                    report["reason"] = "truncated capture metadata"
                    return report
                metadata = parse_capture_header(header + rest)
            elif version in (1, 2):
                metadata = parse_capture_header(header)
            else:
                report.update(status="UNSUPPORTED", reason="unsupported stream version")
                return report
            report.update(metadata)
            report["mapping_check"] = ("NOT_RECORDED" if version == 1 else
                                       "OBSERVED_UNVERIFIED")
            previous = None
            flags = 0
            while True:
                tag = stream.read(4)
                if len(tag) < 4:
                    report["reason"] = "missing or truncated footer"
                    return report
                if tag == b"END1":
                    footer = stream.read(12)
                    if len(footer) != 12:
                        report["reason"] = "truncated footer"
                        return report
                    written, dropped, lost = struct.unpack("<III", footer)
                    if written != report["frames"] or stream.read(1):
                        raise ValueError("invalid footer")
                    if model is not None:
                        if model_error is None:
                            try:
                                model.finish()
                                coverage.finish()
                            except ModelError as exc:
                                model_error = str(exc)
                        if model_error:
                            report["memory_model"] = ("INCONSISTENT" if
                                "inconsistent" in model_error else "INCOMPLETE")
                            report["memory_reason"] = model_error
                        else:
                            report["memory_model"] = "CONSISTENT_UNVERIFIED"
                            report["known_bytes"] = model.known_count
                            report["unknown_bytes"] = ram_size - model.known_count
                    report.update(footer_present=True, dropped=dropped, lost=lost,
                                  flags=flags, reason="external memory and provenance unverified")
                    return report
                if tag != b"FRM1" or report["frames"] >= MAX_FRAMES:
                    raise ValueError("invalid record header")
                record_header = stream.read(36)
                if len(record_header) != 36:
                    report["reason"] = "truncated record header"
                    return report
                length = struct.unpack_from("<I", record_header)[0]
                if length > MAX_RECORD_BYTES:
                    raise ValueError("record exceeds limit")
                data = stream.read(length)
                if len(data) != length:
                    report["reason"] = "truncated record"
                    return report
                if hashlib.sha256(data).digest() != record_header[4:]:
                    raise ValueError("record digest mismatch")
                frame_id, frame_flags, _, events = _record(data)
                if frame_flags & NONRAM_DESCRIPTOR:
                    report["mapping_check"] = "NONRAM_DETECTED"
                elif (version >= 2 and not frame_flags & MAPPING_OBSERVED and
                      report["mapping_check"] != "NONRAM_DETECTED"):
                    report["mapping_check"] = "INCOMPLETE"
                if previous is not None and frame_id <= previous:
                    raise ValueError("frame order is not increasing")
                previous = frame_id
                if model is not None and model_error is None:
                    if frame_flags & NONRAM_DESCRIPTOR:
                        model_error = "non-RAM descriptor mapping"
                    elif version >= 2 and not frame_flags & MAPPING_OBSERVED:
                        model_error = "descriptor mapping observation missing"
                    elif frame_flags & (2 | 4 | 8 | 16) or (
                            report["frames"] and frame_flags & 32):
                        model_error = "capture gap or incomplete frame"
                    else:
                        try:
                            for kind, write, _, address, _, payload in events:
                                model.event(kind, address, payload)
                                coverage.event(kind, write, len(payload))
                            coverage.finish()
                        except ModelError as exc:
                            model_error = str(exc)
                flags |= frame_flags
                report["frames"] += 1
                report["events"] += len(events)
    except FileNotFoundError:
        report["reason"] = "missing stream"
    except (OSError, ValueError, struct.error) as exc:
        report.update(status="INVALID", reason=str(exc) if isinstance(exc, ValueError)
                      else "unreadable or malformed stream")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stream", type=Path)
    parser.add_argument("--ram-size", type=int, help="check observed physical RAM against this size")
    args = parser.parse_args()
    report = inspect(args.stream, args.ram_size)
    print(json.dumps(report, sort_keys=True))
    return 1  # XCR1 never establishes a complete replay or hardware verdict.


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Create private, explicitly unverified replay inputs from an XCR1 stream."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import struct

from dsp_corpus import _open_regular
from dsp_capture_memory import MemoryModel, ModelError, TransferCoverage
from dsp_capture_stream import (MAX_FRAMES, MAX_RECORD_BYTES, MAX_STREAM_BYTES,
                                MAPPING_OBSERVED, NONRAM_DESCRIPTOR, STATE_BYTES,
                                STREAM_V3_HEADER_SIZE, _record, parse_capture_header)


class IncompleteCapture(ValueError):
    pass


def _read(stream, size, digest):
    data = stream.read(size)
    digest.update(data)
    if len(data) != size:
        raise IncompleteCapture("truncated stream")
    return data


def reconstruct(source, ram_size, output, allow_legacy=False):
    report = {"status": "INCOMPLETE", "artifacts_created": False,
              "authority": "UNVERIFIED"}
    created = False
    output = Path(output)
    try:
        if type(ram_size) is not int or ram_size <= 0 or ram_size % 4096:
            raise ValueError("RAM size must be a positive multiple of 4096")
        model = MemoryModel(ram_size)
        coverage = TransferCoverage()
        digest = hashlib.sha256()
        mixes, pcm, logical = [], [], []
        gp_frame_states, ep_frame_states = [], []
        first = last = previous = None
        total_event_bytes = 0
        count = 0
        with _open_regular(Path(source), MAX_STREAM_BYTES) as stream:
            before = os.fstat(stream.fileno())
            header = _read(stream, 8, digest)
            if header[:4] != b"XCR1" or struct.unpack_from("<I", header, 4)[0] not in (1, 2, 3):
                raise ValueError("invalid or unsupported stream header")
            version = struct.unpack_from("<I", header, 4)[0]
            if version == 3:
                header += _read(stream, STREAM_V3_HEADER_SIZE - 8, digest)
            report.update(parse_capture_header(header))
            report["mapping_check"] = ("NOT_RECORDED" if version == 1 else
                                       "OBSERVED_UNVERIFIED")
            if version == 1 and not allow_legacy:
                raise IncompleteCapture("legacy stream lacks descriptor mapping observations")
            while True:
                tag = _read(stream, 4, digest)
                if tag == b"END1":
                    written, dropped, lost = struct.unpack("<III", _read(stream, 12, digest))
                    if written != count or stream.read(1):
                        raise ValueError("invalid footer")
                    if dropped or lost:
                        raise IncompleteCapture("capture has dropped or lost frames")
                    break
                if tag != b"FRM1" or count >= MAX_FRAMES:
                    raise ValueError("invalid record header")
                header = _read(stream, 36, digest)
                length = struct.unpack_from("<I", header)[0]
                if length > MAX_RECORD_BYTES:
                    raise ValueError("record exceeds limit")
                data = _read(stream, length, digest)
                if hashlib.sha256(data).digest() != header[4:]:
                    raise ValueError("record digest mismatch")
                frame_id, flags, _, events = _record(data)
                if flags & NONRAM_DESCRIPTOR:
                    report["mapping_check"] = "NONRAM_DETECTED"
                    raise IncompleteCapture("non-RAM descriptor mapping")
                if version >= 2 and not flags & MAPPING_OBSERVED:
                    report["mapping_check"] = "INCOMPLETE"
                    raise IncompleteCapture("descriptor mapping observation missing")
                if flags & (2 | 4 | 8 | 16) or (count and flags & 32):
                    raise IncompleteCapture("capture has a gap or incomplete boundary")
                if frame_id > ((1 << 64) - 1) // 32:
                    raise IncompleteCapture("frame index overflows sample index")
                _, _, _, phase, gp_reset, ep_reset, monitor = struct.unpack_from("<Q6I", data, 4)
                gp_rt, ep_rt, started, finished = data[36:40]
                if not started or not finished:
                    raise IncompleteCapture("frame boundary missing")
                state = [data[40 + i * STATE_BYTES:40 + (i + 1) * STATE_BYTES]
                         for i in range(4)]
                offset = 40 + 4 * STATE_BYTES
                regs_before = data[offset:offset + 0x80000]
                regs_after = data[offset + 0x80000:offset + 0x100000]
                offset += 0x100000
                frame_mix = data[offset:offset + 4096]
                before_pcm = data[offset + 4096:offset + 5120]
                after_pcm = data[offset + 5120:offset + 6144]
                if data[offset + 6144:offset + 6148] != b"XCT1":
                    raise IncompleteCapture("physical transfer transcript missing")
                controls = (gp_reset, ep_reset, monitor, gp_rt, ep_rt)
                if previous is not None:
                    if (frame_id != previous[0] + 1 or phase != (previous[1] + 1) % 8
                            or controls != previous[2] or state[:2] != previous[3]
                            or regs_before != previous[4] or before_pcm != previous[5]):
                        raise IncompleteCapture("frame boundary state is not continuous")
                else:
                    first = (frame_id, phase, controls, state[:2], regs_before, before_pcm)
                previous = (frame_id, phase, controls, state[2:], regs_after, after_pcm)
                last = (state[2:], regs_after)
                gp_frame_states.append(state[2])
                ep_frame_states.append(state[3])
                mixes.append(frame_mix)
                pcm.append(after_pcm)
                for kind, write, index, address, sample, payload in events:
                    if sample != frame_id * 32:
                        raise IncompleteCapture("unsupported event timestamp")
                    model.event(kind, address, payload)
                    coverage.event(kind, write, len(payload))
                    if kind < 2:
                        total_event_bytes += len(payload)
                        if len(logical) >= 4096 or total_event_bytes > 4 * 1024 * 1024:
                            raise IncompleteCapture("logical transcript exceeds replay limits")
                        logical.append(struct.pack("<BBHIIQI", kind, write, 0, index,
                                                   address, sample - first[0] * 32,
                                                   len(payload)) + payload)
                coverage.finish()
                count += 1
            after = os.fstat(stream.fileno())
            current = Path(source).lstat()
            identity = lambda s: (s.st_dev, s.st_ino, s.st_size, s.st_mtime_ns, s.st_ctime_ns)
            if (not stat.S_ISREG(current.st_mode) or identity(before) != identity(after)
                    or identity(after) != identity(current)):
                raise IncompleteCapture("source changed during reconstruction")
        if not count:
            raise IncompleteCapture("no captured frames")
        model.finish()
        gp_reset, ep_reset, monitor, gp_rt, ep_rt = first[2]
        payloads = {
            "gp-initial.bin": first[3][0], "ep-initial.bin": first[3][1],
            "gp-expected.bin": last[0][0], "ep-expected.bin": last[0][1],
            "apu-registers.bin": first[4], "expected-apu-registers.bin": last[1],
            "mixbins.bin": b"".join(mixes), "prior-pcm.bin": first[5],
            "expected-pcm.bin": b"".join(pcm), "ram.bin": model.initial,
            "gp-frame-states.bin": b"".join(gp_frame_states),
            "ep-frame-states.bin": b"".join(ep_frame_states),
            "expected-ram.bin": model.current, "known-bitmap.bin": model.known,
            "control.bin": struct.pack("<4sIIBBBB", b"XMF1", gp_reset, ep_reset,
                                       monitor, first[1], gp_rt, ep_rt),
            "events.bin": b"XIO1" + struct.pack("<I", len(logical)) + b"".join(logical),
        }
        report.update(frames=count, known_bytes=model.known_count,
                      unknown_bytes=ram_size - model.known_count,
                      logical_events=len(logical), source_sha256=digest.hexdigest(),
                      reason="unobserved RAM and provenance remain unverified")
        output.mkdir(mode=0o700)
        created = True
        hashes = {}
        for name, payload in payloads.items():
            flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0)
            flags |= getattr(os, "O_BINARY", 0)
            fd = os.open(output / name, flags, 0o600)
            with os.fdopen(fd, "wb") as dest:
                dest.write(payload)
            hashes[name] = hashlib.sha256(payload).hexdigest()
        metadata = dict(report, artifacts_created=True, files=hashes)
        fd = os.open(output / "status.json", os.O_WRONLY | os.O_CREAT | os.O_EXCL |
                     getattr(os, "O_NOFOLLOW", 0), 0o600)
        with os.fdopen(fd, "w", encoding="utf-8") as dest:
            json.dump(metadata, dest, sort_keys=True)
        report["artifacts_created"] = True
    except IncompleteCapture as exc:
        report["reason"] = str(exc)
    except ModelError as exc:
        report["reason"] = str(exc)
    except (OSError, ValueError, struct.error, MemoryError):
        report.update(status="INVALID", reason="invalid input or output unavailable")
    if created and not report["artifacts_created"]:
        shutil.rmtree(output, ignore_errors=True)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stream", type=Path)
    parser.add_argument("--ram-size", type=int, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--allow-legacy-unverified", action="store_true",
                        help="reconstruct older streams without descriptor mapping observations")
    args = parser.parse_args()
    print(json.dumps(reconstruct(args.stream, args.ram_size, args.output,
                                 args.allow_legacy_unverified), sort_keys=True))
    return 1  # Reconstruction is not complete capture or correctness evidence.


if __name__ == "__main__":
    raise SystemExit(main())

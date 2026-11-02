#!/usr/bin/env python3
"""Build an unverified sparse RAM model from observed capture transfers.

Unobserved bytes are unknown, even though their backing arrays contain zero.
Consistency is not proof that a live guest snapshot was coherent.
"""

MAX_RAM = 128 * 1024 * 1024


class ModelError(ValueError):
    pass


class TransferCoverage:
    """Conservatively pair logical callbacks with physical transfers."""

    def __init__(self):
        self.write_remaining = 0
        self.read_bytes = 0
        self.descriptor = False
        self.prewrite = None
        self.transfers = 0

    def event(self, kind, write, length):
        if (type(kind) is not int or kind not in range(6) or type(write) is not bool
                or type(length) is not int or length < 0):
            raise ModelError("invalid transfer event")
        if kind in (0, 1):
            if self.write_remaining or self.prewrite is not None or self.descriptor:
                raise ModelError("incomplete physical transfer")
            if write:
                if self.read_bytes:
                    raise ModelError("incomplete read transfer")
                self.write_remaining = length
            else:
                if self.read_bytes != length:
                    raise ModelError("uncovered read transfer")
                self.read_bytes = 0
            self.transfers += 1
        elif kind == 5:
            if write or length != 4 or self.descriptor or self.prewrite is not None:
                raise ModelError("invalid descriptor transfer")
            self.descriptor = True
        elif kind == 2:
            if write or not self.write_remaining or not self.descriptor or self.prewrite is not None:
                raise ModelError("uncovered prewrite transfer")
            self.prewrite = length
            self.descriptor = False
        elif kind == 4:
            if (not write or self.prewrite is None or self.prewrite != length
                    or length > self.write_remaining):
                raise ModelError("uncovered write transfer")
            self.write_remaining -= length
            self.prewrite = None
        else:
            if write or self.write_remaining or not self.descriptor or self.prewrite is not None:
                raise ModelError("uncovered physical read transfer")
            self.read_bytes += length
            self.descriptor = False

    def finish(self):
        if self.write_remaining or self.read_bytes or self.descriptor or self.prewrite is not None:
            raise ModelError("incomplete physical transfer")


class MemoryModel:
    def __init__(self, size):
        if type(size) is not int or not 0 < size <= MAX_RAM:
            raise ModelError("invalid RAM size")
        try:
            self.initial = bytearray(size)
            self.current = bytearray(size)
            self.known = bytearray((size + 7) // 8)
        except MemoryError as exc:
            raise ModelError("RAM model allocation failed") from exc
        self.known_count = 0
        self.pending = None
        self.physical_events = 0
        self.valid = True

    def is_known(self, address):
        if type(address) is not int or not 0 <= address < len(self.initial):
            raise ModelError("address out of bounds")
        return bool(self.known[address // 8] & (1 << (address % 8)))

    def _observe(self, address, data):
        for offset, value in enumerate(data):
            position = address + offset
            mask = 1 << (position % 8)
            slot = position // 8
            if self.known[slot] & mask:
                if self.current[position] != value:
                    raise ModelError("inconsistent observed memory")
            else:
                self.known[slot] |= mask
                self.known_count += 1
                self.initial[position] = self.current[position] = value

    def event(self, kind, address, data):
        if not self.valid:
            raise ModelError("model is invalid")
        try:
            self._event(kind, address, data)
        except ModelError:
            self.valid = False
            raise

    def _event(self, kind, address, data):
        if type(kind) is not int or kind not in range(6) or not isinstance(data, bytes):
            raise ModelError("invalid event")
        if self.pending is not None and kind != 4:
            raise ModelError("missing paired prewrite")
        if kind in (0, 1):  # logical scratch/FIFO callbacks
            return
        if (type(address) is not int or address < 0 or address > len(self.initial)
                or len(data) > len(self.initial) - address):
            raise ModelError("physical event out of bounds")
        if kind == 5 and len(data) != 4:
            raise ModelError("invalid descriptor read")
        self.physical_events += 1
        if kind in (2, 3, 5):
            self._observe(address, data)
            if kind == 2:
                self.pending = (address, len(data))
        else:
            if self.pending != (address, len(data)):
                raise ModelError("write does not match prewrite")
            self.current[address:address + len(data)] = data
            self.pending = None

    def finish(self):
        if not self.valid:
            raise ModelError("model is invalid")
        if self.pending is not None:
            self.valid = False
            raise ModelError("missing paired prewrite")
        if not self.physical_events:
            self.valid = False
            raise ModelError("no physical context")

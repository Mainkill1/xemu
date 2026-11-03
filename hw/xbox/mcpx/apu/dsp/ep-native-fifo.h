/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_EP_NATIVE_FIFO_H
#define XEMU_EP_NATIVE_FIFO_H

#include "ep-native-sge.h"

/* Validate every destination before native execution changes DSP or FIFO
 * state. The callback must check writable ordinary RAM without writing it.
 * Unusual cursor states are rejected for fallback to the existing DSP. */
static inline bool
ep_native_fifo_preflight(void *opaque, EPNativePhysicalRead write_probe,
                         uint64_t ram_size, uint64_t sge_base, uint32_t max_sge,
                         uint32_t base, uint32_t end, uint32_t current,
                         size_t length, uint32_t *next)
{
    uint8_t scratch[1024];
    if (!write_probe || !next || base >= end || current >= end ||
        length > sizeof(scratch)) {
        return false;
    }
    if (current < base) {
        current = base;
    }
    size_t remaining = length;
    while (remaining) {
        size_t chunk = end - current;
        if (chunk > remaining) {
            chunk = remaining;
        }
        if (!ep_native_sge_read(opaque, write_probe, ram_size, sge_base,
                                max_sge, current, scratch, chunk)) {
            return false;
        }
        remaining -= chunk;
        current += chunk;
        if (current == end) {
            current = base;
        }
    }
    *next = current;
    return true;
}
#endif

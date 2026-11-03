/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_EP_NATIVE_SGE_H
#define XEMU_EP_NATIVE_SGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The callback must accept ordinary RAM only and have no read side effects. */
typedef bool (*EPNativePhysicalRead)(void *opaque, uint64_t address,
                                     uint8_t *bytes, size_t length);

/* Read the bounded EP scratch ranges used by the candidate, staging the
 * complete result so failed translations do not publish partial data. */
static inline bool ep_native_sge_read(void *opaque, EPNativePhysicalRead read,
                                      uint64_t ram_size, uint64_t sge_base,
                                      uint32_t max_sge, uint32_t address,
                                      uint8_t *output, size_t length)
{
    uint8_t staged[1024];
    uint64_t cursor = address;
    size_t copied = 0;
    if (!read || !output || length > sizeof(staged)) {
        return false;
    }
    while (copied < length) {
        uint64_t page = cursor / 4096, offset = cursor % 4096;
        if (page > max_sge || page > (UINT64_MAX - sge_base) / 8) {
            return false;
        }
        uint64_t descriptor = sge_base + page * 8;
        if (descriptor > ram_size || ram_size - descriptor < 4) {
            return false;
        }
        uint8_t bytes[4];
        if (!read(opaque, descriptor, bytes, sizeof(bytes))) {
            return false;
        }
        uint64_t physical = (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
                            (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
        physical += offset;
        size_t chunk = 4096 - offset;
        if (chunk > length - copied) {
            chunk = length - copied;
        }
        if (physical > ram_size || chunk > ram_size - physical ||
            !read(opaque, physical, staged + copied, chunk)) {
            return false;
        }
        copied += chunk;
        cursor += chunk;
    }
    memcpy(output, staged, length);
    return true;
}
#endif

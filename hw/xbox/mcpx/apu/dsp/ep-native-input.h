/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_EP_NATIVE_INPUT_H
#define XEMU_EP_NATIVE_INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* This route is specific to an identified EP program. Callers must validate
 * the complete program and device state before relying on these addresses.
 * Scratch reads must be side-effect-free; this function never writes guest
 * memory or publishes partially read inputs. */
typedef bool (*EPNativeScratchRead)(void *opaque, uint32_t address,
                                    uint8_t *bytes, size_t length);
typedef bool (*EPNativeOverlayCheck)(void *opaque, const uint8_t *bytes,
                                     size_t length);

static inline bool ep_native_prepare_input(void *opaque,
                                           EPNativeScratchRead read,
                                           EPNativeOverlayCheck overlay_check,
                                           unsigned phase, uint32_t slot,
                                           uint32_t output[5][256])
{
    uint32_t staged[5][256];
    uint8_t bytes[1024];
    if (!read || !overlay_check || !output || phase >= 6 ||
        (slot != 0 && slot != 0x100)) {
        return false;
    }
    if (phase == 0 && (!read(opaque, 0x73f8, bytes, 96) ||
                       !overlay_check(opaque, bytes, 96))) {
        return false;
    }
    if (!read(opaque, 0x70d4, bytes, 804) ||
        !overlay_check(opaque, bytes, 804)) {
        return false;
    }
    for (unsigned ch = 0; ch < 5; ch++) {
        if (!read(opaque, 0x10000 + slot * 4 + ch * 2048, bytes, 1024)) {
            return false;
        }
        for (unsigned i = 0; i < 256; i++) {
            const uint8_t *p = bytes + 4 * i;
            uint32_t word = (uint32_t)p[0] | (uint32_t)p[1] << 8 |
                            (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
            if (word > 0xffffff) {
                return false;
            }
            staged[ch][i] = word;
        }
    }
    memcpy(output, staged, sizeof(staged));
    return true;
}
#endif

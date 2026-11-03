/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_EP_MIX_HLE_H
#define XEMU_EP_MIX_HLE_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static inline int32_t ep_mix_signed24(uint32_t word)
{
    return word & 0x800000 ? (int32_t)word - 0x1000000 : (int32_t)word;
}

static inline int64_t ep_mix_floor_shift(int64_t value, unsigned bits)
{
    return value >= 0 ? value >> bits :
                        -((-value + ((int64_t)1 << bits) - 1) >> bits);
}

static inline uint32_t ep_mix_isqrt(uint64_t value)
{
    uint64_t result = 0, bit = UINT64_C(1) << 62;
    while (bit > value) {
        bit >>= 2;
    }
    while (bit) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return result;
}

/* Private 5-channel to stereo prediction for one verified EP program and
 * overlay. The caller must verify both identities and the input ordering. */
typedef struct EPMixFrame {
    uint32_t mixed[2][256];
    uint32_t peak[2], rms[2];
    uint8_t pcm[1024];
    bool negative_clip;
} EPMixFrame;

static inline bool ep_mix_frame_hle(const uint32_t input[5][256],
                                    EPMixFrame *output)
{
    EPMixFrame result;
    uint64_t squares[2] = { 0, 0 };
    result.peak[0] = result.peak[1] = 0;
    result.negative_clip = false;
    if (!input || !output) {
        return false;
    }
    for (unsigned i = 0; i < 256; i++) {
        int32_t samples[5];
        for (unsigned ch = 0; ch < 5; ch++) {
            if (input[ch][i] > 0xffffff) {
                return false;
            }
            samples[ch] = ep_mix_signed24(input[ch][i]);
        }
        for (unsigned ch = 0; ch < 2; ch++) {
            int32_t front = samples[ch ? 2 : 0];
            int32_t surround = samples[ch ? 4 : 3];
            int64_t sum = (int64_t)front * 0x7fffff +
                          ((int64_t)samples[1] + surround) * 0x5a8279;
            int64_t q23 = ep_mix_floor_shift(sum + (INT64_C(1) << 22), 23);
            if (((uint64_t)sum & 0x7fffff) == 0x400000 && q23 % 2 != 0) {
                q23--;
            }
            result.negative_clip |= q23 < -0x800000;
            q23 = q23 < -0x800000 ? -0x800000 : q23 > 0x7fffff ? 0x7fffff : q23;
            result.mixed[ch][i] = (uint32_t)q23 & 0xffffff;
            uint32_t magnitude = q23 < 0 ? -q23 : q23;
            magnitude = magnitude > 0x7fffff ? 0x7fffff : magnitude;
            if (magnitude > result.peak[ch]) {
                result.peak[ch] = magnitude;
            }
            /* 256 squared 24-bit samples fit within 54 bits. */
            squares[ch] += q23 * q23;
            uint16_t sample = (uint16_t)ep_mix_floor_shift(q23, 8);
            unsigned offset = 4 * i + 2 * ch;
            result.pcm[offset] = sample;
            result.pcm[offset + 1] = sample >> 8;
        }
    }
    for (unsigned ch = 0; ch < 2; ch++) {
        result.rms[ch] = ep_mix_isqrt(squares[ch] / 256);
    }
    *output = result;
    return true;
}

/* Observed for one identified program; diagnostic only, not a DSP rule. */
static inline uint32_t ep_mix_shadow_status(uint32_t before,
                                            const EPMixFrame *frame)
{
    return (before & ~UINT32_C(0x3f)) | 0x10 |
           (frame->negative_clip ? 0x40 : 0);
}

static inline bool ep_mix_hle(const uint32_t input[5][256],
                              uint8_t output[1024])
{
    EPMixFrame frame;
    if (!output || !ep_mix_frame_hle(input, &frame)) {
        return false;
    }
    memcpy(output, frame.pcm, sizeof(frame.pcm));
    return true;
}
#endif

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

/* Private 5-channel to stereo prediction for one verified EP program and
 * overlay. The caller must verify both identities and the input ordering. */
typedef struct EPMixFrame {
    uint32_t mixed[2][256];
    uint8_t pcm[1024];
} EPMixFrame;

static inline bool ep_mix_frame_hle(const uint32_t input[5][256],
                                    EPMixFrame *output)
{
    EPMixFrame result;
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
            q23 = q23 < -0x800000 ? -0x800000 : q23 > 0x7fffff ? 0x7fffff : q23;
            result.mixed[ch][i] = (uint32_t)q23 & 0xffffff;
            uint16_t sample = (uint16_t)ep_mix_floor_shift(q23, 8);
            unsigned offset = 4 * i + 2 * ch;
            result.pcm[offset] = sample;
            result.pcm[offset + 1] = sample >> 8;
        }
    }
    *output = result;
    return true;
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

/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_GP_DECIMATE_HLE_H
#define XEMU_GP_DECIMATE_HLE_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Candidate for one observed 32-sample GP route. The qualified phases yield
 * exactly six outputs. Unsupported inputs leave all caller outputs untouched.
 */
static inline bool gp_decimate_hle(const uint32_t input[32], uint32_t gain,
                                   unsigned phase, uint32_t output[6],
                                   unsigned *count, unsigned *next_phase)
{
    if (!input || !output || !count || !next_phase || gain > 0xffffff ||
        (phase != 5 && phase != 6)) {
        return false;
    }

    int32_t signed_gain =
        gain & 0x800000 ? (int32_t)gain - 0x1000000 : (int32_t)gain;
    uint32_t result[6];
    unsigned produced = 0;
    for (unsigned i = 0; i < 32; i++) {
        if (input[i] > 0xffffff) {
            return false;
        }
        if (phase >= 6) {
            int32_t sample = input[i] & 0x800000 ?
                                 (int32_t)input[i] - 0x1000000 :
                                 (int32_t)input[i];
            int64_t product = (int64_t)sample * signed_gain;
            int64_t scaled = product >= 0 ? product / 8388608 :
                                            -((-product + 8388607) / 8388608);
            int64_t amplified = scaled * 9;
            if (amplified < -0x800000 || amplified > 0x7fffff ||
                produced >= 6) {
                return false;
            }
            result[produced++] = (uint32_t)amplified & 0xffffff;
            phase = 0;
        }
        phase++;
    }
    if (produced != 6) {
        return false;
    }
    memcpy(output, result, sizeof(result));
    *count = produced;
    *next_phase = phase;
    return true;
}

#endif

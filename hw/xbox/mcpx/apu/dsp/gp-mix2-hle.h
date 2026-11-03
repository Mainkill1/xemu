/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_GP_MIX2_HLE_H
#define XEMU_GP_MIX2_HLE_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static inline int64_t gp_mix2_signed24(uint32_t v)
{
    return v & 0x800000 ? (int64_t)v - 0x1000000 : v;
}

static inline int64_t gp_mix2_floor_shift(int64_t value, unsigned bits)
{
    int64_t divisor = INT64_C(1) << bits;
    return value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor);
}

/* Round a signed accumulator to the nearest multiple of 2^24, ties to even. */
static inline int64_t gp_mix2_round24(int64_t value)
{
    int64_t word = gp_mix2_floor_shift(value + 0x800000, 24);
    if (((uint64_t)value & 0xffffff) == 0x800000 && (word & 1)) {
        word--;
    }
    return word * 0x1000000;
}

/* Candidate for one identified 16-sample two-output GP stage, in the
 * observed unscaled convergent-rounding mode. Reject unqualified clipping.
 * No caller output is modified until the whole block has succeeded. */
static inline bool gp_mix2_hle(const uint32_t x[32], const uint32_t y[32],
                               const uint32_t z[16], uint32_t gain0,
                               uint32_t gain1, uint32_t gain2, uint32_t gain3,
                               uint32_t output[32])
{
    if (!x || !y || !z || !output || gain0 > 0xffffff || gain1 > 0xffffff ||
        gain2 > 0xffffff || gain3 > 0xffffff) {
        return false;
    }
    int64_t a = gp_mix2_signed24(gain0), b = gp_mix2_signed24(gain1);
    int64_t c = gp_mix2_signed24(gain2);
    int64_t d = gp_mix2_floor_shift(gp_mix2_signed24(gain3), 1);
    uint32_t result[32];
    for (unsigned i = 0; i < 16; i++) {
        if (x[i] > 0xffffff || x[i + 16] > 0xffffff || y[i] > 0xffffff ||
            y[i + 16] > 0xffffff || z[i] > 0xffffff) {
            return false;
        }
        int64_t zi = gp_mix2_signed24(z[i]);
        int64_t lo = 2 * a * gp_mix2_signed24(x[i]);
        int64_t hi = 2 * b * gp_mix2_signed24(x[i + 16]);
        lo = gp_mix2_round24(lo + 2 * c * gp_mix2_signed24(y[i]));
        hi = gp_mix2_round24(hi + 2 * c * gp_mix2_signed24(y[i + 16]));
        lo = gp_mix2_round24(lo + 2 * d * zi);
        hi = gp_mix2_round24(hi + 2 * d * zi);
        if (lo < -(INT64_C(1) << 47) || lo >= (INT64_C(1) << 47) ||
            hi < -(INT64_C(1) << 47) || hi >= (INT64_C(1) << 47)) {
            return false;
        }
        lo = gp_mix2_floor_shift(lo * 128, 24);
        hi = gp_mix2_floor_shift(hi * 128, 24);
        if (lo < -0x800000 || lo > 0x7fffff || hi < -0x800000 ||
            hi > 0x7fffff) {
            return false;
        }
        result[i] = (uint32_t)lo & 0xffffff;
        result[i + 16] = (uint32_t)hi & 0xffffff;
    }
    memcpy(output, result, sizeof(result));
    return true;
}
#endif

/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_GP_FILTER_FRAME_HLE_H
#define XEMU_GP_FILTER_FRAME_HLE_H

#include <stdbool.h>
#include <stdint.h>

/* Private predictions for the eight observed two-section GP filters. */
typedef struct GPFilterFrame {
    uint32_t samples[8][32];
    uint32_t history[8][8];
    uint32_t last_input[8];
} GPFilterFrame;

static inline int32_t gp_filter_frame_signed24(uint32_t value)
{
    return value & 0x800000 ? (int32_t)value - 0x1000000 : (int32_t)value;
}

static inline int64_t gp_filter_frame_floor_shift(int64_t value, unsigned bits)
{
    return value >= 0 ? value >> bits :
                        -((-value + ((int64_t)1 << bits) - 1) >> bits);
}

static inline bool gp_filter_frame_fits24(int64_t value)
{
    return value >= -0x800000 && value <= 0x7fffff;
}

/* Caller establishes the exact program identity. Input and guest memory are
 * never modified, including on rejection of a later channel or sample. */
static inline bool gp_filter_frame_hle(const uint32_t xram[4096],
                                       const uint32_t yram[2048],
                                       GPFilterFrame *output)
{
    GPFilterFrame result;
    if (!xram || !yram || !output) {
        return false;
    }
    for (unsigned ch = 0; ch < 8; ch++) {
        unsigned base = 0x80 + 39 * ch, state = 8 * ch;
        unsigned dest = 0xd60 + 32 * ch;
        int32_t c[11], history[8];
        if (xram[base] != 0xc000 || xram[base + 1] != 0 ||
            xram[base + 2] != state || xram[base + 3] != 8 ||
            xram[base + 4] != 1 || xram[base + 5] != 0x1560 + 32 * ch ||
            xram[base + 6] != dest) {
            return false;
        }
        for (unsigned i = 0; i < 11; i++) {
            if (xram[base + 7 + i] > 0xffffff) {
                return false;
            }
            c[i] = gp_filter_frame_signed24(xram[base + 7 + i]);
        }
        for (unsigned i = 0; i < 8; i++) {
            if (yram[state + i] > 0xffffff) {
                return false;
            }
            history[i] = gp_filter_frame_signed24(yram[state + i]);
        }
        for (unsigned i = 0; i < 32; i++) {
            if (xram[dest + i] > 0xffffff) {
                return false;
            }
            int32_t input = gp_filter_frame_signed24(xram[dest + i]);
            int64_t first = (int64_t)c[1] * input;
            for (unsigned j = 0; j < 4; j++) {
                first += (int64_t)c[2 + j] * history[j];
            }
            int64_t q1 = gp_filter_frame_floor_shift(first, 22);
            if (!gp_filter_frame_fits24(q1)) {
                return false;
            }
            int64_t amplified = q1 * 16;
            amplified = amplified < -0x800000 ? -0x800000 :
                        amplified > 0x7fffff  ? 0x7fffff :
                                                amplified;
            int64_t middle = gp_filter_frame_floor_shift(amplified * c[0], 23);
            if (!gp_filter_frame_fits24(middle)) {
                return false;
            }
            int64_t second = (int64_t)c[6] * middle;
            for (unsigned j = 0; j < 4; j++) {
                second += (int64_t)c[7 + j] * history[4 + j];
            }
            int64_t q2 = gp_filter_frame_floor_shift(second, 22);
            if (!gp_filter_frame_fits24(q2)) {
                return false;
            }
            result.samples[ch][i] =
                (uint32_t)gp_filter_frame_floor_shift(second, 23) & 0xffffff;
            history[1] = history[0];
            history[0] = input;
            history[3] = history[2];
            history[2] = q1;
            history[5] = history[4];
            history[4] = middle;
            history[7] = history[6];
            history[6] = q2;
            result.last_input[ch] = (uint32_t)input & 0xffffff;
        }
        for (unsigned i = 0; i < 8; i++) {
            result.history[ch][i] = (uint32_t)history[i] & 0xffffff;
        }
    }
    *output = result;
    return true;
}
#endif

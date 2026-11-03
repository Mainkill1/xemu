/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_GP_FEEDBACK_HLE_H
#define XEMU_GP_FEEDBACK_HLE_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static inline int32_t gp_feedback_signed24(uint32_t value)
{
    return value & 0x800000 ? (int32_t)value - 0x1000000 : (int32_t)value;
}

/* A bounded candidate for one observed 16-sample feedback stage. The caller
 * must establish program identity and routing. Saturating cases remain DSP
 * work until their exact behavior is qualified. Outputs are transactional. */
static inline bool gp_feedback_hle(const uint32_t input[16], uint32_t initial,
                                   uint32_t feedback_gain, uint32_t input_gain,
                                   uint32_t output[16], uint32_t *final_state)
{
    if (!input || !output || !final_state || initial > 0xffffff ||
        feedback_gain > 0xffffff || input_gain > 0xffffff) {
        return false;
    }
    int64_t state = gp_feedback_signed24(initial);
    int64_t a = gp_feedback_signed24(feedback_gain);
    int64_t b = gp_feedback_signed24(input_gain);
    uint32_t result[16];
    for (unsigned i = 0; i < 16; i++) {
        if (input[i] > 0xffffff) {
            return false;
        }
        int64_t sum = state * a + gp_feedback_signed24(input[i]) * b;
        state = sum >= 0 ? sum / 8388608 : -((-sum + 8388607) / 8388608);
        if (state < -0x800000 || state > 0x7fffff) {
            return false;
        }
        result[i] = (uint32_t)state & 0xffffff;
    }
    memcpy(output, result, sizeof(result));
    *final_state = result[15];
    return true;
}
#endif

/*
 * MCPX APU voice arithmetic helpers
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_XBOX_MCPX_APU_VP_VOICE_MATH_H
#define HW_XBOX_MCPX_APU_VP_VOICE_MATH_H

#include <stdint.h>

#define MCPX_APU_ATTENUATION_TABLE_SIZE (1U << 12)

void mcpx_apu_attenuation_table_init(
    float table[MCPX_APU_ATTENUATION_TABLE_SIZE]);

static inline float mcpx_apu_attenuation_lookup(
    const float table[MCPX_APU_ATTENUATION_TABLE_SIZE], uint16_t volume)
{
    return table[volume & (MCPX_APU_ATTENUATION_TABLE_SIZE - 1)];
}

#endif

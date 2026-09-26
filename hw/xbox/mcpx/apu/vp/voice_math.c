/*
 * MCPX APU voice arithmetic helpers
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <math.h>

#include "voice_math.h"

void mcpx_apu_attenuation_table_init(
    float table[MCPX_APU_ATTENUATION_TABLE_SIZE])
{
    for (unsigned int volume = 0;
         volume < MCPX_APU_ATTENUATION_TABLE_SIZE - 1; volume++) {
        table[volume] = powf(10.0f, volume / (64.0 * -20.0f));
    }
    table[MCPX_APU_ATTENUATION_TABLE_SIZE - 1] = 0.0f;
}

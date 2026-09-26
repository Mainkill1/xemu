/*
 * MCPX APU voice attenuation tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hw/xbox/mcpx/apu/vp/voice_math.h"

static uint32_t float_bits(float value)
{
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static float attenuation_reference(uint16_t volume)
{
    volume &= MCPX_APU_ATTENUATION_TABLE_SIZE - 1;
    return volume == 0xfff ? 0.0f
                           : powf(10.0f, volume / (64.0 * -20.0f));
}

static int test_attenuation_exhaustive(void)
{
    float table[MCPX_APU_ATTENUATION_TABLE_SIZE];

    mcpx_apu_attenuation_table_init(table);

    for (unsigned int volume = 0; volume <= UINT16_MAX; volume++) {
        float expected = attenuation_reference(volume);
        float actual = mcpx_apu_attenuation_lookup(table, volume);

        if (float_bits(actual) != float_bits(expected)) {
            fprintf(stderr,
                    "volume %u: expected 0x%08x, received 0x%08x\n",
                    volume, float_bits(expected), float_bits(actual));
            return 1;
        }
    }
    return 0;
}

static int test_attenuation_shape(void)
{
    float table[MCPX_APU_ATTENUATION_TABLE_SIZE];

    mcpx_apu_attenuation_table_init(table);

    if (float_bits(table[0]) != float_bits(1.0f)) {
        return 1;
    }
    for (unsigned int volume = 1;
         volume < MCPX_APU_ATTENUATION_TABLE_SIZE - 1; volume++) {
        if (!(table[volume] < table[volume - 1]) || table[volume] <= 0.0f) {
            fprintf(stderr, "invalid attenuation shape at volume %u\n", volume);
            return 1;
        }
    }
    return float_bits(table[0xfff]) != float_bits(0.0f);
}

int main(void)
{
    int exhaustive = test_attenuation_exhaustive();
    int shape = test_attenuation_shape();

    puts("TAP version 13");
    puts("1..2");
    printf("%s 1 - attenuation lookup matches every 16-bit input\n",
           exhaustive ? "not ok" : "ok");
    printf("%s 2 - attenuation table has the expected shape\n",
           shape ? "not ok" : "ok");
    return exhaustive || shape;
}

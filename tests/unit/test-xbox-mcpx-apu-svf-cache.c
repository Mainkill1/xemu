/*
 * MCPX APU state-variable filter parameter cache tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hw/xbox/mcpx/apu/vp/svf.h"

static int test_svf_cache_lifecycle(void)
{
    sv_filter filter = { 0 };

    if (!svf_config_update(&filter, -4096, 0x4000)) {
        return 1;
    }
    if (svf_config_update(&filter, -4096, 0x4000)) {
        return 1;
    }
    if (!svf_config_update(&filter, -4095, 0x4000)) {
        return 1;
    }
    if (!svf_config_update(&filter, -4095, 0x4001)) {
        return 1;
    }

    memset(&filter, 0, sizeof(filter));
    return !svf_config_update(&filter, -4095, 0x4001);
}

static int test_svf_cache_preserves_processing_state(void)
{
    sv_filter filter = {
        .h = 1.0f,
        .b = 2.0f,
        .l = 3.0f,
        .p = 4.0f,
        .n = 5.0f,
    };

    if (!svf_config_update(&filter, 1234, 5678)) {
        return 1;
    }

    return filter.h != 1.0f || filter.b != 2.0f || filter.l != 3.0f ||
           filter.p != 4.0f || filter.n != 5.0f;
}

static uint32_t float_bits(float value)
{
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static int test_svf_cache_output_parity(void)
{
    static const struct {
        int16_t raw_fc;
        uint16_t raw_q;
        float fc;
        float q;
    } blocks[] = {
        { -4096, 0x2000, 0.5f, 0.25f },
        { -4096, 0x2000, 0.5f, 0.25f },
        { -4096, 0x2000, 0.5f, 0.25f },
        { 1024, 0x6000, 0.75f, 0.75f },
        { 1024, 0x6000, 0.75f, 0.75f },
        { -4096, 0x2000, 0.5f, 0.25f },
    };
    sv_filter reference = { 0 };
    sv_filter cached = { 0 };

    for (unsigned int block = 0; block < sizeof(blocks) / sizeof(blocks[0]);
         block++) {
        setup_svf(&reference, blocks[block].fc, blocks[block].q, F_LP);
        if (svf_config_update(&cached, blocks[block].raw_fc,
                              blocks[block].raw_q)) {
            setup_svf(&cached, blocks[block].fc, blocks[block].q, F_LP);
        }

        for (unsigned int sample = 0; sample < 32; sample++) {
            float input = ((int)((block * 32 + sample) % 17) - 8) / 8.0f;
            float expected = run_svf(&reference, input);
            float actual = run_svf(&cached, input);

            if (float_bits(actual) != float_bits(expected)) {
                fprintf(stderr, "block %u sample %u output mismatch\n", block,
                        sample);
                return 1;
            }
        }
    }
    return 0;
}

int main(void)
{
    int lifecycle = test_svf_cache_lifecycle();
    int state = test_svf_cache_preserves_processing_state();
    int parity = test_svf_cache_output_parity();

    puts("TAP version 13");
    puts("1..3");
    printf("%s 1 - SVF cache detects parameter changes and resets\n",
           lifecycle ? "not ok" : "ok");
    printf("%s 2 - SVF cache leaves processing history unchanged\n",
           state ? "not ok" : "ok");
    printf("%s 3 - cached and repeated setup produce identical output\n",
           parity ? "not ok" : "ok");
    return lifecycle || state || parity;
}

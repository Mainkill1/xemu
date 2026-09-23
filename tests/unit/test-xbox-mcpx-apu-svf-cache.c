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

int main(void)
{
    int lifecycle = test_svf_cache_lifecycle();
    int state = test_svf_cache_preserves_processing_state();

    puts("TAP version 13");
    puts("1..2");
    printf("%s 1 - SVF cache detects parameter changes and resets\n",
           lifecycle ? "not ok" : "ok");
    printf("%s 2 - SVF cache leaves processing history unchanged\n",
           state ? "not ok" : "ok");
    return lifecycle || state;
}

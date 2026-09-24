/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <stdbool.h>
#include <stdio.h>

#include "hw/xbox/mcpx/apu/vp/worker-count.h"

static int failures;
static int checks;

static void check_count(const char *name, int actual, int expected)
{
    checks++;
    if (actual == expected) {
        printf("ok %d - %s\n", checks, name);
        return;
    }

    failures++;
    printf("not ok %d - %s: expected %d, got %d\n", checks, name,
           expected, actual);
}

int main(void)
{
    puts("TAP version 13");

    check_count("explicit one wins",
                mcpx_apu_voice_worker_count(1, 16, true), 1);
    check_count("explicit four wins",
                mcpx_apu_voice_worker_count(4, 16, false), 4);
    check_count("explicit maximum wins",
                mcpx_apu_voice_worker_count(16, 4, true), 16);
    check_count("explicit value is bounded",
                mcpx_apu_voice_worker_count(17, 4, false), 16);
    check_count("only zero selects auto",
                mcpx_apu_voice_worker_count(-1, 16, true), 1);

    check_count("sinc auto uses four logical CPUs",
                mcpx_apu_voice_worker_count(0, 4, false), 4);
    check_count("sinc auto uses eight logical CPUs",
                mcpx_apu_voice_worker_count(0, 8, false), 8);
    check_count("sinc auto is bounded",
                mcpx_apu_voice_worker_count(0, 32, false), 16);

    check_count("linear auto uses two logical CPUs",
                mcpx_apu_voice_worker_count(0, 2, true), 2);
    check_count("linear auto caps eight logical CPUs",
                mcpx_apu_voice_worker_count(0, 8, true), 4);
    check_count("linear auto caps sixteen logical CPUs",
                mcpx_apu_voice_worker_count(0, 16, true), 4);
    check_count("auto never returns zero",
                mcpx_apu_voice_worker_count(0, 0, true), 1);

    printf("1..%d\n", checks);
    return failures != 0;
}

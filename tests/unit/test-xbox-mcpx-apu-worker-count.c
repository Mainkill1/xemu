/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <limits.h>
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

/* Pin the public policy, independently of the implementation constants. */
static bool test_all_explicit_overrides(void)
{
    static const int configured[] = {
        INT_MIN, -1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
        16, 17, INT_MAX,
    };
    static const int hosts[] = {
        INT_MIN, -1, 0, 1, 3, 4, 8, 16, 32, INT_MAX,
    };

    for (size_t c = 0; c < sizeof(configured) / sizeof(configured[0]); c++) {
        int expected = configured[c] < 1 ? 1 :
                       configured[c] > 16 ? 16 : configured[c];

        for (size_t h = 0; h < sizeof(hosts) / sizeof(hosts[0]); h++) {
            for (int linear = 0; linear <= 1; linear++) {
                int actual = mcpx_apu_voice_worker_count(
                    configured[c], hosts[h], linear);

                if (actual != expected) {
                    printf("# configured=%d host=%d linear=%d: "
                           "expected %d, got %d\n", configured[c], hosts[h],
                           linear, expected, actual);
                    return false;
                }
            }
        }
    }
    return true;
}

static bool test_auto_boundaries(void)
{
    static const struct {
        int host;
        int sinc;
        int linear;
    } cases[] = {
        { INT_MIN, 1, 1 }, { -1, 1, 1 }, { 0, 1, 1 }, { 1, 1, 1 },
        { 2, 2, 2 }, { 3, 3, 3 }, { 4, 4, 4 }, { 5, 5, 4 },
        { 8, 8, 4 }, { 16, 16, 4 }, { 17, 16, 4 }, { 32, 16, 4 },
        { INT_MAX, 16, 4 },
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        for (int linear = 0; linear <= 1; linear++) {
            int expected = linear ? cases[i].linear : cases[i].sinc;
            int actual = mcpx_apu_voice_worker_count(0, cases[i].host, linear);

            if (actual != expected) {
                printf("# auto host=%d linear=%d: expected %d, got %d\n",
                       cases[i].host, linear, expected, actual);
                return false;
            }
        }
    }
    return true;
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

    check_count("every explicit override ignores host and resampler policy",
                test_all_explicit_overrides(), true);
    check_count("auto respects host boundaries and converter-specific caps",
                test_auto_boundaries(), true);

    printf("1..%d\n", checks);
    return failures != 0;
}

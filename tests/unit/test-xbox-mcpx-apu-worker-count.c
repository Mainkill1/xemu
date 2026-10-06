/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "hw/xbox/mcpx/apu/vp/worker-count.h"

static const struct {
    int logical_cpus;
    int automatic_workers;
} hosts[] = {
    { INT_MIN, 1 }, { -1, 1 }, { 0, 1 }, { 1, 1 }, { 2, 2 }, { 3, 3 },
    { 4, 4 }, { 5, 4 }, { 8, 4 }, { 16, 4 }, { 17, 4 }, { 32, 4 },
    { INT_MAX, 4 },
};

static void test_auto(void)
{
    for (size_t h = 0; h < ARRAY_SIZE(hosts); h++) {
        g_assert_cmpint(mcpx_apu_voice_worker_count(0, hosts[h].logical_cpus),
                        ==, hosts[h].automatic_workers);
    }
}

static void test_explicit(void)
{
    /* Literal expected values pin compatibility, including invalid config. */
    static const struct {
        int requested;
        int effective;
    } overrides[] = {
        { INT_MIN, 1 }, { -1, 1 }, { 1, 1 }, { 2, 2 }, { 3, 3 },
        { 4, 4 }, { 5, 5 }, { 6, 6 }, { 7, 7 }, { 8, 8 }, { 9, 9 },
        { 10, 10 }, { 11, 11 }, { 12, 12 }, { 13, 13 }, { 14, 14 },
        { 15, 15 }, { 16, 16 }, { 17, 16 }, { INT_MAX, 16 },
    };

    for (size_t c = 0; c < ARRAY_SIZE(overrides); c++) {
        for (size_t h = 0; h < ARRAY_SIZE(hosts); h++) {
            g_assert_cmpint(mcpx_apu_voice_worker_count(
                                overrides[c].requested, hosts[h].logical_cpus),
                            ==, overrides[c].effective);
        }
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/mcpx-apu/workers/auto-four", test_auto);
    g_test_add_func("/mcpx-apu/workers/explicit-overrides", test_explicit);
    return g_test_run();
}

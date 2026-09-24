/*
 * Vulkan background worker scheduling helper tests
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/background-worker-priority.h"

static void test_modes(void)
{
    g_assert_cmpint(pgraph_vk_worker_scheduling_mode_from_string(NULL), ==,
                    PGRAPH_VK_WORKER_SCHEDULING_CURRENT);
    g_assert_cmpint(pgraph_vk_worker_scheduling_mode_from_string("current"), ==,
                    PGRAPH_VK_WORKER_SCHEDULING_CURRENT);
    g_assert_cmpint(pgraph_vk_worker_scheduling_mode_from_string("all-low"), ==,
                    PGRAPH_VK_WORKER_SCHEDULING_ALL_LOW);
    g_assert_cmpint(
        pgraph_vk_worker_scheduling_mode_from_string("split-demand"), ==,
        PGRAPH_VK_WORKER_SCHEDULING_SPLIT_DEMAND);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/vk/background-worker-priority/modes", test_modes);
    return g_test_run();
}

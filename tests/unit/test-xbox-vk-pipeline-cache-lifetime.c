/*
 * NV2A Vulkan graphics pipeline cache lifetime tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/pipeline-cache-lifetime.h"

static void test_entry_lifetime(void)
{
    g_assert_true(pgraph_vk_pipeline_cache_entry_can_evict(false, 10, 10));
    g_assert_true(pgraph_vk_pipeline_cache_entry_can_evict(true, 9, 10));
    g_assert_false(pgraph_vk_pipeline_cache_entry_can_evict(true, 10, 10));
    g_assert_false(pgraph_vk_pipeline_cache_entry_can_evict(true, 11, 10));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/vk-pipeline-cache/entry-lifetime",
                    test_entry_lifetime);
    return g_test_run();
}

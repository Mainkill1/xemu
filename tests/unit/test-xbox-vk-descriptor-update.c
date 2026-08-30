/*
 * Vulkan NV2A descriptor update tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/descriptor-update.h"

static void test_descriptor_write_decision(void)
{
    g_assert_true(pgraph_vk_descriptor_set_needs_write(0, false, false));
    g_assert_true(pgraph_vk_descriptor_set_needs_write(1, true, false));
    g_assert_true(pgraph_vk_descriptor_set_needs_write(1, false, true));
    g_assert_false(pgraph_vk_descriptor_set_needs_write(1, false, false));
}

static void test_dynamic_uniform_offset(void)
{
    uint32_t dynamic_offset = UINT32_MAX;

    g_assert_true(
        pgraph_vk_get_dynamic_uniform_offset(1024, 256, &dynamic_offset));
    g_assert_cmpuint(dynamic_offset, ==, 1024);
    g_assert_false(
        pgraph_vk_get_dynamic_uniform_offset(1025, 256, &dynamic_offset));
    g_assert_false(
        pgraph_vk_get_dynamic_uniform_offset(1024, 0, &dynamic_offset));
    g_assert_false(pgraph_vk_get_dynamic_uniform_offset(
        (uint64_t)UINT32_MAX + 1, 1, &dynamic_offset));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/vk-descriptors/write-decision",
                    test_descriptor_write_decision);
    g_test_add_func("/xbox/vk-descriptors/dynamic-offset",
                    test_dynamic_uniform_offset);
    return g_test_run();
}

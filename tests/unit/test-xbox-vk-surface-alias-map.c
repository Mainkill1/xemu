/*
 * Guest Z24S8 surface alias layout tests
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/xbox/nv2a/pgraph/swizzle.h"
#include "hw/xbox/nv2a/pgraph/vk/surface-alias-map.h"

static void compare_to_guest_swizzle(uint32_t width, uint32_t height)
{
    uint32_t linear[1024];
    uint32_t swizzled[1024] = { 0 };
    uint32_t count = width * height;

    g_assert_cmpuint(count, <=, G_N_ELEMENTS(linear));
    for (uint32_t i = 0; i < count; i++) {
        linear[i] = 0x12000000u | i;
    }
    swizzle_box((const uint8_t *)linear, width, height, 1,
                (uint8_t *)swizzled, width * sizeof(uint32_t), 0,
                sizeof(uint32_t));

    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint32_t index = UINT32_MAX;

            g_assert_true(pgraph_vk_alias_morton_index(
                x, y, width, height, &index));
            g_assert_cmpuint(index, <, count);
            g_assert_cmphex(swizzled[index], ==, linear[y * width + x]);
        }
    }
}

static void test_guest_morton_layout(void)
{
    compare_to_guest_swizzle(32, 32);
    compare_to_guest_swizzle(16, 32);
    compare_to_guest_swizzle(32, 16);
}

static void test_invalid_morton_layout(void)
{
    uint32_t index = UINT32_MAX;

    g_assert_false(pgraph_vk_alias_morton_index(0, 0, 0, 32, &index));
    g_assert_false(pgraph_vk_alias_morton_index(0, 0, 32, 0, &index));
    g_assert_false(pgraph_vk_alias_morton_index(0, 0, 30, 32, &index));
    g_assert_false(pgraph_vk_alias_morton_index(0, 0, 32, 30, &index));
    g_assert_false(pgraph_vk_alias_morton_index(32, 0, 32, 32, &index));
    g_assert_false(pgraph_vk_alias_morton_index(0, 32, 32, 32, &index));
    g_assert_false(pgraph_vk_alias_morton_index(0, 0, 65536, 65536,
                                                &index));
    g_assert_false(pgraph_vk_alias_morton_index(0, 0, 32, 32, NULL));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/vk/surface-alias/guest-morton-layout",
                    test_guest_morton_layout);
    g_test_add_func("/xbox/vk/surface-alias/reject-invalid-shapes",
                    test_invalid_morton_layout);
    return g_test_run();
}

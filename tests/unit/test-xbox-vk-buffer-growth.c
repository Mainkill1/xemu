/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"

#include "hw/xbox/nv2a/pgraph/vk/buffer-growth.h"

static void test_sustained_wrap_reuses_capacity(void)
{
    const uint64_t mib = 1024 * 1024;
    const uint64_t capacity = 8 * mib;
    size_t target = capacity;

    for (unsigned int i = 0; i < 4096; i++) {
        uint64_t before_finish;
        uint64_t after_finish;

        g_assert_true(pgraph_vk_buffer_required_size_checked(
            capacity - 16, 64, 16, &before_finish));
        g_assert_cmpuint(before_finish, >, capacity);
        g_assert_true(pgraph_vk_buffer_required_size_checked(
            0, 64, 16, &after_finish));
        g_assert_cmpuint(after_finish, ==, 64);
        target = pgraph_vk_buffer_growth_target(
            target, capacity, after_finish);
        g_assert_cmpuint(target, ==, capacity);
    }
}

static void test_true_oversize_grows_geometrically(void)
{
    const size_t mib = 1024 * 1024;
    const size_t capacity = 8 * mib;

    g_assert_cmpuint(pgraph_vk_buffer_growth_target(
                         capacity, capacity, capacity + 1),
                     ==, 16 * mib);
}

static void test_growth_near_size_limit_is_exact(void)
{
    size_t current = SIZE_MAX / 2 + 1;
    g_assert_cmpuint(pgraph_vk_buffer_growth_target(
                         current, 0, current + 1),
                     ==, current + 1);
}

static void test_required_size_rejects_bad_layout(void)
{
    uint64_t required;

    g_assert_false(pgraph_vk_buffer_required_size_checked(
        0, 1, 0, &required));
    g_assert_false(pgraph_vk_buffer_required_size_checked(
        UINT64_MAX, 1, 16, &required));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/vulkan/buffer-growth/sustained-wrap",
                    test_sustained_wrap_reuses_capacity);
    g_test_add_func("/xbox/vulkan/buffer-growth/true-oversize",
                    test_true_oversize_grows_geometrically);
    g_test_add_func("/xbox/vulkan/buffer-growth/size-limit",
                    test_growth_near_size_limit_is_exact);
    g_test_add_func("/xbox/vulkan/buffer-growth/invalid-layout",
                    test_required_size_rejects_bad_layout);
    return g_test_run();
}

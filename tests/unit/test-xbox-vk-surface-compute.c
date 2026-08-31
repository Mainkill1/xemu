/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"

#include "hw/xbox/nv2a/pgraph/vk/surface-compute.h"

static void test_size_limit(void)
{
    g_assert_cmpuint(
        pgraph_vk_compute_workgroup_size(1024, 256, 1024), ==, 256);
}

static void test_invocation_limit(void)
{
    g_assert_cmpuint(
        pgraph_vk_compute_workgroup_size(1024, 1024, 64), ==, 64);
}

static void test_even_divisor(void)
{
    g_assert_cmpuint(
        pgraph_vk_compute_workgroup_size(1000, 256, 256), ==, 8);
}

static void test_prime_fallback(void)
{
    g_assert_cmpuint(
        pgraph_vk_compute_workgroup_size(1009, 128, 128), ==, 1);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/vulkan/compute/size-limit", test_size_limit);
    g_test_add_func("/xbox/vulkan/compute/invocation-limit",
                    test_invocation_limit);
    g_test_add_func("/xbox/vulkan/compute/even-divisor", test_even_divisor);
    g_test_add_func("/xbox/vulkan/compute/fallback-one",
                    test_prime_fallback);
    return g_test_run();
}

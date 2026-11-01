/*
 * Production Vulkan alias dispatch admission tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/surface-compute.c"

static void test_dispatch_device_limits(void)
{
    VkPhysicalDeviceLimits limits = { 0 };

    limits.maxStorageBufferRange = 4096;
    limits.maxComputeWorkGroupSize[0] = 256;
    limits.maxComputeWorkGroupInvocations = 32;
    limits.maxComputeWorkGroupCount[0] = 16;
    /* Check invocations independently of the X dimension. */
    g_assert_false(packed_depth_dispatch_limits_allow(&limits, 4096, 64, 16));
    limits.maxComputeWorkGroupInvocations = 64;
    g_assert_true(packed_depth_dispatch_limits_allow(&limits, 4096, 64, 16));
    g_assert_false(packed_depth_dispatch_limits_allow(&limits, 4097, 64, 16));
    g_assert_false(packed_depth_dispatch_limits_allow(&limits, 4096, 64, 17));
    limits.maxComputeWorkGroupSize[0] = 32;
    g_assert_false(packed_depth_dispatch_limits_allow(&limits, 4096, 64, 16));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/vk/alias/device-limits",
                    test_dispatch_device_limits);
    return g_test_run();
}

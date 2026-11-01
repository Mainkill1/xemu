/*
 * Production Vulkan depth conversion dispatch tests.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/renderer.h"

static unsigned updates, dispatches;
static uint32_t groups;
static ComputePipeline pipeline;

static VKAPI_ATTR void VKAPI_CALL update(VkDevice device, uint32_t count,
                                         const VkWriteDescriptorSet *writes,
                                         uint32_t copies,
                                         const VkCopyDescriptorSet *copy)
{
    g_assert_cmpuint(count, ==, 3);
    for (unsigned i = 0; i < count; i++) {
        g_assert_cmpuint(writes[i].pBufferInfo->range, >, 0);
    }
    updates++;
}
static VKAPI_ATTR void VKAPI_CALL bind_pipeline(VkCommandBuffer cmd,
                                                VkPipelineBindPoint point,
                                                VkPipeline value)
{
}
static VKAPI_ATTR void VKAPI_CALL bind_sets(
    VkCommandBuffer cmd, VkPipelineBindPoint point, VkPipelineLayout layout,
    uint32_t first, uint32_t count, const VkDescriptorSet *sets,
    uint32_t offsets, const uint32_t *dynamic)
{
}
static VKAPI_ATTR void VKAPI_CALL push(VkCommandBuffer cmd,
                                       VkPipelineLayout layout,
                                       VkShaderStageFlags flags,
                                       uint32_t offset, uint32_t size,
                                       const void *values)
{
}
static VKAPI_ATTR void VKAPI_CALL dispatch(VkCommandBuffer cmd, uint32_t x,
                                           uint32_t y, uint32_t z)
{
    g_assert_cmpuint(y, ==, 1);
    g_assert_cmpuint(z, ==, 1);
    groups = x;
    dispatches++;
}

void pgraph_vk_begin_debug_marker(PGRAPHVkState *r, VkCommandBuffer cmd,
                                  float color[4], const char *format, ...)
{
}
void pgraph_vk_end_debug_marker(PGRAPHVkState *r, VkCommandBuffer cmd)
{
}

static void init_pipeline(Lru *lru, LruNode *node, const void *key)
{
    pipeline.key = *(const ComputePipelineKey *)key;
}
static bool compare_pipeline(Lru *lru, LruNode *node, const void *key)
{
    return memcmp(&pipeline.key, key, sizeof(pipeline.key)) != 0;
}

static void run_conversion(bool pack, bool downscale, unsigned width,
                           unsigned height, unsigned scale, unsigned max_x,
                           unsigned max_invocations)
{
    g_autofree PGRAPHState *pg = g_new0(PGRAPHState, 1);
    g_autofree PGRAPHVkState *r = g_new0(PGRAPHVkState, 1);
    SurfaceBinding surface = { .width = width, .height = height };
    unsigned units = width * height * (downscale ? 1 : scale * scale);
    surface.host_fmt.vk_format = VK_FORMAT_D24_UNORM_S8_UINT;
    pg->vk_renderer_state = r;
    pg->surface_scale_factor = scale;
    r->device_props.limits.maxComputeWorkGroupSize[0] = max_x;
    r->device_props.limits.maxComputeWorkGroupInvocations = max_invocations;
    r->device_props.limits.maxComputeWorkGroupCount[0] = 65535;
    r->device_props.limits.minStorageBufferOffsetAlignment = 16;
    lru_init(&r->compute.pipeline_cache);
    memset(&pipeline, 0, sizeof(pipeline));
    lru_add_free(&r->compute.pipeline_cache, &pipeline.node);
    r->compute.pipeline_cache.init_node = init_pipeline;
    r->compute.pipeline_cache.compare_nodes = compare_pipeline;
    updates = dispatches = groups = 0;
    if (!units) {
        /* An empty job must not consume even an exhausted descriptor pool. */
        r->compute.descriptor_set_index =
            ARRAY_SIZE(r->compute.descriptor_sets);
    }
    int before = r->compute.descriptor_set_index;
    if (pack) {
        pgraph_vk_pack_depth_stencil(pg, &surface, VK_NULL_HANDLE,
                                     VK_NULL_HANDLE, VK_NULL_HANDLE, downscale);
    } else {
        pgraph_vk_unpack_depth_stencil(pg, &surface, VK_NULL_HANDLE,
                                       VK_NULL_HANDLE, VK_NULL_HANDLE);
    }
    g_assert_cmpuint(updates, ==, !!units);
    g_assert_cmpuint(dispatches, ==, !!units);
    g_assert_cmpint(r->compute.descriptor_set_index, ==, before + !!units);
    g_assert_cmpint(r->compute.pipeline_cache.num_used, ==, !!units);
    if (units) {
        unsigned size = pipeline.key.workgroup_size;
        g_assert_cmpuint(size, >, 0);
        g_assert_cmpuint(size, <=, max_x);
        g_assert_cmpuint(size, <=, max_invocations);
        g_assert_cmpuint(size & (size - 1), ==, 0);
        g_assert_cmpuint(groups * size, ==, units);
        g_assert_cmpuint(pipeline.key.operation, ==,
                         pack ? PGRAPH_VK_COMPUTE_PACK_DEPTH_STENCIL :
                                PGRAPH_VK_COMPUTE_UNPACK_DEPTH_STENCIL);
    }
}

static void test_empty_unpack(void)
{
    run_conversion(false, false, 0, 4, 2, 256, 128);
    run_conversion(false, false, 4, 0, 2, 256, 128);
}
static void test_empty_pack(void)
{
    run_conversion(true, false, 0, 4, 2, 256, 128);
    run_conversion(true, false, 4, 0, 2, 256, 128);
    run_conversion(true, true, 0, 4, 2, 256, 128);
    run_conversion(true, true, 4, 0, 2, 256, 128);
}
static void test_restricted(void)
{
    for (unsigned pack = 0; pack < 2; pack++) {
        run_conversion(pack, false, 40, 25, 1, 256, 128);
        run_conversion(pack, false, 32, 16, 2, 64, 256);
        run_conversion(pack, false, 32, 16, 2, 256, 32);
    }
    run_conversion(true, true, 40, 25, 2, 256, 128);
}
int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    vkUpdateDescriptorSets = update;
    vkCmdBindPipeline = bind_pipeline;
    vkCmdBindDescriptorSets = bind_sets;
    vkCmdPushConstants = push;
    vkCmdDispatch = dispatch;

    g_test_add_func("/xbox/vk/surface-compute/empty-pack", test_empty_pack);
    g_test_add_func("/xbox/vk/surface-compute/empty-unpack", test_empty_unpack);
    g_test_add_func("/xbox/vk/surface-compute/restricted", test_restricted);
    return g_test_run();
}

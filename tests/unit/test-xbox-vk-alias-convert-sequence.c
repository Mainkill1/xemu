/*
 * Production depth-alias conversion sequencing tests
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/surface-alias-convert.c"

static unsigned int finishes, capacities, commands, descriptors;
static bool started;

void pgraph_vk_finish(PGRAPHState *pg, FinishReason why)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    g_assert_false(started);
    g_assert_cmpuint(commands, ==, 0);
    r->compute.descriptor_set_index = 0;
    r->in_command_buffer = false;
    finishes++;
}

void pgraph_vk_ensure_buffer_capacity(PGRAPHState *pg, int index,
                                      VkDeviceSize size)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    StorageBuffer *buffer = &r->storage_buffers[index];

    g_assert_false(started);
    capacities++;
    if (buffer->buffer_size < size) {
        if (r->in_command_buffer) {
            pgraph_vk_finish(pg, VK_FINISH_REASON_NEED_BUFFER_SPACE);
        }
        buffer->buffer_size = size;
        buffer->buffer = (VkBuffer)(uintptr_t)(index + 10);
    }
}

VkCommandBuffer pgraph_vk_begin_nondraw_commands(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    g_assert_cmpuint(capacities, ==, 2);
    g_assert_cmpuint(r->compute.descriptor_set_index, <=,
                     ARRAY_SIZE(r->compute.descriptor_sets) - 3);
    started = true;
    return (VkCommandBuffer)(uintptr_t)1;
}

void pgraph_vk_end_nondraw_commands(PGRAPHState *pg, VkCommandBuffer cmd)
{
    g_assert_true(started);
    g_assert_cmpuint(descriptors, ==, 3);
}

#include "hw/xbox/nv2a/pgraph/vk/image.c"

static void consume_descriptor(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    g_assert_true(started);
    g_assert_cmpuint(r->compute.descriptor_set_index, <,
                     ARRAY_SIZE(r->compute.descriptor_sets));
    r->compute.descriptor_set_index++;
    descriptors++;
}

static void check_buffers(PGRAPHState *pg, VkBuffer src, VkBuffer dst,
                          bool source_split)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    int source = source_split ? BUFFER_COMPUTE_DST : BUFFER_COMPUTE_SRC;
    int destination = source_split ? BUFFER_COMPUTE_SRC : BUFFER_COMPUTE_DST;

    g_assert_true(src == r->storage_buffers[source].buffer);
    g_assert_true(dst == r->storage_buffers[destination].buffer);
}

void pgraph_vk_pack_depth_stencil(PGRAPHState *pg, SurfaceBinding *surface,
                                  VkCommandBuffer cmd, VkBuffer src,
                                  VkBuffer dst, bool downscale)
{
    check_buffers(pg, src, dst, true);
    g_assert_cmpuint(surface->width, ==, 640);
    g_assert_cmpuint(surface->height, ==, 2);
    consume_descriptor(pg);
}

bool pgraph_vk_unswizzle_packed_depth(PGRAPHState *pg, VkCommandBuffer cmd,
                                      VkBuffer src, VkDeviceSize src_size,
                                      VkBuffer dst, VkDeviceSize dst_size,
                                      uint32_t width, uint32_t height)
{
    check_buffers(pg, src, dst, false);
    g_assert_true(src != VK_NULL_HANDLE && dst != VK_NULL_HANDLE);
    g_assert_true(src != dst);
    g_assert_cmpuint(src_size, >=, (uint64_t)width * height * 4);
    g_assert_cmpuint(dst_size, >=, (uint64_t)width * height * 4);
    consume_descriptor(pg);
    return true;
}

void pgraph_vk_unpack_depth_stencil(PGRAPHState *pg, SurfaceBinding *surface,
                                    VkCommandBuffer cmd, VkBuffer src,
                                    VkBuffer dst)
{
    check_buffers(pg, src, dst, true);
    consume_descriptor(pg);
}

static VKAPI_ATTR void VKAPI_CALL record_barrier(
    VkCommandBuffer cmd, VkPipelineStageFlags src, VkPipelineStageFlags dst,
    VkDependencyFlags flags, uint32_t memories, const VkMemoryBarrier *memory,
    uint32_t buffers, const VkBufferMemoryBarrier *buffer, uint32_t images,
    const VkImageMemoryBarrier *image)
{
    g_assert_true(started);
    for (unsigned int i = 0; i < images; i++) {
        if (image[i].oldLayout ==
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL &&
            image[i].newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
            g_assert_true(src & VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT);
            g_assert_true(src & VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT);
        }
    }
    commands++;
}

static VKAPI_ATTR void VKAPI_CALL record_image_to_buffer(
    VkCommandBuffer cmd, VkImage src, VkImageLayout layout, VkBuffer dst,
    uint32_t count, const VkBufferImageCopy *regions)
{
    g_assert_true(started);
    g_assert_cmpuint(count, ==, 2);
    for (unsigned i = 0; i < count; i++) {
        g_assert_cmpuint(regions[i].imageExtent.width, ==, 640);
        g_assert_cmpuint(regions[i].imageExtent.height, ==, 2);
    }
    g_assert_cmpuint(regions[1].bufferOffset, ==, 5120);
    commands++;
}

static VKAPI_ATTR void VKAPI_CALL record_buffer_to_image(
    VkCommandBuffer cmd, VkBuffer src, VkImage dst, VkImageLayout layout,
    uint32_t count, const VkBufferImageCopy *regions)
{
    g_assert_true(started);
    commands++;
}

PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier = record_barrier;
PFN_vkCmdCopyImageToBuffer vkCmdCopyImageToBuffer = record_image_to_buffer;
PFN_vkCmdCopyBufferToImage vkCmdCopyBufferToImage = record_buffer_to_image;

static void test_conversion_sequence(gconstpointer data)
{
    unsigned int scenario = GPOINTER_TO_UINT(data);
    g_autofree PGRAPHState *pg = g_new0(PGRAPHState, 1);
    g_autofree PGRAPHVkState *r = g_new0(PGRAPHVkState, 1);
    SurfaceBinding producer = {
        .image = (VkImage)(uintptr_t)1,
        .width = 640,
        .height = 480,
        .vram_addr = 0x200000,
        .host_fmt.vk_format = VK_FORMAT_D24_UNORM_S8_UINT,
    };
    SurfaceBinding view = producer;
    VkPhysicalDeviceLimits *limits = &r->device_props.limits;

    finishes = capacities = commands = descriptors = 0;
    started = false;
    pg->vk_renderer_state = r;
    r->perf.enabled = true;
    pg->surface_scale_factor = 1;
    view.image = (VkImage)(uintptr_t)2;
    view.width = view.height = 32;
    view.swizzle = true;
    limits->minStorageBufferOffsetAlignment = 256;
    limits->maxStorageBufferRange = 2 * 1024 * 1024;
    limits->maxComputeWorkGroupSize[0] = 256;
    limits->maxComputeWorkGroupInvocations = 256;
    limits->maxComputeWorkGroupCount[0] = 65535;
    r->compute.descriptor_set_index =
        ARRAY_SIZE(r->compute.descriptor_sets) - (scenario == 1 ? 2 : 3);
    r->storage_buffers[BUFFER_COMPUTE_DST].buffer = (VkBuffer)(uintptr_t)1;
    r->storage_buffers[BUFFER_COMPUTE_SRC].buffer = (VkBuffer)(uintptr_t)2;
    r->storage_buffers[BUFFER_COMPUTE_DST].buffer_size = 2 * 1024 * 1024;
    r->storage_buffers[BUFFER_COMPUTE_SRC].buffer_size = 2 * 1024 * 1024;
    if (scenario == 2) {
        r->in_command_buffer = true;
        r->storage_buffers[BUFFER_COMPUTE_DST].buffer_size = 1;
    }
    if (scenario == 3) {
        limits->maxComputeWorkGroupCount[0] = 1;
        g_assert_false(pgraph_vk_convert_depth_alias(pg, &producer, &view));
        g_assert_false(started);
        g_assert_cmpuint(capacities + commands + descriptors, ==, 0);
        return;
    }
    g_assert_true(pgraph_vk_convert_depth_alias(pg, &producer, &view));
    g_assert_cmpuint(r->perf.depth_alias_copied_pixels, ==, 1280);
    g_assert_cmpuint(producer.height, ==, 480);
    g_assert_cmpuint(finishes, ==, scenario == 0 ? 0 : 1);
    g_assert_cmpuint(descriptors, ==, 3);
    g_assert_cmpuint(commands, >, 0);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_data_func("/xbox/vk/alias/three-descriptors",
                         GUINT_TO_POINTER(0), test_conversion_sequence);
    g_test_add_data_func("/xbox/vk/alias/descriptor-reset", GUINT_TO_POINTER(1),
                         test_conversion_sequence);
    g_test_add_data_func("/xbox/vk/alias/capacity-reset", GUINT_TO_POINTER(2),
                         test_conversion_sequence);
    g_test_add_data_func("/xbox/vk/alias/reject-before-recording",
                         GUINT_TO_POINTER(3), test_conversion_sequence);
    return g_test_run();
}

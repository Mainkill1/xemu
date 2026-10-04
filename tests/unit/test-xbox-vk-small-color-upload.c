/*
 * Production small color upload staging tests
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/renderer.h"

static VkResult flush_result;
static unsigned int flushes, finishes, begins, copies, transitions;
static VkDeviceSize flushed_offset, copied_offset;
static bool recording;
static unsigned int phase;

/* GPU visibility is external; use the real upload and unswizzle code. */
static VkResult test_flush_allocation(VmaAllocator allocator,
                                      VmaAllocation allocation,
                                      VkDeviceSize offset, VkDeviceSize size)
{
    g_assert_false(recording);
    g_assert_cmpuint(size, ==, 4096);
    flushed_offset = offset;
    flushes++;
    return flush_result;
}

#define vmaFlushAllocation test_flush_allocation
#include "hw/xbox/nv2a/pgraph/vk/surface.c"
#undef vmaFlushAllocation

NV2AStats g_nv2a_stats;

void pgraph_vk_finish(PGRAPHState *pg, FinishReason reason)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    g_assert_false(recording);
    g_assert_cmpint(reason, ==, VK_FINISH_REASON_NEED_BUFFER_SPACE);
    r->storage_buffers[BUFFER_TEXTURE_STAGING].buffer_offset = 0;
    r->in_command_buffer = false;
    finishes++;
}

void pgraph_vk_ensure_buffer_capacity(PGRAPHState *pg, int index,
                                      VkDeviceSize size)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    StorageBuffer *b = &r->storage_buffers[index];

    g_assert_false(recording);
    g_assert_cmpint(index, ==, BUFFER_TEXTURE_STAGING);
    if (b->buffer_size < size) {
        if (r->in_command_buffer) {
            pgraph_vk_finish(pg, VK_FINISH_REASON_NEED_BUFFER_SPACE);
        }
        b->buffer_size = size;
    }
}

bool pgraph_vk_buffer_has_space_for(PGRAPHState *pg, int index,
                                    VkDeviceSize size,
                                    VkDeviceAddress alignment)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    StorageBuffer *b = &r->storage_buffers[index];
    VkDeviceSize offset = ROUND_UP(b->buffer_offset, alignment);

    return offset <= b->buffer_size && size <= b->buffer_size - offset;
}

VkCommandBuffer pgraph_vk_begin_nondraw_commands(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    g_assert_false(recording);
    g_assert_cmpint(flush_result, ==, VK_SUCCESS);
    g_assert_cmpuint(flushes, ==, begins + 1);
    g_assert_cmpuint(phase, ==, 0);
    recording = true;
    r->in_command_buffer = true;
    begins++;
    return (VkCommandBuffer)(uintptr_t)1;
}

void pgraph_vk_end_nondraw_commands(PGRAPHState *pg, VkCommandBuffer cmd)
{
    g_assert_true(recording);
    g_assert_cmpuint(transitions, ==, 2 * begins);
    g_assert_cmpuint(copies, ==, begins);
    g_assert_cmpuint(phase, ==, 4);
    phase = 0;
    recording = false;
}

void pgraph_vk_transition_image_layout(PGRAPHState *pg, VkCommandBuffer cmd,
                                       VkImage image, VkFormat format,
                                       VkImageLayout before,
                                       VkImageLayout after)
{
    bool to_transfer = transitions % 2 == 0;

    g_assert_true(recording);
    g_assert_cmpuint(phase, ==, to_transfer ? 1 : 3);
    g_assert_cmpint(before, ==,
                    to_transfer ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL :
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    g_assert_cmpint(after, ==,
                    to_transfer ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL :
                                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    transitions++;
    phase++;
}

static VKAPI_ATTR void VKAPI_CALL
record_barrier(VkCommandBuffer cmd, VkPipelineStageFlags source,
               VkPipelineStageFlags destination, VkDependencyFlags dependency,
               uint32_t memory_count, const VkMemoryBarrier *memory,
               uint32_t buffer_count, const VkBufferMemoryBarrier *buffer,
               uint32_t image_count, const VkImageMemoryBarrier *image)
{
    g_assert_true(recording);
    g_assert_cmpuint(phase, ==, 0);
    g_assert_cmpuint(source, ==, VK_PIPELINE_STAGE_HOST_BIT);
    g_assert_cmpuint(destination, ==, VK_PIPELINE_STAGE_TRANSFER_BIT);
    g_assert_cmpuint(memory_count, ==, 0);
    g_assert_cmpuint(image_count, ==, 0);
    g_assert_cmpuint(buffer_count, ==, 1);
    g_assert_cmpuint(buffer->offset, ==, flushed_offset);
    g_assert_cmpuint(buffer->size, ==, 4096);
    g_assert_cmpuint(buffer->srcAccessMask, ==, VK_ACCESS_HOST_WRITE_BIT);
    g_assert_cmpuint(buffer->dstAccessMask, ==, VK_ACCESS_TRANSFER_READ_BIT);
    phase++;
}

static VKAPI_ATTR void VKAPI_CALL record_copy(VkCommandBuffer cmd,
                                              VkBuffer buffer, VkImage image,
                                              VkImageLayout layout,
                                              uint32_t count,
                                              const VkBufferImageCopy *region)
{
    g_assert_true(recording);
    g_assert_cmpuint(phase, ==, 2);
    g_assert_cmpuint(count, ==, 1);
    g_assert_cmpint(layout, ==, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    g_assert_cmpuint(region->bufferOffset, ==, flushed_offset);
    g_assert_cmpuint(region->imageSubresource.aspectMask, ==,
                     VK_IMAGE_ASPECT_COLOR_BIT);
    g_assert_cmpuint(region->imageExtent.width, ==, 32);
    g_assert_cmpuint(region->imageExtent.height, ==, 32);
    g_assert_cmpuint(region->imageExtent.depth, ==, 1);
    copied_offset = region->bufferOffset;
    copies++;
    phase++;
}

static void check_pixels(const uint8_t *linear, uint8_t value)
{
    /* Independent Morton probes: (0,0), (1,0), (0,1), (16,0), (31,31). */
    static const size_t offsets[] = { 0, 4, 128, 64, 4092 };

    for (unsigned int i = 0; i < G_N_ELEMENTS(offsets); i++) {
        for (unsigned int channel = 0; channel < 4; channel++) {
            g_assert_cmpuint(linear[offsets[i] + channel], ==, value + i);
        }
    }
    g_assert_cmpuint(linear[8], ==, 0);
}

static void set_pixels(uint8_t *guest, uint8_t value)
{
    static const size_t offsets[] = { 0, 4, 8, 1024, 4092 };

    memset(guest, 0, 4096);
    for (unsigned int i = 0; i < G_N_ELEMENTS(offsets); i++) {
        memset(guest + offsets[i], value + i, 4);
    }
}

static void test_upload(gconstpointer data)
{
    int mode = GPOINTER_TO_INT(data);
    g_autofree NV2AState *d = g_new0(NV2AState, 1);
    g_autofree PGRAPHVkState *r = g_new0(PGRAPHVkState, 1);
    g_autofree uint8_t *guest = g_malloc0(4096);
    g_autofree uint8_t *staged = g_malloc0(16384);
    SurfaceBinding surface = {
        .upload_pending = true,
        .readback_superseded_by_guest = true,
        .image = (VkImage)(uintptr_t)2,
        .host_fmt.vk_format = VK_FORMAT_R8G8B8A8_UNORM,
    };
    StorageBuffer *buffer = &r->storage_buffers[BUFFER_TEXTURE_STAGING];

    flush_result = mode == 3 ? VK_ERROR_MEMORY_MAP_FAILED : VK_SUCCESS;
    flushes = finishes = begins = copies = transitions = 0;
    recording = false;
    phase = 0;
    d->vram_ptr = guest;
    d->pgraph.vk_renderer_state = r;
    d->pgraph.draw_time = 17;
    r->device_props.limits.optimalBufferCopyOffsetAlignment = 256;
    buffer->mapped = staged;
    buffer->buffer_size = mode == 2 ? 1024 : 16384;
    buffer->buffer_offset = mode == 1 ? 16000 : 17;
    r->in_command_buffer = mode != 3;
    vkCmdPipelineBarrier = record_barrier;
    vkCmdCopyBufferToImage = record_copy;
    set_pixels(guest, 20);

    bool uploaded = upload_small_swizzled_color(&d->pgraph, &surface);
    if (mode == 3) {
        g_assert_false(uploaded);
        g_assert_cmpuint(buffer->buffer_offset, ==, 17);
        g_assert_cmpuint(begins, ==, 0);
        g_assert_cmpuint(copies, ==, 0);
        g_assert_cmpuint(transitions, ==, 0);
        g_assert_true(surface.upload_pending);
        g_assert_true(surface.readback_superseded_by_guest);
        g_assert_false(surface.initialized);
        return;
    }

    g_assert_true(uploaded);
    g_assert_cmpuint(copied_offset, ==, mode == 0 ? 256 : 0);
    g_assert_cmpuint(finishes, ==, mode == 0 ? 0 : 1);
    g_assert_false(surface.upload_pending);
    g_assert_false(surface.readback_superseded_by_guest);
    g_assert_true(surface.initialized);
    g_assert_cmpuint(surface.draw_time, ==, 17);
    check_pixels(staged + copied_offset, 20);

    if (mode == 0) {
        /* A guest rewrite before submit gets a new immutable staging slice. */
        VkDeviceSize first = copied_offset;
        set_pixels(guest, 40);
        surface.upload_pending = true;
        surface.readback_superseded_by_guest = true;
        d->pgraph.draw_time++;
        g_assert_true(upload_small_swizzled_color(&d->pgraph, &surface));
        g_assert_cmpuint(copied_offset, ==, 4352);
        check_pixels(staged + first, 20);
        check_pixels(staged + copied_offset, 40);
        g_assert_cmpuint(finishes, ==, 0);
        g_assert_cmpuint(surface.draw_time, ==, 18);
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_data_func("/xbox/vk/small-upload/immutable-slices", NULL,
                         test_upload);
    g_test_add_data_func("/xbox/vk/small-upload/ring-exhaustion",
                         GINT_TO_POINTER(1), test_upload);
    g_test_add_data_func("/xbox/vk/small-upload/capacity-growth",
                         GINT_TO_POINTER(2), test_upload);
    g_test_add_data_func("/xbox/vk/small-upload/flush-failure",
                         GINT_TO_POINTER(3), test_upload);
    return g_test_run();
}

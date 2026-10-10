/*
 * Exercise production PVIDEO resource and upload paths at the GPU API boundary.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/renderer.h"

static unsigned images, views, samplers, freed_images, freed_views,
    freed_samplers;
static unsigned copies, submissions, image_barriers;
static uintptr_t next_handle;
static uint8_t staging[4096];
static uint8_t uploaded[4096];
static VkImageLayout upload_old_layout;
static VkPipelineStageFlags upload_source_stage;
static VkAccessFlags upload_source_access;

#define HANDLE(type) ((type)++next_handle)

static VkResult create_image(VmaAllocator allocator,
                             const VkImageCreateInfo *info,
                             const VmaAllocationCreateInfo *alloc_info,
                             VkImage *image, VmaAllocation *allocation,
                             VmaAllocationInfo *allocation_info)
{
    g_assert_cmpint(info->format, ==, VK_FORMAT_R8G8B8A8_UNORM);
    g_assert_cmpint(info->initialLayout, ==, VK_IMAGE_LAYOUT_UNDEFINED);
    g_assert_cmpuint(info->usage, ==,
                     VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                         VK_IMAGE_USAGE_SAMPLED_BIT);
    images++;
    *image = HANDLE(VkImage);
    *allocation = HANDLE(VmaAllocation);
    return VK_SUCCESS;
}

static void destroy_image(VmaAllocator allocator, VkImage image,
                          VmaAllocation allocation)
{
    g_assert_true(image != VK_NULL_HANDLE);
    g_assert_nonnull(allocation);
    freed_images++;
}

static VkResult create_view(VkDevice device, const VkImageViewCreateInfo *info,
                            const VkAllocationCallbacks *allocator,
                            VkImageView *view)
{
    g_assert_true(info->image != VK_NULL_HANDLE);
    views++;
    *view = HANDLE(VkImageView);
    return VK_SUCCESS;
}

static void destroy_view(VkDevice device, VkImageView view,
                         const VkAllocationCallbacks *allocator)
{
    g_assert_true(view != VK_NULL_HANDLE);
    freed_views++;
}

static VkResult create_sampler(VkDevice device, const VkSamplerCreateInfo *info,
                               const VkAllocationCallbacks *allocator,
                               VkSampler *sampler)
{
    g_assert_cmpint(info->magFilter, ==, VK_FILTER_LINEAR);
    g_assert_cmpint(info->minFilter, ==, VK_FILTER_NEAREST);
    samplers++;
    *sampler = HANDLE(VkSampler);
    return VK_SUCCESS;
}

static void destroy_sampler(VkDevice device, VkSampler sampler,
                            const VkAllocationCallbacks *allocator)
{
    g_assert_true(sampler != VK_NULL_HANDLE);
    freed_samplers++;
}

static VkResult map_memory(VmaAllocator allocator, VmaAllocation allocation,
                           void **mapped)
{
    *mapped = staging;
    return VK_SUCCESS;
}

static void unmap_memory(VmaAllocator allocator, VmaAllocation allocation)
{
}

static VkResult flush_memory(VmaAllocator allocator, VmaAllocation allocation,
                             VkDeviceSize offset, VkDeviceSize size)
{
    g_assert_cmpuint(offset, ==, 0);
    g_assert_true(size == VK_WHOLE_SIZE);
    return VK_SUCCESS;
}

static void
pipeline_barrier(VkCommandBuffer cmd, VkPipelineStageFlags src,
                 VkPipelineStageFlags dst, VkDependencyFlags flags,
                 uint32_t memory_count, const VkMemoryBarrier *memory,
                 uint32_t buffer_count, const VkBufferMemoryBarrier *buffers,
                 uint32_t image_count, const VkImageMemoryBarrier *barriers)
{
    if (image_count) {
        g_assert_cmpuint(image_count, ==, 1);
        g_assert_cmpuint(barriers[0].subresourceRange.aspectMask, ==,
                         VK_IMAGE_ASPECT_COLOR_BIT);
        image_barriers++;
        if (barriers[0].newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            upload_old_layout = barriers[0].oldLayout;
            upload_source_stage = src;
            upload_source_access = barriers[0].srcAccessMask;
        } else {
            g_assert_cmpint(barriers[0].oldLayout, ==,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            g_assert_cmpint(barriers[0].newLayout, ==,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    }
}

static void copy_image(VkCommandBuffer cmd, VkBuffer buffer, VkImage image,
                       VkImageLayout layout, uint32_t count,
                       const VkBufferImageCopy *region)
{
    g_assert_cmpuint(count, ==, 1);
    g_assert_cmpint(layout, ==, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    size_t bytes =
        region[0].imageExtent.width * region[0].imageExtent.height * 4;
    g_assert_cmpuint(bytes, <=, sizeof(uploaded));
    memcpy(uploaded, staging, bytes);
    copies++;
}

VkCommandBuffer pgraph_vk_begin_single_time_commands(PGRAPHState *pg)
{
    return (VkCommandBuffer)(uintptr_t)1;
}

void pgraph_vk_end_single_time_commands(PGRAPHState *pg, VkCommandBuffer cmd,
                                        SingleTimeReason reason, uint64_t bytes)
{
    g_assert_cmpint(reason, ==, VK_SINGLE_TIME_PVIDEO_UPLOAD);
    g_assert_cmpuint(bytes, >, 0);
    submissions++;
}

/* Include the real functions, not a copied decision predicate. Section GC
 * discards unrelated public display entry points from this unit executable. */
#define vmaCreateImage create_image
#define vmaDestroyImage destroy_image
#define vkCreateImageView create_view
#define vkDestroyImageView destroy_view
#define vkCreateSampler create_sampler
#define vkDestroySampler destroy_sampler
#define vmaMapMemory map_memory
#define vmaUnmapMemory unmap_memory
#define vmaFlushAllocation flush_memory
#define vkCmdCopyBufferToImage copy_image
#define vkCmdPipelineBarrier pipeline_barrier
#include "hw/xbox/nv2a/pgraph/vk/display.c"
#include "hw/xbox/nv2a/pgraph/vk/image.c"

static NV2AState *nv2a;
static PGRAPHVkState *renderer;

static void setup(void)
{
    nv2a = g_new0(NV2AState, 1);
    renderer = g_new0(PGRAPHVkState, 1);
    nv2a->pgraph.vk_renderer_state = renderer;
    nv2a->vram_ptr = g_malloc0(4096);
    images = views = samplers = freed_images = freed_views = freed_samplers = 0;
    copies = submissions = image_barriers = 0;
    next_handle = 1;
}

static void teardown(void)
{
    destroy_pvideo_image(&nv2a->pgraph);
    g_assert_cmpuint(images, ==, freed_images);
    g_assert_cmpuint(views, ==, freed_views);
    g_assert_cmpuint(samplers, ==, freed_samplers);
    g_free(nv2a->vram_ptr);
    g_free(renderer);
    g_free(nv2a);
}

static void test_same_size_reuses_resources(void)
{
    setup();
    create_pvideo_image(&nv2a->pgraph, 16, 8);
    VkImage first = renderer->display.pvideo.image;
    for (unsigned i = 0; i < 10000; i++) {
        create_pvideo_image(&nv2a->pgraph, 16, 8);
    }
    g_assert_cmpuint(images, ==, 1);
    g_assert_cmpuint(views, ==, 1);
    g_assert_cmpuint(samplers, ==, 1);
    g_assert_true(renderer->display.pvideo.image == first);
    g_assert_cmpint(renderer->display.pvideo.width, ==, 16);
    g_assert_cmpint(renderer->display.pvideo.height, ==, 8);
    teardown();
}

static void test_resize_replaces_resources(void)
{
    setup();
    create_pvideo_image(&nv2a->pgraph, 16, 8);
    create_pvideo_image(&nv2a->pgraph, 16, 9);
    create_pvideo_image(&nv2a->pgraph, 18, 9);
    g_assert_cmpuint(images, ==, 3);
    g_assert_cmpuint(freed_images, ==, 2);
    g_assert_cmpuint(freed_views, ==, 2);
    g_assert_cmpuint(freed_samplers, ==, 2);
    teardown();
}

static void test_incomplete_resources_are_replaced(void)
{
    setup();
    create_pvideo_image(&nv2a->pgraph, 16, 8);
    destroy_view(renderer->device, renderer->display.pvideo.image_view, NULL);
    renderer->display.pvideo.image_view = VK_NULL_HANDLE;
    /* Keep recorded dimensions matching to attack an image-only reuse check. */
    renderer->display.pvideo.width = 16;
    renderer->display.pvideo.height = 8;
    create_pvideo_image(&nv2a->pgraph, 16, 8);
    g_assert_cmpuint(images, ==, 2);
    g_assert_cmpuint(freed_images, ==, 1);
    g_assert_cmpuint(freed_samplers, ==, 1);
    teardown();
}

static void test_destroy_recreate(void)
{
    setup();
    create_pvideo_image(&nv2a->pgraph, 16, 8);
    destroy_pvideo_image(&nv2a->pgraph);
    destroy_pvideo_image(&nv2a->pgraph);
    g_assert_true(renderer->display.pvideo.image == VK_NULL_HANDLE);
    g_assert_true(renderer->display.pvideo.image_view == VK_NULL_HANDLE);
    g_assert_true(renderer->display.pvideo.sampler == VK_NULL_HANDLE);
    create_pvideo_image(&nv2a->pgraph, 16, 8);
    g_assert_cmpuint(images, ==, 2);
    teardown();
}

static void test_resize_toggle_stress(void)
{
    setup();
    for (unsigned i = 0; i < 1000; i++) {
        int width = (i % 2) ? 18 : 16;
        create_pvideo_image(&nv2a->pgraph, width, 8);
        create_pvideo_image(&nv2a->pgraph, width, 8);
        if (i % 10 == 0) {
            destroy_pvideo_image(&nv2a->pgraph);
        }
        g_assert_cmpuint(images - freed_images, <=, 1);
        g_assert_cmpuint(views - freed_views, <=, 1);
        g_assert_cmpuint(samplers - freed_samplers, <=, 1);
    }
    g_assert_cmpuint(images, ==, 1000);
    teardown();
}

static void test_upload_refreshes_same_size_content(void)
{
    setup();
    PvideoState state = { .in_width = 2, .in_height = 1, .pitch = 4 };
    const uint8_t black[] = { 16, 128, 16, 128 };
    const uint8_t white[] = { 235, 128, 235, 128 };
    const uint8_t black_rgba[] = { 0, 0, 0, 255, 0, 0, 0, 255 };
    const uint8_t white_rgba[] = { 255, 255, 255, 255, 255, 255, 255, 255 };
    memcpy(nv2a->vram_ptr, black, sizeof(black));
    upload_pvideo_image(&nv2a->pgraph, state);
    g_assert_cmpmem(uploaded, 8, black_rgba, 8);
    memcpy(nv2a->vram_ptr + 16, white, sizeof(white));
    state.offset = 16;
    upload_pvideo_image(&nv2a->pgraph, state);
    g_assert_cmpmem(uploaded, 8, white_rgba, 8);
    g_assert_cmpuint(copies, ==, 2);
    g_assert_cmpuint(submissions, ==, 2);
    g_assert_cmpuint(images, ==, 1);
    teardown();
}

static void test_upload_orders_previous_shader_read(void)
{
    setup();
    PvideoState state = { .in_width = 2, .in_height = 1, .pitch = 4 };
    upload_pvideo_image(&nv2a->pgraph, state);
    g_assert_cmpint(upload_old_layout, ==, VK_IMAGE_LAYOUT_UNDEFINED);
    upload_pvideo_image(&nv2a->pgraph, state);
    g_assert_cmpint(upload_old_layout, ==,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    g_assert_cmpuint(upload_source_stage, ==,
                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    g_assert_cmpuint(upload_source_access, ==, VK_ACCESS_SHADER_READ_BIT);
    g_assert_cmpuint(image_barriers, ==, 4);
    state.in_height = 2;
    upload_pvideo_image(&nv2a->pgraph, state);
    g_assert_cmpint(upload_old_layout, ==, VK_IMAGE_LAYOUT_UNDEFINED);
    destroy_pvideo_image(&nv2a->pgraph);
    g_assert_cmpint(renderer->display.pvideo.width, ==, 0);
    g_assert_cmpint(renderer->display.pvideo.height, ==, 0);
    g_assert_cmpint(renderer->display.pvideo.current_layout, ==,
                    VK_IMAGE_LAYOUT_UNDEFINED);
    upload_pvideo_image(&nv2a->pgraph, state);
    g_assert_cmpint(upload_old_layout, ==, VK_IMAGE_LAYOUT_UNDEFINED);
    teardown();
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/pvideo/same-size", test_same_size_reuses_resources);
    g_test_add_func("/pvideo/resize", test_resize_replaces_resources);
    g_test_add_func("/pvideo/incomplete",
                    test_incomplete_resources_are_replaced);
    g_test_add_func("/pvideo/destroy-recreate", test_destroy_recreate);
    g_test_add_func("/pvideo/resize-toggle-stress", test_resize_toggle_stress);
    g_test_add_func("/pvideo/content-refresh",
                    test_upload_refreshes_same_size_content);
    g_test_add_func("/pvideo/read-to-write-order",
                    test_upload_orders_previous_shader_read);
    return g_test_run();
}

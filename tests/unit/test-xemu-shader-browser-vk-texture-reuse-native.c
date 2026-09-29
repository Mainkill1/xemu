/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "../../hw/xbox/nv2a/pgraph/vk/renderer.h"
#include "../../hw/xbox/nv2a/pgraph/shader-browser-geometry-copy.h"
#include "../../hw/xbox/nv2a/pgraph/shader-browser-resource.h"

/* This fixture exercises texture admission/retirement directly. Unused draw
 * helpers must never be reached; keep the production header independent of the
 * pipeline-builder callbacks whose global tables defeat sanitizer section GC.
 */
static void begin_render_pass(PGRAPHState *pg)
{
    g_assert_not_reached();
}
static void end_render_pass(PGRAPHVkState *r)
{
    g_assert_not_reached();
}
#include "../../hw/xbox/nv2a/pgraph/vk/shader-browser-inputs.h"
static void get_size_and_count_for_format(VkFormat format, size_t *size,
                                          size_t *count)
{
    g_assert_not_reached();
}

/* Exercise the production admission and retirement path. These bounded device
 * callbacks copy at command recording and expose allocations/retirement; this
 * fixture does not claim to measure a real Vulkan device or fence. */
static PGRAPHState *fixture_pg;
static PGRAPHVkState *fixture_r;
static TextureBinding fixture_texture;
static uint16_t image_value;
static unsigned copies, allocations, resolutions, destructions;
static unsigned variation;

VkResult vmaCreateBuffer(VmaAllocator allocator,
                         const VkBufferCreateInfo *buffer_info,
                         const VmaAllocationCreateInfo *allocation_info,
                         VkBuffer *buffer, VmaAllocation *allocation,
                         VmaAllocationInfo *info)
{
    void *bytes = g_malloc0(buffer_info->size);
    *buffer = (VkBuffer)(uintptr_t)bytes;
    *allocation = (VmaAllocation)bytes;
    memset(info, 0, sizeof(*info));
    info->pMappedData = bytes;
    ++allocations;
    return VK_SUCCESS;
}
VkResult vmaInvalidateAllocation(VmaAllocator allocator,
                                 VmaAllocation allocation, VkDeviceSize offset,
                                 VkDeviceSize size)
{
    ++resolutions;
    return VK_SUCCESS;
}
void vmaDestroyBuffer(VmaAllocator allocator, VkBuffer buffer,
                      VmaAllocation allocation)
{
    assert((void *)(uintptr_t)buffer == (void *)allocation);
    g_free(allocation);
    ++destructions;
}
void pgraph_vk_transition_image_layout(PGRAPHState *pg, VkCommandBuffer command,
                                       VkImage image, VkFormat format,
                                       VkImageLayout before,
                                       VkImageLayout after)
{
}
static VKAPI_ATTR void VKAPI_CALL copy_image(VkCommandBuffer command,
                                             VkImage image,
                                             VkImageLayout layout,
                                             VkBuffer buffer, uint32_t count,
                                             const VkBufferImageCopy *regions)
{
    assert(count == 1);
    const VkBufferImageCopy *region = regions;
    uint16_t value = image_value +
                     region->imageSubresource.baseArrayLayer * 0x500 +
                     region->imageSubresource.mipLevel * 0x300;
    uint16_t *bytes = (void *)(uintptr_t)buffer;
    for (size_t i = 0;
         i < (size_t)region->imageExtent.width * region->imageExtent.height;
         ++i)
        bytes[i] = value;
    ++copies;
}
static VKAPI_ATTR void VKAPI_CALL
fixture_barrier(VkCommandBuffer command, VkPipelineStageFlags before,
                VkPipelineStageFlags after, VkDependencyFlags flags,
                uint32_t memory_count, const VkMemoryBarrier *memory,
                uint32_t buffer_count, const VkBufferMemoryBarrier *buffers,
                uint32_t image_count, const VkImageMemoryBarrier *images)
{
}

void xemu_test_texture_reuse_start(void);
void xemu_test_texture_reuse_stage(uint64_t token, uint64_t owner,
                                   uint64_t version, uint64_t batch,
                                   uint16_t value, uint32_t mips,
                                   uint32_t faces);
void xemu_test_texture_reuse_retire(uint64_t stats[5], bool reverse);
void xemu_test_texture_reuse_modify(unsigned flags);
uint64_t xemu_test_texture_reuse_pending(void);
uint64_t xemu_test_texture_reuse_pending(void)
{
    return fixture_r->shader_browser_input_pending_bytes;
}
void xemu_test_texture_reuse_modify(unsigned flags)
{
    variation = flags;
}
void xemu_test_texture_reuse_start(void)
{
    (void)pgraph_vk_shader_inputs_begin;
    (void)finish_vk_command;
    assert(!fixture_pg && !fixture_r);
    fixture_pg = g_new0(PGRAPHState, 1);
    fixture_r = g_new0(PGRAPHVkState, 1);
    fixture_pg->vk_renderer_state = fixture_r;
    memset(&fixture_texture, 0, sizeof(fixture_texture));
    fixture_texture.image = (VkImage)(uintptr_t)1;
    fixture_texture.key.state.color_format = 0x30;
    fixture_texture.key.vk_format = VK_FORMAT_R16_UNORM;
    fixture_texture.storage_extent = (VkExtent3D){ 2, 2, 1 };
    fixture_texture.storage_image_type = VK_IMAGE_TYPE_2D;
    fixture_texture.current_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    fixture_texture.input_transfer_src = true;
    fixture_r->texture_bindings[0] = &fixture_texture;
    copies = allocations = resolutions = destructions = 0;
    variation = 0;
    vkCmdCopyImageToBuffer = copy_image;
    vkCmdPipelineBarrier = fixture_barrier;
}
void xemu_test_texture_reuse_stage(uint64_t token, uint64_t owner,
                                   uint64_t version, uint64_t batch,
                                   uint16_t value, uint32_t mips,
                                   uint32_t faces)
{
    fixture_texture.capture_owner = owner;
    fixture_texture.capture_content_version = version;
    fixture_texture.storage_mip_levels = mips;
    fixture_texture.storage_layer_count = faces;
    fixture_texture.component_mapping.r = variation & 2 ?
                                              VK_COMPONENT_SWIZZLE_ZERO :
                                              VK_COMPONENT_SWIZZLE_IDENTITY;
    fixture_texture.sampler_min_filter =
        variation & 1 ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    fixture_texture.captured_sampler.minFilter =
        fixture_texture.sampler_min_filter;
    memset(fixture_r->texture_bindings, 0, sizeof(fixture_r->texture_bindings));
    fixture_r->texture_bindings[variation & 1 ? 1 : 0] = &fixture_texture;
    fixture_r->capture_main_batch.handle = batch;
    image_value = value;
    fixture_texture.storage_extent.width =
        fixture_texture.storage_extent.height = variation & 8 ? 2048 : 2;
    PGRAPHVkShaderInputs *inputs = g_new0(PGRAPHVkShaderInputs, 1);
    inputs->token = token;
    inputs->budget = variation & 4 ? 0 : PGRAPH_VK_INPUT_BUDGET;
    inputs->emitted = true;
    inputs->next = fixture_r->shader_browser_inputs;
    fixture_r->shader_browser_inputs = inputs;
    ++fixture_r->shader_browser_input_events;
    pgraph_vk_input_stage_textures(fixture_pg, token, inputs);
    pgraph_vk_input_reserve_pending(fixture_r, inputs);
}
void xemu_test_texture_reuse_retire(uint64_t stats[5], bool reverse)
{
    image_value =
        0xffff; /* Later image contents must not replace any snapshot. */
    if (reverse) {
        PGRAPHVkShaderInputs *head = NULL;
        while (fixture_r->shader_browser_inputs) {
            PGRAPHVkShaderInputs *inputs = fixture_r->shader_browser_inputs;
            fixture_r->shader_browser_inputs = inputs->next;
            inputs->next = head;
            head = inputs;
        }
        fixture_r->shader_browser_inputs = head;
    }
    pgraph_vk_shader_inputs_retire(fixture_pg);
    stats[0] = copies;
    stats[1] = allocations;
    stats[2] = resolutions;
    stats[3] = destructions;
    stats[4] = fixture_r->shader_browser_input_staging_bytes;
    assert(!fixture_r->shader_browser_inputs &&
           !fixture_r->shader_browser_input_events);
    assert(!fixture_r->shader_browser_input_pending_bytes);
    g_clear_pointer(&fixture_pg, g_free);
    g_clear_pointer(&fixture_r, g_free);
}

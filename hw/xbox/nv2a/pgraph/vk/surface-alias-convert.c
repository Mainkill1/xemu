/*
 * Vulkan read-only Z24S8 surface alias conversion
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "hw/xbox/nv2a/pgraph/pgraph.h"
#include "renderer.h"
#include "surface-alias-map.h"

static void barrier_buffer(VkCommandBuffer cmd, VkBuffer buffer,
                           VkPipelineStageFlags src_stage,
                           VkAccessFlags src_access,
                           VkPipelineStageFlags dst_stage,
                           VkAccessFlags dst_access)
{
    VkBufferMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = src_access,
        .dstAccessMask = dst_access,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = buffer,
        .size = VK_WHOLE_SIZE,
    };
    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, NULL, 1,
                         &barrier, 0, NULL);
}

bool pgraph_vk_convert_depth_alias(PGRAPHState *pg,
                                   SurfaceBinding *producer,
                                   SurfaceBinding *view)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    PGRAPHVkDepthAliasPlan plan;
    VkFormat format = producer->host_fmt.vk_format;

    if (pg->surface_scale_factor != 1 || !producer->image || !view->image ||
        producer->color || view->color || producer->swizzle ||
        !view->swizzle || producer->vram_addr != view->vram_addr ||
        format != view->host_fmt.vk_format ||
        (format != VK_FORMAT_D24_UNORM_S8_UINT &&
         format != VK_FORMAT_D32_SFLOAT_S8_UINT) ||
        r->device_props.limits.maxComputeWorkGroupSize[0] < 1024 ||
        !pgraph_vk_depth_alias_plan(
            producer->width, producer->height, view->width, view->height,
            r->device_props.limits.minStorageBufferOffsetAlignment,
            r->device_props.limits.maxStorageBufferRange, &plan) ||
        plan.producer_pixels % 1024 != 0 ||
        plan.producer_pixels / 1024 >
            r->device_props.limits.maxComputeWorkGroupCount[0] ||
        (plan.view_pixels + 63) / 64 >
            r->device_props.limits.maxComputeWorkGroupCount[0]) {
        return false;
    }

    /* Three distinct descriptor sets remain valid until the command buffer
     * completes. Capacity growth and descriptor reset may submit old work. */
    if (r->compute.descriptor_set_index >
        ARRAY_SIZE(r->compute.descriptor_sets) - 3) {
        pgraph_vk_finish(pg, VK_FINISH_REASON_NEED_BUFFER_SPACE);
    }
    pgraph_vk_ensure_buffer_capacity(pg, BUFFER_COMPUTE_DST,
                                     plan.compute_dst_bytes);
    pgraph_vk_ensure_buffer_capacity(pg, BUFFER_COMPUTE_SRC,
                                     plan.compute_src_bytes);

    VkBuffer split = r->storage_buffers[BUFFER_COMPUTE_DST].buffer;
    VkBuffer packed = r->storage_buffers[BUFFER_COMPUTE_SRC].buffer;
    VkCommandBuffer cmd = pgraph_vk_begin_nondraw_commands(pg);
    const VkPipelineStageFlags all_stages = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    const VkAccessFlags prior_access = VK_ACCESS_MEMORY_READ_BIT |
                                       VK_ACCESS_MEMORY_WRITE_BIT;

    barrier_buffer(cmd, split, all_stages, prior_access,
                   VK_PIPELINE_STAGE_TRANSFER_BIT,
                   VK_ACCESS_TRANSFER_WRITE_BIT);
    barrier_buffer(cmd, packed, all_stages, prior_access,
                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                   VK_ACCESS_SHADER_WRITE_BIT);

    VkBufferImageCopy source_regions[2] = {
        {
            .imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .imageSubresource.layerCount = 1,
            .imageExtent = { producer->width, producer->height, 1 },
        },
        {
            .bufferOffset = plan.producer_stencil_offset,
            .imageSubresource.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT,
            .imageSubresource.layerCount = 1,
            .imageExtent = { producer->width, producer->height, 1 },
        },
    };
    pgraph_vk_transition_image_layout(
        pg, cmd, producer->image, format,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    vkCmdCopyImageToBuffer(cmd, producer->image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, split, 2,
                           source_regions);
    pgraph_vk_transition_image_layout(
        pg, cmd, producer->image, format,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

    barrier_buffer(cmd, split, VK_PIPELINE_STAGE_TRANSFER_BIT,
                   VK_ACCESS_TRANSFER_WRITE_BIT,
                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                   VK_ACCESS_SHADER_READ_BIT);
    pgraph_vk_pack_depth_stencil(pg, producer, cmd, split, packed, false);

    barrier_buffer(cmd, packed, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                   VK_ACCESS_SHADER_WRITE_BIT,
                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                   VK_ACCESS_SHADER_READ_BIT);
    barrier_buffer(cmd, split, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                   VK_ACCESS_SHADER_READ_BIT,
                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                   VK_ACCESS_SHADER_WRITE_BIT);
    bool converted = pgraph_vk_unswizzle_packed_depth(
        pg, cmd, packed, r->storage_buffers[BUFFER_COMPUTE_SRC].buffer_size,
        split, r->storage_buffers[BUFFER_COMPUTE_DST].buffer_size,
        view->width, view->height);
    assert(converted);

    barrier_buffer(cmd, split, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                   VK_ACCESS_SHADER_WRITE_BIT,
                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                   VK_ACCESS_SHADER_READ_BIT);
    barrier_buffer(cmd, packed, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                   VK_ACCESS_SHADER_READ_BIT,
                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                   VK_ACCESS_SHADER_WRITE_BIT);
    pgraph_vk_unpack_depth_stencil(pg, view, cmd, split, packed);

    barrier_buffer(cmd, packed, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                   VK_ACCESS_SHADER_WRITE_BIT,
                   VK_PIPELINE_STAGE_TRANSFER_BIT,
                   VK_ACCESS_TRANSFER_READ_BIT);
    VkBufferImageCopy view_regions[2] = {
        {
            .imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .imageSubresource.layerCount = 1,
            .imageExtent = { view->width, view->height, 1 },
        },
        {
            .bufferOffset = plan.view_stencil_offset,
            .imageSubresource.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT,
            .imageSubresource.layerCount = 1,
            .imageExtent = { view->width, view->height, 1 },
        },
    };
    pgraph_vk_transition_image_layout(
        pg, cmd, view->image, format,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vkCmdCopyBufferToImage(cmd, packed, view->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 2,
                           view_regions);
    pgraph_vk_transition_image_layout(
        pg, cmd, view->image, format,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    pgraph_vk_end_nondraw_commands(pg, cmd);
    return true;
}

/*
 * Vulkan read-only Z24S8 surface alias conversion
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "hw/xbox/nv2a/pgraph/pgraph.h"
#include "renderer.h"
#include "surface-alias-map.h"

static void barrier_buffer(VkCommandBuffer cmd, VkBuffer buffer,
                           VkDeviceSize offset, VkDeviceSize size,
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
        .offset = offset,
        .size = size,
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
    uint32_t producer_workgroup_size;
    uint32_t producer_group_count;
    uint32_t view_workgroup_size;
    uint32_t view_group_count;

    if (pg->surface_scale_factor != 1 || !producer->image || !view->image ||
        producer->color || view->color || producer->swizzle ||
        !view->swizzle || producer->vram_addr != view->vram_addr ||
        format != view->host_fmt.vk_format ||
        (format != VK_FORMAT_D24_UNORM_S8_UINT &&
         format != VK_FORMAT_D32_SFLOAT_S8_UINT) ||
        !pgraph_vk_depth_alias_plan(
            producer->width, producer->height, view->width, view->height,
            r->device_props.limits.minStorageBufferOffsetAlignment,
            r->device_props.limits.maxStorageBufferRange, &plan) ||
        !pgraph_vk_compute_dispatch_plan(
            plan.producer_pixels,
            r->device_props.limits.maxComputeWorkGroupSize[0],
            r->device_props.limits.maxComputeWorkGroupInvocations,
            r->device_props.limits.maxComputeWorkGroupCount[0],
            &producer_workgroup_size, &producer_group_count) ||
        !pgraph_vk_compute_dispatch_plan(
            plan.view_pixels,
            r->device_props.limits.maxComputeWorkGroupSize[0],
            r->device_props.limits.maxComputeWorkGroupInvocations,
            r->device_props.limits.maxComputeWorkGroupCount[0],
            &view_workgroup_size, &view_group_count)) {
        return false;
    }

    assert(producer_workgroup_size && producer_group_count);
    assert(view_workgroup_size && view_group_count);

    /*
     * The pack shader only consumes dimensions and format. Keep the real
     * producer's metadata unchanged while packing the copied row prefix.
     */
    SurfaceBinding source_prefix = {
        .width = producer->width,
        .height = plan.producer_pixels / producer->width,
        .host_fmt = producer->host_fmt,
    };

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
    const VkPipelineStageFlags scratch_stages =
        VK_PIPELINE_STAGE_TRANSFER_BIT |
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    const VkAccessFlags scratch_access =
        VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT |
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    const VkDeviceSize producer_packed_bytes =
        plan.producer_pixels * sizeof(uint32_t);
    const VkDeviceSize view_packed_bytes =
        plan.view_pixels * sizeof(uint32_t);
    const VkDeviceSize view_output_bytes =
        plan.view_stencil_offset + plan.view_pixels;

    barrier_buffer(cmd, split, 0, plan.compute_dst_bytes, scratch_stages,
                   scratch_access,
                   VK_PIPELINE_STAGE_TRANSFER_BIT,
                   VK_ACCESS_TRANSFER_WRITE_BIT);
    barrier_buffer(cmd, packed, 0, producer_packed_bytes, scratch_stages,
                   scratch_access,
                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                   VK_ACCESS_SHADER_WRITE_BIT);

    VkBufferImageCopy source_regions[2] = {
        {
            .imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .imageSubresource.layerCount = 1,
            .imageExtent = { source_prefix.width, source_prefix.height, 1 },
        },
        {
            .bufferOffset = plan.producer_stencil_offset,
            .imageSubresource.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT,
            .imageSubresource.layerCount = 1,
            .imageExtent = { source_prefix.width, source_prefix.height, 1 },
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

    barrier_buffer(cmd, split, 0, plan.compute_dst_bytes,
                   VK_PIPELINE_STAGE_TRANSFER_BIT,
                   VK_ACCESS_TRANSFER_WRITE_BIT,
                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                   VK_ACCESS_SHADER_READ_BIT);
    pgraph_vk_pack_depth_stencil(pg, &source_prefix, cmd, split, packed, false);

    barrier_buffer(cmd, packed, 0, producer_packed_bytes,
                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                   VK_ACCESS_SHADER_WRITE_BIT,
                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                   VK_ACCESS_SHADER_READ_BIT);
    barrier_buffer(cmd, split, 0, view_packed_bytes,
                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                   VK_ACCESS_SHADER_READ_BIT,
                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                   VK_ACCESS_SHADER_WRITE_BIT);
    bool converted = pgraph_vk_unswizzle_packed_depth(
        pg, cmd, packed, r->storage_buffers[BUFFER_COMPUTE_SRC].buffer_size,
        split, r->storage_buffers[BUFFER_COMPUTE_DST].buffer_size,
        view->width, view->height);
    assert(converted);

    barrier_buffer(cmd, split, 0, view_packed_bytes,
                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                   VK_ACCESS_SHADER_WRITE_BIT,
                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                   VK_ACCESS_SHADER_READ_BIT);
    barrier_buffer(cmd, packed, 0, plan.compute_src_bytes,
                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                   VK_ACCESS_SHADER_READ_BIT,
                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                   VK_ACCESS_SHADER_WRITE_BIT);
    pgraph_vk_unpack_depth_stencil(pg, view, cmd, split, packed);

    barrier_buffer(cmd, packed, 0, view_output_bytes,
                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
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
    if (r->perf.enabled) {
        r->perf.depth_alias_copied_pixels += plan.producer_pixels;
    }
    return true;
}

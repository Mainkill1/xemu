/*
 * Geforce NV2A PGRAPH Vulkan Renderer
 *
 * Copyright (c) 2024 Matt Borgerson
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "renderer.h"
#include "hw/xbox/nv2a/pgraph/shader-browser-resource.h"

static PGRAPHVkCaptureBatch *capture_batch(PGRAPHVkState *r, bool auxiliary)
{
    return auxiliary ? &r->capture_auxiliary_batch : &r->capture_main_batch;
}

uint64_t pgraph_vk_capture_batch_handle(PGRAPHState *pg, bool auxiliary)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    PGRAPHVkCaptureBatch *batch = capture_batch(r, auxiliary);
    if (!xemu_shader_capture_session_batch_current(batch->handle)) {
        memset(batch, 0, sizeof(*batch));
        batch->handle = xemu_shader_capture_session_batch_begin(
            xemu_shader_browser_scope_generation(),
            nv2a_profile_preview_renderer_epoch());
    }
    return batch->handle;
}

static bool capture_is_auxiliary(PGRAPHVkState *r, VkCommandBuffer cmd)
{
    return cmd != VK_NULL_HANDLE && cmd == r->aux_command_buffer &&
           !r->capture_finish_auxiliary;
}

bool pgraph_vk_capture_hold(PGRAPHState *pg, VkCommandBuffer cmd,
                            uint64_t token)
{
    if (!xemu_shader_capture_session_token(token))
        return true;
    uint64_t batch = pgraph_vk_capture_batch_handle(
        pg, capture_is_auxiliary(pg->vk_renderer_state, cmd));
    return batch && xemu_shader_capture_session_batch_hold(batch, token);
}

void pgraph_vk_capture_record(PGRAPHState *pg, VkCommandBuffer cmd,
                              uint64_t token, uint32_t kind)
{
    if (!xemu_shader_capture_session_token(token))
        return;
    PGRAPHVkState *r = pg->vk_renderer_state;
    PGRAPHVkCaptureBatch *batch =
        capture_batch(r, capture_is_auxiliary(r, cmd));
    uint32_t phase = cmd == r->command_buffer ? XEMU_SHADER_CAPTURE_MAIN :
                                                XEMU_SHADER_CAPTURE_AUXILIARY;
    if (xemu_shader_capture_session_batch_current(batch->handle)) {
        if (kind != XEMU_SHADER_CAPTURE_COMMAND_UNKNOWN)
            xemu_shader_capture_session_describe_command(token, kind, 0, 0, 0);
        xemu_shader_capture_session_batch_record(batch->handle, token, phase,
                                                 ++batch->ordinal[phase]);
    }
}

void pgraph_vk_capture_abort(PGRAPHState *pg, VkCommandBuffer cmd,
                             VkResult result)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    PGRAPHVkCaptureBatch *batch =
        capture_batch(r, capture_is_auxiliary(r, cmd));
    xemu_shader_capture_session_batch_abort(
        batch->handle, XEMU_SHADER_CAPTURE_BATCH_ABORTED, result);
}

uint64_t pgraph_vk_capture_submit(PGRAPHState *pg, VkCommandBuffer cmd,
                                  VkResult result)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    uint64_t batch = capture_batch(r, capture_is_auxiliary(r, cmd))->handle;
    xemu_shader_capture_session_batch_submit(
        batch, result == VK_SUCCESS, ++r->capture_queue_ordinal, result);
    return batch;
}

void pgraph_vk_capture_retire(uint64_t batch, VkResult result)
{
    xemu_shader_capture_session_batch_retire(batch, result == VK_SUCCESS,
                                             result);
}

static uint64_t capture_resource_begin(PGRAPHState *pg, VkCommandBuffer cmd,
                                       uint32_t type)
{
    uint64_t token = pgraph_shader_resource_begin(pg, type);
    if (token && !pgraph_vk_capture_hold(pg, cmd, token)) {
        xemu_shader_capture_session_fail(token,
                                         "Vulkan batch admission failed");
        pgraph_shader_resource_finish(token);
        return 0;
    }
    return token;
}

static void reserve_deferred_copy(PGRAPHState *pg, int source)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    uint64_t batch = pgraph_vk_capture_batch_handle(pg, false);
    if (!batch || r->capture_deferred_copy_batch[source] == batch)
        return;
    r->capture_deferred_copy_batch[source] = batch;
    r->capture_deferred_copy_token[source] =
        capture_resource_begin(pg, r->command_buffer, XEMU_SHADER_CAPTURE_COPY);
}

void pgraph_vk_capture_buffer_upload(PGRAPHState *pg, int index,
                                     VkDeviceSize offset, VkDeviceSize size,
                                     const void *data, bool unknown_source)
{
    if (!size || !xemu_shader_capture_session_active())
        return;
    PGRAPHVkState *r = pg->vk_renderer_state;
    bool auxiliary = index == BUFFER_STAGING_SRC;
    uint64_t token =
        pgraph_shader_resource_begin(pg, XEMU_SHADER_CAPTURE_UPLOAD);
    if (!token)
        return;
    uint64_t batch_handle = pgraph_vk_capture_batch_handle(pg, auxiliary);
    if (!batch_handle ||
        !xemu_shader_capture_session_batch_hold(batch_handle, token)) {
        xemu_shader_capture_session_fail(token,
                                         "Vulkan batch admission failed");
        pgraph_shader_resource_finish(token);
        return;
    }
    StorageBuffer *buffer = &r->storage_buffers[index];
    pgraph_shader_resource_stage_buffer_upload(token, buffer->capture_owner,
                                               buffer->buffer_size, offset,
                                               size, data, unknown_source);
    xemu_shader_capture_session_describe_command(
        token, XEMU_SHADER_CAPTURE_COMMAND_CPU_UPLOAD, 0, offset, size);
    PGRAPHVkCaptureBatch *batch = capture_batch(r, auxiliary);
    xemu_shader_capture_session_batch_record(
        batch_handle, token, XEMU_SHADER_CAPTURE_HOST_PREPARATION,
        ++batch->ordinal[XEMU_SHADER_CAPTURE_HOST_PREPARATION]);
    pgraph_shader_resource_finish(token);
    if (index == BUFFER_INDEX_STAGING ||
        index == BUFFER_VERTEX_INLINE_STAGING ||
        index == BUFFER_UNIFORM_STAGING)
        reserve_deferred_copy(pg, index);
}

uint64_t pgraph_vk_capture_buffer_copy(PGRAPHState *pg, VkCommandBuffer cmd,
                                       int source, int destination,
                                       VkDeviceSize source_offset,
                                       VkDeviceSize destination_offset,
                                       VkDeviceSize bytes, bool deferred)
{
    if (!bytes)
        return 0;
    PGRAPHVkState *r = pg->vk_renderer_state;
    uint64_t batch =
        pgraph_vk_capture_batch_handle(pg, capture_is_auxiliary(r, cmd));
    uint64_t token = 0;
    if (deferred && r->capture_deferred_copy_batch[source] == batch)
        token = r->capture_deferred_copy_token[source];
    if (!token)
        token = capture_resource_begin(pg, cmd, XEMU_SHADER_CAPTURE_COPY);
    if (!token)
        return 0;
    StorageBuffer *src = &r->storage_buffers[source];
    StorageBuffer *dst = &r->storage_buffers[destination];
    const char *snapshot = NULL;
    bool host_staging = source == BUFFER_INDEX_STAGING ||
                        source == BUFFER_VERTEX_INLINE_STAGING ||
                        source == BUFFER_UNIFORM_STAGING ||
                        source == BUFFER_VERTEX_RAM_STAGING ||
                        source == BUFFER_TEXTURE_STAGING ||
                        source == BUFFER_STAGING_SRC;
    if (host_staging && src->mapped && source_offset <= src->buffer_size &&
        bytes <= src->buffer_size - source_offset) {
        XemuShaderDrawBlob blob = {
            .name = "resource.buffer.copy",
            .data = src->mapped + source_offset,
            .byte_count = bytes,
            .offset = source_offset,
        };
        if (xemu_shader_draw_request_stage_blob(token, &blob))
            snapshot = blob.name;
    }
    XemuShaderCaptureResource read = {
        .owner = src->capture_owner,
        .byte_size = src->buffer_size,
        .offset = source_offset,
        .size = bytes,
        .access = XEMU_SHADER_CAPTURE_RESOURCE_READ,
        .kind = XEMU_SHADER_CAPTURE_RESOURCE_BUFFER,
    };
    XemuShaderCaptureResource write = {
        .owner = dst->capture_owner,
        .byte_size = dst->buffer_size,
        .offset = destination_offset,
        .size = bytes,
        .access = XEMU_SHADER_CAPTURE_RESOURCE_PARTIAL_WRITE,
        .kind = XEMU_SHADER_CAPTURE_RESOURCE_BUFFER,
    };
    if (snapshot) {
        xemu_shader_capture_session_resource_snapshot(token, &read, snapshot);
        xemu_shader_capture_session_resource_snapshot(token, &write, snapshot);
    } else {
        xemu_shader_capture_session_resource(token, &read);
        xemu_shader_capture_session_resource(token, &write);
    }
    xemu_shader_capture_session_describe_command(
        token, XEMU_SHADER_CAPTURE_COMMAND_BUFFER_COPY, source_offset,
        destination_offset, bytes);
    return token;
}

void pgraph_vk_capture_resource_write(
    PGRAPHState *pg, VkCommandBuffer cmd, uint32_t event_kind, uint64_t owner,
    uint64_t bytes, uint32_t resource_kind, bool opaque, uint64_t source_owner,
    uint64_t source_bytes, uint32_t source_kind, bool source_opaque,
    uint64_t source_offset, uint64_t source_size)
{
    uint64_t token = capture_resource_begin(pg, cmd, event_kind);
    if (!token)
        return;
    if (source_size) {
        XemuShaderCaptureResource source = {
            .owner = source_owner,
            .byte_size = source_bytes,
            .offset = source_offset,
            .size = source_size,
            .access = XEMU_SHADER_CAPTURE_RESOURCE_READ,
            .kind = source_kind,
            .flags =
                source_opaque ? XEMU_SHADER_CAPTURE_RESOURCE_OPAQUE_EXTENT : 0,
        };
        xemu_shader_capture_session_resource(token, &source);
    }
    pgraph_shader_resource_stage_write(token, owner, bytes, resource_kind,
                                       opaque, 0, 0, 0, false);
    pgraph_vk_capture_record(pg, cmd, token,
                             event_kind == XEMU_SHADER_CAPTURE_RESOLVE ?
                                 XEMU_SHADER_CAPTURE_COMMAND_RESOLVE :
                                 XEMU_SHADER_CAPTURE_COMMAND_IMAGE_COPY);
    pgraph_shader_resource_finish(token);
}

static bool capture_replay_image(VkFormat format, VkExtent3D extent,
                                 uint32_t mips, uint32_t layers,
                                 VkComponentMapping components,
                                 XemuShaderCaptureReplayImage *image)
{
    *image = (XemuShaderCaptureReplayImage){
        .width = extent.width,
        .height = extent.height,
        .mip_levels = mips,
        .layers = layers,
        .samples = 1,
        .coordinate_origin = XEMU_SHADER_CAPTURE_REPLAY_TOP_DOWN,
    };
    if (!extent.width || !extent.height || extent.depth != 1 || mips != 1 ||
        layers != 1)
        return false;
    if (format == VK_FORMAT_R8G8B8A8_UNORM)
        image->format = XEMU_SHADER_CAPTURE_REPLAY_RGBA8_UNORM;
    else if (format == VK_FORMAT_B8G8R8A8_UNORM)
        image->format = XEMU_SHADER_CAPTURE_REPLAY_BGRA8_UNORM;
    else
        return false;
    const VkComponentSwizzle swizzles[] = { components.r, components.g,
                                            components.b, components.a };
    for (uint32_t i = 0; i < 4; ++i) {
        image->storage_to_rgba[i] =
            format == VK_FORMAT_B8G8R8A8_UNORM && i < 3 ? 2 - i : i;
        switch (swizzles[i]) {
        case VK_COMPONENT_SWIZZLE_IDENTITY:
            image->sample_swizzle[i] = i;
            break;
        case VK_COMPONENT_SWIZZLE_ZERO:
            image->sample_swizzle[i] = 4;
            break;
        case VK_COMPONENT_SWIZZLE_ONE:
            image->sample_swizzle[i] = 5;
            break;
        case VK_COMPONENT_SWIZZLE_R:
            image->sample_swizzle[i] = 0;
            break;
        case VK_COMPONENT_SWIZZLE_G:
            image->sample_swizzle[i] = 1;
            break;
        case VK_COMPONENT_SWIZZLE_B:
            image->sample_swizzle[i] = 2;
            break;
        case VK_COMPONENT_SWIZZLE_A:
            image->sample_swizzle[i] = 3;
            break;
        default:
            return false;
        }
    }
    return true;
}

static bool capture_replay_surface(PGRAPHState *pg,
                                   const SurfaceBinding *surface,
                                   XemuShaderCaptureReplayImage *image)
{
    if (!surface || !surface->color || !surface->image ||
        !surface->capture_owner || !surface->capture_bytes ||
        surface->host_fmt.aspect != VK_IMAGE_ASPECT_COLOR_BIT ||
        !pg->surface_scale_factor)
        return false;
    uint64_t width = (uint64_t)surface->width * pg->surface_scale_factor;
    uint64_t height = (uint64_t)surface->height * pg->surface_scale_factor;
    if (width > UINT32_MAX || height > UINT32_MAX)
        return false;
    // Surface/texture creation records one physical sample. Guest antialiasing
    // and swizzle state remain separate raw evidence; these are host extents.
    return capture_replay_image(surface->host_fmt.vk_format,
                                (VkExtent3D){ width, height, 1 }, 1, 1,
                                (VkComponentMapping){ 0 }, image);
}

bool pgraph_vk_capture_draw_description(
    PGRAPHState *pg, bool indexed,
    XemuShaderCaptureReplayDescription *description)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    *description = (XemuShaderCaptureReplayDescription){
        .kind = XEMU_SHADER_CAPTURE_COMMAND_DRAW
    };
    XemuShaderCaptureReplayImage color;
    if (!r->color_binding || !r->color_binding->image_view ||
        !capture_replay_surface(pg, r->color_binding, &color))
        return false;
    for (uint32_t write = 0; write < 2; ++write) {
        description->bindings[description->binding_count++] =
            (XemuShaderCaptureReplayBinding){
                .resource = { XEMU_SHADER_CAPTURE_RESOURCE_COLOR, 0, write, 0 },
                .role = XEMU_SHADER_CAPTURE_REPLAY_COLOR,
                .checkpoint = 1,
                .image = color,
            };
    }
    for (uint32_t slot = 0; slot < NV2A_MAX_TEXTURES; ++slot) {
        const TextureBinding *texture = r->texture_bindings[slot];
        if (!texture || texture == &r->dummy_texture)
            continue;
        XemuShaderCaptureReplayImage image;
        if (!texture->image || !texture->image_view ||
            !texture->capture_owner || !texture->capture_bytes ||
            texture->storage_image_type != VK_IMAGE_TYPE_2D ||
            !capture_replay_image(
                texture->key.vk_format, texture->storage_extent,
                texture->storage_mip_levels, texture->storage_layer_count,
                texture->component_mapping, &image))
            return false;
        bool checkpoint = true;
        for (uint32_t component = 0; component < 4; ++component)
            checkpoint &= image.sample_swizzle[component] == component;
        description->bindings[description->binding_count++] =
            (XemuShaderCaptureReplayBinding){
                .resource = { XEMU_SHADER_CAPTURE_RESOURCE_TEXTURE, slot, 0,
                              0 },
                .role = XEMU_SHADER_CAPTURE_REPLAY_TEXTURE,
                .slot = slot,
                .checkpoint = checkpoint,
                .image = image,
            };
    }
    for (uint32_t slot = 0; slot < NV2A_VERTEXSHADER_ATTRIBUTES; ++slot) {
        int location = r->vertex_attribute_to_description_location[slot];
        if (location < 0)
            continue;
        if (location >= r->num_active_vertex_attribute_descriptions)
            return false;
        XemuShaderCaptureReplayBinding *binding =
            &description->bindings[description->binding_count++];
        *binding = (XemuShaderCaptureReplayBinding){
            .resource = { XEMU_SHADER_CAPTURE_RESOURCE_BUFFER, slot, 0, 0 },
            .role = XEMU_SHADER_CAPTURE_REPLAY_VERTEX_STREAM,
            .slot = slot,
            .checkpoint = 1,
        };
        snprintf(binding->blob_name, sizeof(binding->blob_name),
                 "vertex.attribute%u", slot);
    }
    if (indexed) {
        XemuShaderCaptureReplayBinding *binding =
            &description->bindings[description->binding_count++];
        *binding = (XemuShaderCaptureReplayBinding){
            .resource = { XEMU_SHADER_CAPTURE_RESOURCE_BUFFER,
                          NV2A_VERTEXSHADER_ATTRIBUTES, 0, 0 },
            .role = XEMU_SHADER_CAPTURE_REPLAY_INDICES,
            .slot = NV2A_VERTEXSHADER_ATTRIBUTES,
            .checkpoint = 1,
        };
        snprintf(binding->blob_name, sizeof(binding->blob_name),
                 "vertex.indices");
    }
    return true;
}

void pgraph_vk_capture_draw_replay(PGRAPHState *pg, uint64_t token,
                                   uint32_t index_count)
{
    if (!xemu_shader_capture_session_token(token))
        return;
    XemuShaderCaptureReplayDescription description;
    bool described =
        pgraph_vk_capture_draw_description(pg, index_count != 0,
                                           &description) &&
        xemu_shader_capture_session_describe_replay(token, &description);
    xemu_shader_draw_request_stage_register(
        token, "capture.logical.description.status", described ? 0 : 1);
    PGRAPHVkState *r = pg->vk_renderer_state;
    for (uint32_t slot = 0; slot < NV2A_MAX_TEXTURES; ++slot) {
        const TextureBinding *texture = r->texture_bindings[slot];
        if (!texture || texture == &r->dummy_texture)
            continue;
        const uint32_t map[] = { texture->component_mapping.r,
                                 texture->component_mapping.g,
                                 texture->component_mapping.b,
                                 texture->component_mapping.a };
        for (uint32_t component = 0; component < 4; ++component) {
            char name[64];
            snprintf(name, sizeof(name), "capture.texture.%u.view.%u", slot,
                     component);
            xemu_shader_draw_request_stage_register(token, name,
                                                    map[component]);
        }
    }
}

uint64_t pgraph_vk_capture_color_image_copy(PGRAPHState *pg,
                                            VkCommandBuffer cmd,
                                            const SurfaceBinding *source,
                                            const TextureBinding *destination,
                                            const VkImageCopy *region)
{
    uint64_t token = capture_resource_begin(pg, cmd, XEMU_SHADER_CAPTURE_COPY);
    if (!token)
        return 0;
    pgraph_shader_resource_stage_write(
        token, destination->capture_owner, destination->capture_bytes,
        XEMU_SHADER_CAPTURE_RESOURCE_TEXTURE, false, source->capture_owner,
        source->capture_bytes, XEMU_SHADER_CAPTURE_RESOURCE_COLOR, false);
    pgraph_vk_capture_record(pg, cmd, token,
                             XEMU_SHADER_CAPTURE_COMMAND_IMAGE_COPY);
    XemuShaderDrawBlob raw = { .name = "capture.vk.image.copy",
                               .data = region,
                               .byte_count = sizeof(*region),
                               .format = 1 };
    xemu_shader_draw_request_stage_blob(token, &raw);
    xemu_shader_draw_request_stage_register(
        token, "capture.copy.source.host_format", source->host_fmt.vk_format);
    xemu_shader_draw_request_stage_register(
        token, "capture.copy.destination.host_format",
        destination->key.vk_format);
    XemuShaderCaptureReplayDescription description = {
        .kind = XEMU_SHADER_CAPTURE_COMMAND_IMAGE_COPY
    };
    XemuShaderCaptureReplayImage src, dst;
    bool supported =
        capture_replay_surface(pg, source, &src) && destination->image &&
        destination->capture_owner && destination->capture_bytes &&
        destination->storage_image_type == VK_IMAGE_TYPE_2D &&
        capture_replay_image(
            destination->key.vk_format, destination->storage_extent,
            destination->storage_mip_levels, destination->storage_layer_count,
            (VkComponentMapping){ 0 }, &dst) &&
        src.format == dst.format &&
        region->srcSubresource.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT &&
        region->dstSubresource.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT &&
        !region->srcSubresource.mipLevel && !region->dstSubresource.mipLevel &&
        !region->srcSubresource.baseArrayLayer &&
        !region->dstSubresource.baseArrayLayer &&
        region->srcSubresource.layerCount == 1 &&
        region->dstSubresource.layerCount == 1 && region->srcOffset.x >= 0 &&
        region->srcOffset.y >= 0 && !region->srcOffset.z &&
        region->dstOffset.x >= 0 && region->dstOffset.y >= 0 &&
        !region->dstOffset.z && region->extent.width && region->extent.height &&
        region->extent.depth == 1 &&
        (uint64_t)region->srcOffset.x + region->extent.width <= src.width &&
        (uint64_t)region->srcOffset.y + region->extent.height <= src.height &&
        (uint64_t)region->dstOffset.x + region->extent.width <= dst.width &&
        (uint64_t)region->dstOffset.y + region->extent.height <= dst.height;
    if (supported) {
        description.source_x = region->srcOffset.x;
        description.source_y = region->srcOffset.y;
        description.destination_x = region->dstOffset.x;
        description.destination_y = region->dstOffset.y;
        description.width = region->extent.width;
        description.height = region->extent.height;
        description.bindings[description.binding_count++] =
            (XemuShaderCaptureReplayBinding){
                .resource = { XEMU_SHADER_CAPTURE_RESOURCE_COLOR, 0, 0, 0 },
                .role = XEMU_SHADER_CAPTURE_REPLAY_COPY_SOURCE,
                .image = src,
            };
        description.bindings[description.binding_count++] =
            (XemuShaderCaptureReplayBinding){
                .resource = { XEMU_SHADER_CAPTURE_RESOURCE_TEXTURE, 0, 1, 0 },
                .role = XEMU_SHADER_CAPTURE_REPLAY_COPY_DESTINATION,
                .image = dst,
            };
        description.bindings[description.binding_count++] =
            (XemuShaderCaptureReplayBinding){
                .resource = { XEMU_SHADER_CAPTURE_RESOURCE_TEXTURE, 0, 0, 0 },
                .role = XEMU_SHADER_CAPTURE_REPLAY_COPY_DESTINATION,
                .image = dst,
            };
        supported =
            xemu_shader_capture_session_describe_replay(token, &description);
    }
    xemu_shader_draw_request_stage_register(
        token, "capture.logical.description.status", supported ? 0 : 1);
    pgraph_shader_resource_finish(token);
    return token;
}

bool pgraph_vk_capture_clear_description(
    PGRAPHState *pg, uint32_t parameter, uint32_t count,
    const VkClearAttachment *attachments, const VkClearRect *rect,
    bool masked_draw, XemuShaderCaptureReplayDescription *description)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    *description = (XemuShaderCaptureReplayDescription){
        .kind = XEMU_SHADER_CAPTURE_COMMAND_CLEAR
    };
    XemuShaderCaptureReplayImage image;
    if (masked_draw || count != 1 ||
        (parameter & NV097_CLEAR_SURFACE_COLOR) != NV097_CLEAR_SURFACE_COLOR ||
        (parameter & (NV097_CLEAR_SURFACE_Z | NV097_CLEAR_SURFACE_STENCIL)) ||
        pg->surface_shape.anti_aliasing !=
            NV097_SET_SURFACE_FORMAT_ANTI_ALIASING_CENTER_1 ||
        attachments[0].aspectMask != VK_IMAGE_ASPECT_COLOR_BIT ||
        attachments[0].colorAttachment != 0 || rect->baseArrayLayer != 0 ||
        rect->layerCount != 1 || rect->rect.offset.x < 0 ||
        rect->rect.offset.y < 0 || !rect->rect.extent.width ||
        !rect->rect.extent.height ||
        !capture_replay_surface(pg, r->color_binding, &image) ||
        (uint64_t)rect->rect.offset.x + rect->rect.extent.width > image.width ||
        (uint64_t)rect->rect.offset.y + rect->rect.extent.height > image.height)
        return false;
    description->destination_x = rect->rect.offset.x;
    description->destination_y = rect->rect.offset.y;
    description->width = rect->rect.extent.width;
    description->height = rect->rect.extent.height;
    memcpy(description->clear_color_bits,
           attachments[0].clearValue.color.float32,
           sizeof(description->clear_color_bits));
    for (uint32_t component = 0; component < 4; ++component)
        if ((description->clear_color_bits[component] & 0x7f800000U) ==
            0x7f800000U)
            return false;
    description->clear_color_mask = 15;
    for (uint32_t write = 0; write < 2; ++write)
        description->bindings[description->binding_count++] =
            (XemuShaderCaptureReplayBinding){
                .resource = { XEMU_SHADER_CAPTURE_RESOURCE_COLOR, 0, write, 0 },
                .role = XEMU_SHADER_CAPTURE_REPLAY_COLOR,
                .image = image,
            };
    return true;
}

uint64_t pgraph_vk_capture_clear(PGRAPHState *pg, VkCommandBuffer cmd,
                                 uint32_t parameter, uint32_t count,
                                 const VkClearAttachment *attachments,
                                 const VkClearRect *rect, bool masked_draw,
                                 const float *masked_color)
{
    if (!count && !masked_draw)
        return 0;
    uint64_t token = capture_resource_begin(pg, cmd, XEMU_SHADER_CAPTURE_CLEAR);
    if (!token)
        return 0;
    PGRAPHVkState *r = pg->vk_renderer_state;
    bool write_color = parameter & NV097_CLEAR_SURFACE_COLOR;
    bool write_zeta =
        parameter & (NV097_CLEAR_SURFACE_Z | NV097_CLEAR_SURFACE_STENCIL);
    if (write_color && r->color_binding) {
        pgraph_shader_resource_stage_write(
            token, r->color_binding->capture_owner,
            r->color_binding->capture_bytes, XEMU_SHADER_CAPTURE_RESOURCE_COLOR,
            false, 0, 0, 0, false);
    }
    if (write_zeta && r->zeta_binding) {
        pgraph_shader_resource_stage_write(
            token, r->zeta_binding->capture_owner,
            r->zeta_binding->capture_bytes,
            XEMU_SHADER_CAPTURE_RESOURCE_DEPTH_STENCIL, false, 0, 0, 0, false);
    }
    pgraph_vk_capture_record(pg, cmd, token, XEMU_SHADER_CAPTURE_COMMAND_CLEAR);
    const struct {
        const char *name;
        const void *data;
        size_t bytes;
    } blobs[] = {
        { "capture.vk.clear.attachments", attachments,
          count * sizeof(*attachments) },
        { "capture.vk.clear.rect", rect, sizeof(*rect) },
        { "capture.vk.clear.masked.color", masked_color,
          masked_draw && masked_color ? 4 * sizeof(*masked_color) : 0 },
    };
    for (size_t i = 0; i < ARRAY_SIZE(blobs); ++i) {
        if (!blobs[i].bytes)
            continue;
        XemuShaderDrawBlob raw = { .name = blobs[i].name,
                                   .data = blobs[i].data,
                                   .byte_count = blobs[i].bytes,
                                   .format = 1 };
        xemu_shader_draw_request_stage_blob(token, &raw);
    }
    const struct {
        const char *name;
        uint64_t value;
    } values[] = {
        { "capture.clear.parameter", parameter },
        { "capture.clear.masked.draw", masked_draw },
        { "capture.clear.attachment.count", count },
        { "capture.clear.command.count", (count ? 1U : 0U) + masked_draw },
        { "capture.clear.guest.color",
          pgraph_reg_r(pg, NV_PGRAPH_COLORCLEARVALUE) },
        { "capture.clear.guest.zeta",
          pgraph_reg_r(pg, NV_PGRAPH_ZSTENCILCLEARVALUE) },
        { "capture.clear.guest.rect.x",
          pgraph_reg_r(pg, NV_PGRAPH_CLEARRECTX) },
        { "capture.clear.guest.rect.y",
          pgraph_reg_r(pg, NV_PGRAPH_CLEARRECTY) },
        { "capture.clear.guest.antialiasing", pg->surface_shape.anti_aliasing },
        { "capture.clear.host.samples", VK_SAMPLE_COUNT_1_BIT },
        { "capture.clear.guest.color.format", pg->surface_shape.color_format },
        { "capture.clear.guest.zeta.format", pg->surface_shape.zeta_format },
        { "capture.clear.color.host.format",
          r->color_binding ? r->color_binding->host_fmt.vk_format : 0 },
        { "capture.clear.color.host.width",
          r->color_binding ?
              (uint64_t)r->color_binding->width * pg->surface_scale_factor :
              0 },
        { "capture.clear.color.host.height",
          r->color_binding ?
              (uint64_t)r->color_binding->height * pg->surface_scale_factor :
              0 },
        { "capture.clear.zeta.host.format",
          r->zeta_binding ? r->zeta_binding->host_fmt.vk_format : 0 },
    };
    for (size_t i = 0; i < ARRAY_SIZE(values); ++i)
        xemu_shader_draw_request_stage_register(token, values[i].name,
                                                values[i].value);
    XemuShaderCaptureReplayDescription description;
    bool described =
        pgraph_vk_capture_clear_description(pg, parameter, count, attachments,
                                            rect, masked_draw, &description) &&
        xemu_shader_capture_session_describe_replay(token, &description);
    xemu_shader_draw_request_stage_register(
        token, "capture.logical.description.status", described ? 0 : 1);
    xemu_shader_draw_request_finish(token, true, 0, 0, 0);
    xemu_shader_draw_request_inputs_complete(token);
    return token;
}

static void create_command_pool(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    QueueFamilyIndices indices =
        pgraph_vk_find_queue_families(r->physical_device);

    VkCommandPoolCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = indices.queue_family,
    };
    VK_CHECK(
        vkCreateCommandPool(r->device, &create_info, NULL, &r->command_pool));
}

static void destroy_command_pool(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    vkDestroyCommandPool(r->device, r->command_pool, NULL);
}

static void create_command_buffers(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = r->command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = ARRAY_SIZE(r->command_buffers),
    };
    VK_CHECK(
        vkAllocateCommandBuffers(r->device, &alloc_info, r->command_buffers));

    r->command_buffer = r->command_buffers[0];
    r->aux_command_buffer = r->command_buffers[1];
}

static void destroy_command_buffers(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    xemu_shader_capture_session_batch_abort(
        r->capture_main_batch.handle, XEMU_SHADER_CAPTURE_BATCH_ABORTED, 0);
    xemu_shader_capture_session_batch_abort(
        r->capture_auxiliary_batch.handle, XEMU_SHADER_CAPTURE_BATCH_ABORTED, 0);

    vkFreeCommandBuffers(r->device, r->command_pool,
                         ARRAY_SIZE(r->command_buffers), r->command_buffers);

    r->command_buffer = VK_NULL_HANDLE;
    r->aux_command_buffer = VK_NULL_HANDLE;
}

VkCommandBuffer pgraph_vk_begin_single_time_commands(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    assert(!r->in_aux_command_buffer);
    r->in_aux_command_buffer = true;

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    VkResult result = vkBeginCommandBuffer(r->aux_command_buffer, &begin_info);
    if (result != VK_SUCCESS)
        pgraph_vk_capture_abort(pg, r->aux_command_buffer, result);
    VK_CHECK(result);

    return r->aux_command_buffer;
}

void pgraph_vk_end_single_time_commands(PGRAPHState *pg, VkCommandBuffer cmd,
                                        SingleTimeReason reason,
                                        uint64_t staged_bytes)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    assert(r->in_aux_command_buffer);

    VkResult result = vkEndCommandBuffer(cmd);
    if (result != VK_SUCCESS)
        pgraph_vk_capture_abort(pg, cmd, result);
    VK_CHECK(result);

    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
    };
    int64_t submit_start = r->perf.enabled ?
        qemu_clock_get_us(QEMU_CLOCK_REALTIME) : 0;
    result = vkQueueSubmit(r->queue, 1, &submit_info, VK_NULL_HANDLE);
    uint64_t capture_batch_handle = pgraph_vk_capture_submit(pg, cmd, result);
    uint64_t submit_cpu_us = r->perf.enabled ?
        MAX(qemu_clock_get_us(QEMU_CLOCK_REALTIME) - submit_start, 0) : 0;
    VK_CHECK(result);
    nv2a_profile_log_event_once(NV2A_PROFILE_EVENT_GPU_SUBMIT);
    nv2a_profile_inc_counter(NV2A_PROF_QUEUE_SUBMIT_AUX);
    int64_t wait_start = r->perf.enabled ?
        qemu_clock_get_us(QEMU_CLOCK_REALTIME) : 0;
    result = vkQueueWaitIdle(r->queue);
    pgraph_vk_capture_retire(capture_batch_handle, result);
    uint64_t wait_us = r->perf.enabled ?
        MAX(qemu_clock_get_us(QEMU_CLOCK_REALTIME) - wait_start, 0) : 0;
    VK_CHECK(result);
    pgraph_vk_perf_record_single_time_submit(
        r, reason, submit_cpu_us, wait_us, staged_bytes);

    r->in_aux_command_buffer = false;
    memset(&r->capture_auxiliary_batch, 0, sizeof(r->capture_auxiliary_batch));
}

void pgraph_vk_init_command_buffers(PGRAPHState *pg)
{
    create_command_pool(pg);
    create_command_buffers(pg);
}

void pgraph_vk_finalize_command_buffers(PGRAPHState *pg)
{
    destroy_command_buffers(pg);
    destroy_command_pool(pg);
}

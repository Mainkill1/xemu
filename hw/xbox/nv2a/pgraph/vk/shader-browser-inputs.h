/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_XBOX_NV2A_PGRAPH_VK_SHADER_BROWSER_INPUTS_H
#define HW_XBOX_NV2A_PGRAPH_VK_SHADER_BROWSER_INPUTS_H

#include "ui/xui/shader-browser-draw-inputs.h"
#include "hw/xbox/nv2a/pgraph/s3tc.h"
#include "shader-browser-input-pressure.h"

/* Kept in draw.c after the render-pass helpers. Every buffer belongs to the
 * game submission and is released only after that submission's fence. */
enum {
    PGRAPH_VK_INPUT_MISSING = 1,
    PGRAPH_VK_INPUT_UNSUPPORTED = 2,
    PGRAPH_VK_INPUT_LIMIT = 4,
    PGRAPH_VK_INPUT_ALLOCATION = 8,
    PGRAPH_VK_INPUT_REJECTED = 16,
};
#define PGRAPH_VK_INPUT_BUDGET (64 * 1024 * 1024)
#define PGRAPH_VK_INPUT_SOURCE_BUDGET (4 * 1024 * 1024)
#define PGRAPH_VK_INPUT_MAX_DIMENSION 2048
#define PGRAPH_VK_INPUT_MAX_EVENTS 8192
#define PGRAPH_VK_INPUT_BLOB_BUDGET (16 * 1024 * 1024)

typedef struct PGRAPHVkInputReadback {
    struct PGRAPHVkInputReadback *next;
    VkBuffer buffer;
    VmaAllocation allocation;
    void *mapped;
    size_t raw_bytes, rgba_bytes, storage_bytes;
    uint32_t width, height;
    VkFormat format;
    VkComponentMapping components;
    XemuShaderDrawTexture texture;
    XemuShaderDrawBlob blob;
    char blob_name[64];
    size_t blob_data_offset;
    int before; /* -1 denotes a texture; -2 denotes a raw vertex stream. */
    bool recorded;
} PGRAPHVkInputReadback;

typedef struct PGRAPHVkShaderInputs {
    struct PGRAPHVkShaderInputs *next;
    uint64_t token;
    size_t budget;
    PGRAPHVkInputReadback *readbacks;
    PGRAPHVkInputReadback *after;
    VkImage color_image;
    VkFormat color_format;
    bool emitted;
    bool failed_budget;
    uint32_t vertex_status;
    uint32_t texture_status[NV2A_MAX_TEXTURES];
} PGRAPHVkShaderInputs;

static size_t pgraph_vk_input_raw_bytes(VkFormat format, uint32_t width,
                                        uint32_t height)
{
    if (!width || !height || width > PGRAPH_VK_INPUT_MAX_DIMENSION ||
        height > PGRAPH_VK_INPUT_MAX_DIMENSION) {
        return 0;
    }
    unsigned int texel_bytes;
    switch (format) {
    case VK_FORMAT_R8_UNORM:
        texel_bytes = 1;
        break;
    case VK_FORMAT_R8G8_UNORM:
    case VK_FORMAT_R16_UNORM:
    case VK_FORMAT_A1R5G5B5_UNORM_PACK16:
    case VK_FORMAT_R5G6B5_UNORM_PACK16:
    case VK_FORMAT_A4R4G4B4_UNORM_PACK16:
        texel_bytes = 2;
        break;
    case VK_FORMAT_R8G8B8_SNORM:
        texel_bytes = 3;
        break;
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_UNORM:
        texel_bytes = 4;
        break;
    case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        return (size_t)((width + 3) / 4) * ((height + 3) / 4) * 8;
    case VK_FORMAT_BC2_UNORM_BLOCK:
    case VK_FORMAT_BC3_UNORM_BLOCK:
        return (size_t)((width + 3) / 4) * ((height + 3) / 4) * 16;
    default:
        return 0;
    }
    return (size_t)width * height * texel_bytes;
}

static void pgraph_vk_input_status(uint64_t token, const char *name,
                                   uint32_t status)
{
    xemu_shader_draw_request_stage_register(token, name, status);
    if (g_str_has_suffix(name, ".status") &&
        (status & (PGRAPH_VK_INPUT_LIMIT | PGRAPH_VK_INPUT_ALLOCATION |
                   PGRAPH_VK_INPUT_REJECTED)) &&
        xemu_shader_capture_session_token(token)) {
        xemu_shader_capture_session_fail_budget(
            token, "Vulkan capture could not retain all requested evidence");
    }
}

static void pgraph_vk_input_texture_status(uint64_t token, uint32_t slot,
                                           uint32_t status)
{
    char name[64];
    snprintf(name, sizeof(name), "capture.texture.%u.status", slot);
    pgraph_vk_input_status(token, name, status);
}

static PGRAPHVkShaderInputs *pgraph_vk_shader_inputs_find(PGRAPHVkState *r,
                                                          uint64_t token)
{
    if (!token) {
        return NULL;
    }
    for (PGRAPHVkShaderInputs *inputs = r->shader_browser_inputs; inputs;
         inputs = inputs->next) {
        if (inputs->token == token) {
            return inputs;
        }
    }
    return NULL;
}

static void pgraph_vk_shader_inputs_fail_budget(PGRAPHVkShaderInputs *inputs,
                                                const char *reason)
{
    if (!inputs->failed_budget &&
        xemu_shader_capture_session_token(inputs->token)) {
        inputs->failed_budget = true;
        xemu_shader_capture_session_fail_budget(inputs->token, reason);
    }
}

static PGRAPHVkInputReadback *
pgraph_vk_input_allocate_bytes(PGRAPHVkState *r, PGRAPHVkShaderInputs *inputs,
                               size_t raw_bytes, size_t owned_bytes,
                               uint32_t *status)
{
    size_t reservation = raw_bytes + owned_bytes;
    if (!raw_bytes || inputs->failed_budget || reservation > inputs->budget ||
        reservation > PGRAPH_VK_INPUT_STAGING_BUDGET -
                          r->shader_browser_input_staging_bytes) {
        *status |= PGRAPH_VK_INPUT_LIMIT;
        pgraph_vk_shader_inputs_fail_budget(
            inputs, "Vulkan capture readbacks exceed the owned staging budget");
        return NULL;
    }
    PGRAPHVkInputReadback *readback = g_try_new0(PGRAPHVkInputReadback, 1);
    if (!readback) {
        *status |= PGRAPH_VK_INPUT_ALLOCATION;
        pgraph_vk_shader_inputs_fail_budget(
            inputs, "Vulkan capture readback allocation failed");
        return NULL;
    }
    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = raw_bytes,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VmaAllocationCreateInfo allocation_info = {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                 VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
    };
    VmaAllocationInfo mapped_info;
    VkResult result =
        vmaCreateBuffer(r->allocator, &buffer_info, &allocation_info,
                        &readback->buffer, &readback->allocation, &mapped_info);
    if (result != VK_SUCCESS || !mapped_info.pMappedData) {
        if (result == VK_SUCCESS) {
            vmaDestroyBuffer(r->allocator, readback->buffer,
                             readback->allocation);
        }
        g_free(readback);
        *status |= PGRAPH_VK_INPUT_ALLOCATION;
        pgraph_vk_shader_inputs_fail_budget(
            inputs, "Vulkan capture readback buffer allocation failed");
        return NULL;
    }
    readback->mapped = mapped_info.pMappedData;
    readback->raw_bytes = raw_bytes;
    readback->rgba_bytes = owned_bytes;
    readback->next = inputs->readbacks;
    inputs->readbacks = readback;
    inputs->budget -= reservation;
    r->shader_browser_input_staging_bytes += reservation;
    return readback;
}

static PGRAPHVkInputReadback *
pgraph_vk_input_allocate(PGRAPHVkState *r, PGRAPHVkShaderInputs *inputs,
                         VkFormat format, uint32_t width, uint32_t height,
                         uint32_t *status)
{
    size_t raw_bytes = pgraph_vk_input_raw_bytes(format, width, height);
    if (!raw_bytes) {
        if (width > PGRAPH_VK_INPUT_MAX_DIMENSION ||
            height > PGRAPH_VK_INPUT_MAX_DIMENSION) {
            *status |= PGRAPH_VK_INPUT_LIMIT;
            pgraph_vk_shader_inputs_fail_budget(
                inputs, "Vulkan capture image exceeds 2048 pixels");
        } else {
            *status |= PGRAPH_VK_INPUT_UNSUPPORTED;
        }
        return NULL;
    }
    PGRAPHVkInputReadback *readback = pgraph_vk_input_allocate_bytes(
        r, inputs, raw_bytes,
        (size_t)width * height * 4 +
            (format == VK_FORMAT_R16_UNORM ? raw_bytes : 0),
        status);
    if (readback) {
        readback->storage_bytes = format == VK_FORMAT_R16_UNORM ? raw_bytes : 0;
        readback->rgba_bytes -= readback->storage_bytes;
        readback->width = width;
        readback->height = height;
        readback->format = format;
    }
    return readback;
}

static void pgraph_vk_input_record(PGRAPHState *pg,
                                   PGRAPHVkInputReadback *readback,
                                   VkImage image, VkImageLayout layout,
                                   uint32_t mip, uint32_t layer)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    pgraph_vk_transition_image_layout(pg, r->command_buffer, image,
                                      readback->format, layout,
                                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy region = {
        .imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .imageSubresource.mipLevel = mip,
        .imageSubresource.baseArrayLayer = layer,
        .imageSubresource.layerCount = 1,
        .imageExtent = { readback->width, readback->height, 1 },
    };
    vkCmdCopyImageToBuffer(r->command_buffer, image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readback->buffer, 1, &region);
    VkBufferMemoryBarrier host_barrier = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = readback->buffer,
        .size = VK_WHOLE_SIZE,
    };
    vkCmdPipelineBarrier(r->command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 1,
                         &host_barrier, 0, NULL);
    pgraph_vk_transition_image_layout(
        pg, r->command_buffer, image, readback->format,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, layout);
    readback->recorded = true;
}

static uint8_t pgraph_vk_input_component(VkComponentSwizzle swizzle,
                                         const uint8_t rgba[4], unsigned int i)
{
    switch (swizzle) {
    case VK_COMPONENT_SWIZZLE_ZERO:
        return 0;
    case VK_COMPONENT_SWIZZLE_ONE:
        return 255;
    case VK_COMPONENT_SWIZZLE_R:
    case VK_COMPONENT_SWIZZLE_G:
    case VK_COMPONENT_SWIZZLE_B:
    case VK_COMPONENT_SWIZZLE_A:
        return rgba[swizzle - VK_COMPONENT_SWIZZLE_R];
    default:
        return rgba[i];
    }
}

static uint8_t *pgraph_vk_input_decode(const PGRAPHVkInputReadback *readback)
{
    const uint8_t *raw = readback->mapped;
    uint8_t *pixels;
    if (readback->format == VK_FORMAT_BC1_RGBA_UNORM_BLOCK ||
        readback->format == VK_FORMAT_BC2_UNORM_BLOCK ||
        readback->format == VK_FORMAT_BC3_UNORM_BLOCK) {
        enum S3TC_DECOMPRESS_FORMAT format =
            readback->format == VK_FORMAT_BC1_RGBA_UNORM_BLOCK ?
                S3TC_DECOMPRESS_FORMAT_DXT1 :
            readback->format == VK_FORMAT_BC2_UNORM_BLOCK ?
                S3TC_DECOMPRESS_FORMAT_DXT3 :
                S3TC_DECOMPRESS_FORMAT_DXT5;
        pixels = g_try_malloc(readback->rgba_bytes);
        if (pixels) {
            s3tc_decompress_2d_into(format, raw, readback->width,
                                    readback->height, pixels);
        }
    } else {
        pixels = g_try_malloc(readback->rgba_bytes);
        if (!pixels) {
            return NULL;
        }
        size_t count = (size_t)readback->width * readback->height;
        for (size_t i = 0; i < count; ++i) {
            uint8_t *p = pixels + i * 4;
            p[0] = p[1] = p[2] = 0;
            p[3] = 255;
            uint16_t packed;
            switch (readback->format) {
            case VK_FORMAT_R8_UNORM:
                p[0] = raw[i];
                break;
            case VK_FORMAT_R8G8_UNORM:
                p[0] = raw[i * 2];
                p[1] = raw[i * 2 + 1];
                break;
            case VK_FORMAT_R16_UNORM:
                memcpy(&packed, raw + i * 2, sizeof(packed));
                p[0] = ((uint32_t)packed * 255 + 32767) / 65535;
                break;
            case VK_FORMAT_A1R5G5B5_UNORM_PACK16:
                memcpy(&packed, raw + i * 2, sizeof(packed));
                p[0] = ((packed >> 10) & 31) * 255 / 31;
                p[1] = ((packed >> 5) & 31) * 255 / 31;
                p[2] = (packed & 31) * 255 / 31;
                p[3] = packed & 0x8000 ? 255 : 0;
                break;
            case VK_FORMAT_R5G6B5_UNORM_PACK16:
                memcpy(&packed, raw + i * 2, sizeof(packed));
                p[0] = ((packed >> 11) & 31) * 255 / 31;
                p[1] = ((packed >> 5) & 63) * 255 / 63;
                p[2] = (packed & 31) * 255 / 31;
                break;
            case VK_FORMAT_A4R4G4B4_UNORM_PACK16:
                memcpy(&packed, raw + i * 2, sizeof(packed));
                p[0] = ((packed >> 8) & 15) * 17;
                p[1] = ((packed >> 4) & 15) * 17;
                p[2] = (packed & 15) * 17;
                p[3] = ((packed >> 12) & 15) * 17;
                break;
            case VK_FORMAT_R8G8B8_SNORM:
                for (unsigned int channel = 0; channel < 3; ++channel) {
                    int value = (int8_t)raw[i * 3 + channel];
                    p[channel] = value > 0 ? value * 255 / 127 : 0;
                }
                break;
            case VK_FORMAT_B8G8R8A8_UNORM:
                p[0] = raw[i * 4 + 2];
                p[1] = raw[i * 4 + 1];
                p[2] = raw[i * 4];
                p[3] = raw[i * 4 + 3];
                break;
            case VK_FORMAT_R8G8B8A8_UNORM:
                memcpy(p, raw + i * 4, 4);
                break;
            default:
                g_free(pixels);
                return NULL;
            }
        }
    }
    if (!pixels) {
        return NULL;
    }
    for (size_t i = 0; i < (size_t)readback->width * readback->height; ++i) {
        uint8_t rgba[4];
        memcpy(rgba, pixels + i * 4, 4);
        pixels[i * 4] =
            pgraph_vk_input_component(readback->components.r, rgba, 0);
        pixels[i * 4 + 1] =
            pgraph_vk_input_component(readback->components.g, rgba, 1);
        pixels[i * 4 + 2] =
            pgraph_vk_input_component(readback->components.b, rgba, 2);
        pixels[i * 4 + 3] =
            pgraph_vk_input_component(readback->components.a, rgba, 3);
    }
    return pixels;
}

static bool
pgraph_vk_input_stage_texture_storage(uint64_t token,
                                      PGRAPHVkInputReadback *readback)
{
    if (readback->format != VK_FORMAT_R16_UNORM || readback->before != -1 ||
        readback->texture.mip_level || readback->texture.face ||
        readback->texture.face_count != 1)
        return true;
    char name[64];
    snprintf(name, sizeof(name), "texture.storage.%u", readback->texture.slot);
    XemuShaderDrawBlob blob = {
        .name = name,
        .data = readback->mapped,
        .byte_count = readback->raw_bytes,
        .format = VK_FORMAT_R16_UNORM,
        .components = 1,
        .stride = 2,
        .count = (size_t)readback->width * readback->height,
        .slot = readback->texture.slot,
        .normalized = 1,
    };
    return xemu_shader_draw_request_stage_blob(token, &blob);
}

static void pgraph_vk_shader_inputs_retire(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    while (r->shader_browser_inputs) {
        PGRAPHVkShaderInputs *inputs = r->shader_browser_inputs;
        bool current = xemu_shader_draw_request_wants_inputs(inputs->token);
        for (PGRAPHVkInputReadback *readback = inputs->readbacks; readback;) {
            PGRAPHVkInputReadback *next = readback->next;
            if (current && readback->recorded &&
                (inputs->emitted ||
                 xemu_shader_capture_session_token(inputs->token))) {
                uint32_t status = 0;
                bool valid =
                    vmaInvalidateAllocation(r->allocator, readback->allocation,
                                            0, VK_WHOLE_SIZE) == VK_SUCCESS;
                g_autofree uint8_t *pixels = NULL;
                bool accepted = false;
                if (readback->before == -2) {
                    if (valid) {
                        readback->blob.data = (uint8_t *)readback->mapped +
                                              readback->blob_data_offset;
                        accepted = xemu_shader_draw_request_stage_blob(
                            inputs->token, &readback->blob);
                        if (accepted)
                            xemu_shader_capture_session_resource_read_snapshot(
                                inputs->token,
                                XEMU_SHADER_CAPTURE_RESOURCE_BUFFER,
                                readback->blob.slot, readback->blob.name);
                    }
                } else {
                    if (valid) {
                        pixels = pgraph_vk_input_decode(readback);
                    }
                    if (pixels) {
                        XemuShaderDrawImage image = {
                            readback->width,
                            readback->height,
                            pixels,
                            readback->rgba_bytes,
                        };
                        if (readback->before >= 0) {
                            accepted = xemu_shader_draw_request_stage_image(
                                inputs->token, readback->before, &image);
                        } else {
                            readback->texture.image = image;
                            accepted = xemu_shader_draw_request_stage_texture(
                                inputs->token, &readback->texture);
                            if (accepted)
                                accepted =
                                    pgraph_vk_input_stage_texture_storage(
                                        inputs->token, readback);
                        }
                    }
                }
                if (!valid || (readback->before != -2 && !pixels)) {
                    status = PGRAPH_VK_INPUT_ALLOCATION;
                    pgraph_vk_shader_inputs_fail_budget(
                        inputs, "Vulkan capture readback resolution failed");
                } else if (!accepted) {
                    status = PGRAPH_VK_INPUT_REJECTED;
                    pgraph_vk_shader_inputs_fail_budget(
                        inputs,
                        "Vulkan capture evidence was rejected by the recorder");
                }
                if (readback->before >= 0) {
                    pgraph_vk_input_status(inputs->token,
                                           readback->before ?
                                               "capture.before.status" :
                                               "capture.after.status",
                                           status);
                } else if (readback->before == -2) {
                    inputs->vertex_status |= status;
                } else {
                    inputs->texture_status[readback->texture.slot] |= status;
                }
            }
            vmaDestroyBuffer(r->allocator, readback->buffer,
                             readback->allocation);
            r->shader_browser_input_staging_bytes -= readback->raw_bytes +
                                                     readback->rgba_bytes +
                                                     readback->storage_bytes;
            g_free(readback);
            readback = next;
        }
        for (uint32_t slot = 0; slot < NV2A_MAX_TEXTURES; ++slot) {
            if (inputs->texture_status[slot]) {
                pgraph_vk_input_texture_status(inputs->token, slot,
                                               inputs->texture_status[slot]);
            }
        }
        if (inputs->vertex_status) {
            pgraph_vk_input_status(inputs->token, "capture.vertices.status",
                                   inputs->vertex_status);
        }
        if (current) {
            xemu_shader_draw_request_inputs_complete(inputs->token);
        }
        r->shader_browser_inputs = inputs->next;
        r->shader_browser_input_events--;
        g_free(inputs);
    }
}

static void pgraph_vk_input_stage_uniforms(PGRAPHVkState *r, uint64_t token,
                                           ShaderModuleInfo *module,
                                           const UniformInfo *info,
                                           const int *locations, size_t count,
                                           uint32_t stage, size_t offset,
                                           size_t *budget, uint32_t *status)
{
    if (!module) {
        *status |= PGRAPH_VK_INPUT_MISSING;
        return;
    }
    const ShaderUniformLayout *layout = &module->uniforms;
    const StorageBuffer *buffer = &r->storage_buffers[BUFFER_UNIFORM_STAGING];
    if (!layout->total_size) {
        return;
    }
    if (!buffer->mapped || offset > buffer->buffer_offset ||
        layout->total_size > buffer->buffer_offset - offset) {
        *status |= PGRAPH_VK_INPUT_MISSING;
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        int location = locations[i];
        if (location <= 0 || (size_t)location > layout->num_uniforms) {
            continue;
        }
        const ShaderUniform *uniform = &layout->uniforms[location - 1];
        size_t bytes = info[i].size * info[i].count;
        if (!uniform->dim_v || !uniform->dim_a || uniform->dim_v > 4 ||
            uniform->dim_a > 4096 ||
            uniform->dim_v * uniform->dim_a * 4 != bytes ||
            uniform->offset > layout->total_size ||
            uniform->dim_v * 4 > layout->total_size - uniform->offset ||
            (uniform->dim_a > 1 &&
             (!uniform->stride || uniform->stride > layout->total_size ||
              uniform->dim_a - 1 >
                  (layout->total_size - uniform->offset - uniform->dim_v * 4) /
                      uniform->stride))) {
            *status |= PGRAPH_VK_INPUT_UNSUPPORTED;
            continue;
        }
        if (bytes > *budget) {
            *status |= PGRAPH_VK_INPUT_LIMIT;
            continue;
        }
        g_autofree uint8_t *data = g_try_malloc(bytes);
        if (!data) {
            *status |= PGRAPH_VK_INPUT_ALLOCATION;
            continue;
        }
        for (size_t row = 0; row < uniform->dim_a; ++row) {
            memcpy(data + row * uniform->dim_v * 4,
                   buffer->mapped + offset + uniform->offset +
                       row * uniform->stride,
                   uniform->dim_v * 4);
        }
        uint32_t type = XEMU_SHADER_DRAW_UNIFORM_FLOAT;
        switch (info[i].type) {
        case UniformElementType_int:
        case UniformElementType_ivec2:
        case UniformElementType_ivec4:
            type = XEMU_SHADER_DRAW_UNIFORM_INT;
            break;
        case UniformElementType_uint:
            type = XEMU_SHADER_DRAW_UNIFORM_UINT;
            break;
        case UniformElementType_mat2:
            type = XEMU_SHADER_DRAW_UNIFORM_MAT2;
            break;
        default:
            break;
        }
        XemuShaderDrawUniform captured = {
            .stage = stage,
            .name = info[i].name,
            .type = type,
            .components = info[i].size / 4,
            .count = info[i].count,
            .data = data,
            .byte_count = bytes,
        };
        if (xemu_shader_draw_request_stage_uniform(token, &captured)) {
            *budget -= bytes;
        } else {
            *status |= PGRAPH_VK_INPUT_REJECTED;
        }
    }
    /* Replacement members without a canonical NV2A type remain explicit. */
    for (size_t i = 0; i < layout->num_uniforms; ++i) {
        bool known = false;
        for (size_t j = 0; j < count; ++j) {
            known |= locations[j] == (int)i + 1;
        }
        if (!known) {
            *status |= PGRAPH_VK_INPUT_UNSUPPORTED;
        }
    }
}

static void pgraph_vk_input_stage_program(PGRAPHVkState *r, uint64_t token,
                                          size_t *budget)
{
    ShaderBinding *binding = r->shader_binding;
    ShaderModuleInfo *modules[] = {
        binding->vsh.module_info,
        binding->psh.module_info,
        binding->geom.module_info,
    };
    const uint32_t stages[] = {
        XEMU_SHADER_BROWSER_STAGE_VERTEX,
        XEMU_SHADER_BROWSER_STAGE_PIXEL,
        XEMU_SHADER_BROWSER_STAGE_GEOMETRY,
    };
    uint32_t source_status = 0, uniform_status = 0;
    size_t source_budget = PGRAPH_VK_INPUT_SOURCE_BUDGET;
    for (size_t i = 0; i < G_N_ELEMENTS(modules); ++i) {
        if (!modules[i]) {
            if (i < 2) {
                source_status |= PGRAPH_VK_INPUT_MISSING;
            }
            continue;
        }
        if (!modules[i]->glsl) {
            source_status |= PGRAPH_VK_INPUT_MISSING;
            continue;
        }
        size_t bytes =
            strnlen(modules[i]->glsl, PGRAPH_VK_INPUT_SOURCE_BUDGET + 1);
        if (!bytes || bytes > source_budget || bytes > *budget) {
            source_status |= PGRAPH_VK_INPUT_LIMIT;
            continue;
        }
        if (xemu_shader_draw_request_stage_source(token, stages[i],
                                                  modules[i]->glsl, bytes)) {
            source_budget -= bytes;
            *budget -= bytes;
        } else {
            source_status |= PGRAPH_VK_INPUT_REJECTED;
        }
    }
    pgraph_vk_input_stage_uniforms(r, token, modules[0], VshUniformInfo,
                                   binding->vsh.uniform_locs, VshUniform__COUNT,
                                   stages[0], r->uniform_buffer_offsets[0],
                                   budget, &uniform_status);
    pgraph_vk_input_stage_uniforms(r, token, modules[1], PshUniformInfo,
                                   binding->psh.uniform_locs, PshUniform__COUNT,
                                   stages[1], r->uniform_buffer_offsets[1],
                                   budget, &uniform_status);
    if (modules[1] && modules[1]->uses_uber_controls) {
        StorageBuffer *buffer = &r->storage_buffers[BUFFER_UNIFORM_STAGING];
        size_t bytes = sizeof(PGRAPHUberControls);
        if (!buffer->mapped || r->uber_control_offset > buffer->buffer_offset ||
            bytes > buffer->buffer_offset - r->uber_control_offset) {
            uniform_status |= PGRAPH_VK_INPUT_MISSING;
        } else if (bytes > *budget) {
            uniform_status |= PGRAPH_VK_INPUT_LIMIT;
        } else {
            const size_t raw_bytes = offsetof(PGRAPHUberControls, constants);
            XemuShaderDrawUniform captured = {
                .stage = stages[1],
                .name = "xemuUberControls",
                .type = XEMU_SHADER_DRAW_UNIFORM_UINT,
                .components = 4,
                .count = raw_bytes / 16,
                .data = buffer->mapped + r->uber_control_offset,
                .byte_count = raw_bytes,
            };
            if (xemu_shader_draw_request_stage_uniform(token, &captured)) {
                *budget -= raw_bytes;
            } else {
                uniform_status |= PGRAPH_VK_INPUT_REJECTED;
            }
            captured.name = "xemuUberConstants";
            captured.type = XEMU_SHADER_DRAW_UNIFORM_FLOAT;
            captured.count = (bytes - raw_bytes) / 16;
            captured.data = buffer->mapped + r->uber_control_offset + raw_bytes;
            captured.byte_count = bytes - raw_bytes;
            if (xemu_shader_draw_request_stage_uniform(token, &captured)) {
                *budget -= bytes - raw_bytes;
            } else {
                uniform_status |= PGRAPH_VK_INPUT_REJECTED;
            }
        }
    }
    pgraph_vk_input_status(token, "capture.sources.status", source_status);
    pgraph_vk_input_status(token, "capture.uniforms.status", uniform_status);
}

static void pgraph_vk_input_stage_registers(PGRAPHState *pg, uint64_t token)
{
    static const struct {
        const char *name;
        uint32_t offset;
    } registers[] = {
#define PGRAPH_VK_INPUT_REGISTER(reg) { #reg, reg }
        PGRAPH_VK_INPUT_REGISTER(NV_PGRAPH_SURFACE),
        PGRAPH_VK_INPUT_REGISTER(NV_PGRAPH_CONTROL_0),
        PGRAPH_VK_INPUT_REGISTER(NV_PGRAPH_CONTROL_1),
        PGRAPH_VK_INPUT_REGISTER(NV_PGRAPH_CONTROL_2),
        PGRAPH_VK_INPUT_REGISTER(NV_PGRAPH_CONTROL_3),
        PGRAPH_VK_INPUT_REGISTER(NV_PGRAPH_BLEND),
        PGRAPH_VK_INPUT_REGISTER(NV_PGRAPH_BLENDCOLOR),
        PGRAPH_VK_INPUT_REGISTER(NV_PGRAPH_SETUPRASTER),
        PGRAPH_VK_INPUT_REGISTER(NV_PGRAPH_SHADERCLIPMODE),
        PGRAPH_VK_INPUT_REGISTER(NV_PGRAPH_SHADERCTL),
        PGRAPH_VK_INPUT_REGISTER(NV_PGRAPH_SHADERPROG),
        PGRAPH_VK_INPUT_REGISTER(NV_PGRAPH_COMBINECTL),
        PGRAPH_VK_INPUT_REGISTER(NV_PGRAPH_COMBINESPECFOG0),
        PGRAPH_VK_INPUT_REGISTER(NV_PGRAPH_COMBINESPECFOG1),
        PGRAPH_VK_INPUT_REGISTER(NV_PGRAPH_FOGCOLOR),
        PGRAPH_VK_INPUT_REGISTER(NV_PGRAPH_FOGPARAM0),
        PGRAPH_VK_INPUT_REGISTER(NV_PGRAPH_FOGPARAM1),
        PGRAPH_VK_INPUT_REGISTER(NV_PGRAPH_ZCLIPMIN),
        PGRAPH_VK_INPUT_REGISTER(NV_PGRAPH_ZCLIPMAX),
        PGRAPH_VK_INPUT_REGISTER(NV_PGRAPH_ZOFFSETBIAS),
        PGRAPH_VK_INPUT_REGISTER(NV_PGRAPH_ZOFFSETFACTOR),
#undef PGRAPH_VK_INPUT_REGISTER
    };
    for (size_t i = 0; i < G_N_ELEMENTS(registers); ++i) {
        xemu_shader_draw_request_stage_register(
            token, registers[i].name, pgraph_reg_r(pg, registers[i].offset));
    }
    static const struct {
        const char *name;
        uint32_t offset, count;
    } groups[] = {
        { "NV_PGRAPH_TEXFMT", NV_PGRAPH_TEXFMT0, 4 },
        { "NV_PGRAPH_TEXCTL0_", NV_PGRAPH_TEXCTL0_0, 4 },
        { "NV_PGRAPH_TEXCTL1_", NV_PGRAPH_TEXCTL1_0, 4 },
        { "NV_PGRAPH_TEXFILTER", NV_PGRAPH_TEXFILTER0, 4 },
        { "NV_PGRAPH_TEXADDRESS", NV_PGRAPH_TEXADDRESS0, 4 },
        { "NV_PGRAPH_TEXIMAGERECT", NV_PGRAPH_TEXIMAGERECT0, 4 },
        { "NV_PGRAPH_TEXOFFSET", NV_PGRAPH_TEXOFFSET0, 4 },
        { "NV_PGRAPH_TEXPALETTE", NV_PGRAPH_TEXPALETTE0, 4 },
        { "NV_PGRAPH_BORDERCOLOR", NV_PGRAPH_BORDERCOLOR0, 4 },
        { "NV_PGRAPH_COMBINEFACTOR0[", NV_PGRAPH_COMBINEFACTOR0, 8 },
        { "NV_PGRAPH_COMBINEFACTOR1[", NV_PGRAPH_COMBINEFACTOR1, 8 },
        { "NV_PGRAPH_COMBINEALPHAI", NV_PGRAPH_COMBINEALPHAI0, 8 },
        { "NV_PGRAPH_COMBINEALPHAO", NV_PGRAPH_COMBINEALPHAO0, 8 },
        { "NV_PGRAPH_COMBINECOLORI", NV_PGRAPH_COMBINECOLORI0, 8 },
        { "NV_PGRAPH_COMBINECOLORO", NV_PGRAPH_COMBINECOLORO0, 8 },
        { "NV_PGRAPH_WINDOWCLIPX", NV_PGRAPH_WINDOWCLIPX0, 8 },
        { "NV_PGRAPH_WINDOWCLIPY", NV_PGRAPH_WINDOWCLIPY0, 8 },
    };
    for (size_t group = 0; group < G_N_ELEMENTS(groups); ++group) {
        for (uint32_t i = 0; i < groups[group].count; ++i) {
            char name[64];
            snprintf(name, sizeof(name), "%s%u%s", groups[group].name, i,
                     strchr(groups[group].name, '[') ? "]" : "");
            xemu_shader_draw_request_stage_register(
                token, name, pgraph_reg_r(pg, groups[group].offset + i * 4));
        }
    }
}

static bool pgraph_vk_input_depth_texture(uint32_t format, VkFormat host)
{
    // Guest depth textures are uploaded as ordinary sampled color images.
    // Capture that actual bound representation rather than rejecting its label.
    if (pgraph_vk_input_raw_bytes(host, 1, 1))
        return false;
    switch (format) {
    case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_DEPTH_Y16_FIXED:
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_DEPTH_X8_Y24_FIXED:
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_DEPTH_X8_Y24_FLOAT:
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_DEPTH_Y16_FIXED:
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_DEPTH_Y16_FLOAT:
        return true;
    default:
        return false;
    }
}

static void pgraph_vk_input_stage_textures(PGRAPHState *pg, uint64_t token,
                                           PGRAPHVkShaderInputs *inputs)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    for (uint32_t slot = 0; slot < NV2A_MAX_TEXTURES; ++slot) {
        TextureBinding *binding = r->texture_bindings[slot];
        XemuShaderDrawTexture texture = {
            .slot = slot,
            .bound = binding && binding != &r->dummy_texture,
        };
        uint32_t status = 0;
        if (texture.bound) {
            texture.guest_format = binding->key.state.color_format;
            texture.host_format = binding->key.vk_format;
            texture.width = binding->storage_extent.width;
            texture.height = binding->storage_extent.height;
            texture.depth = binding->storage_extent.depth;
            texture.mip_levels = binding->storage_mip_levels;
            texture.face_count = binding->storage_layer_count;
            texture.min_filter = binding->sampler_min_filter;
            texture.mag_filter = binding->sampler_mag_filter;
            texture.wrap_s = binding->sampler_wrap_s;
            texture.wrap_t = binding->sampler_wrap_t;
            texture.wrap_r = binding->sampler_wrap_r;
            texture.coordinate_scale = binding->key.scale;
            // Raw typed storage is before view swizzling; retain the exact
            // view.
            const VkComponentSwizzle channels[] = {
                binding->component_mapping.r,
                binding->component_mapping.g,
                binding->component_mapping.b,
                binding->component_mapping.a,
            };
            const uint32_t identity[] = { 0x1903, 0x1904, 0x1905, 0x1906 };
            for (uint32_t channel = 0; channel < 4; ++channel) {
                const uint32_t value =
                    channels[channel] == VK_COMPONENT_SWIZZLE_IDENTITY ?
                        identity[channel] :
                    channels[channel] == VK_COMPONENT_SWIZZLE_ZERO ?
                        0 :
                    channels[channel] == VK_COMPONENT_SWIZZLE_ONE ?
                        1 :
                        identity[channels[channel] - VK_COMPONENT_SWIZZLE_R];
                char name[64];
                snprintf(name, sizeof(name), "capture.texture%u.swizzle%u",
                         slot, channel);
                xemu_shader_draw_request_stage_register(token, name, value);
            }
        }
        if (!xemu_shader_draw_request_stage_texture(token, &texture)) {
            status |= PGRAPH_VK_INPUT_REJECTED;
        }
        if (!texture.bound) {
            pgraph_vk_input_texture_status(token, slot,
                                           PGRAPH_VK_INPUT_MISSING);
            continue;
        }
        if (!inputs) {
            status |= PGRAPH_VK_INPUT_MISSING;
        } else if (!binding->input_transfer_src ||
                   binding->storage_image_type != VK_IMAGE_TYPE_2D ||
                   texture.depth != 1 ||
                   pgraph_vk_input_depth_texture(texture.guest_format,
                                                 binding->key.vk_format) ||
                   binding->current_layout !=
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
                   !texture.mip_levels || texture.mip_levels > 16 ||
                   (texture.face_count != 1 && texture.face_count != 6)) {
            status |= PGRAPH_VK_INPUT_UNSUPPORTED;
        } else {
            for (uint32_t face = 0; face < texture.face_count; ++face) {
                for (uint32_t mip = 0; mip < texture.mip_levels; ++mip) {
                    uint32_t width = MAX(1, texture.width >> mip);
                    uint32_t height = MAX(1, texture.height >> mip);
                    PGRAPHVkInputReadback *readback = pgraph_vk_input_allocate(
                        r, inputs, binding->key.vk_format, width, height,
                        &status);
                    if (!readback) {
                        continue;
                    }
                    readback->before = -1;
                    readback->texture = texture;
                    readback->texture.mip_level = mip;
                    readback->texture.face = face;
                    readback->components = binding->component_mapping;
                    pgraph_vk_input_record(pg, readback, binding->image,
                                           binding->current_layout, mip, face);
                }
            }
        }
        if (inputs) {
            inputs->texture_status[slot] |= status;
        }
        pgraph_vk_input_texture_status(token, slot, status);
    }
}

static void get_size_and_count_for_format(VkFormat format, size_t *size,
                                          size_t *count);

static bool pgraph_vk_input_stage_blob(uint64_t token,
                                       const XemuShaderDrawBlob *blob,
                                       size_t *budget, uint32_t *status)
{
    if (blob->byte_count > *budget ||
        blob->byte_count > PGRAPH_VK_INPUT_BLOB_BUDGET) {
        *status |= PGRAPH_VK_INPUT_LIMIT;
        if (xemu_shader_capture_session_token(token)) {
            xemu_shader_capture_session_fail_budget(
                token, "Vulkan raw vertex evidence exceeds the event budget");
        }
        return false;
    }
    if (!xemu_shader_draw_request_stage_blob(token, blob)) {
        *status |= PGRAPH_VK_INPUT_REJECTED;
        if (xemu_shader_capture_session_token(token)) {
            xemu_shader_capture_session_fail_budget(
                token,
                "Vulkan raw vertex evidence was rejected by the recorder");
        }
        return false;
    }
    *budget -= blob->byte_count;
    return true;
}

static void pgraph_vk_input_stage_pipeline(PGRAPHVkState *r, uint64_t token,
                                           size_t *budget)
{
    uint32_t status = 0;
    if (!r->pipeline_binding) {
        pgraph_vk_input_status(token, "capture.pipeline.status",
                               PGRAPH_VK_INPUT_MISSING);
        return;
    }
    const PipelineBinding *pipeline = r->pipeline_binding;
    const PGRAPHVkCapturedPipelineState *state = &pipeline->capture_state;
    const struct {
        const char *name;
        const void *data;
        size_t bytes;
    } values[] = {
        { "vk.viewport", &r->capture_viewport, sizeof(r->capture_viewport) },
        { "vk.scissor", &r->capture_scissor, sizeof(r->capture_scissor) },
        { "vk.line_width", &r->capture_line_width,
          sizeof(r->capture_line_width) },
        { "vk.blend_constants", r->blend_constants.packed_color,
          sizeof(r->blend_constants.packed_color) },
        { "vk.pipeline.key", &pipeline->key, sizeof(pipeline->key) },
        { "vk.pipeline.assembly", &state->assembly, sizeof(state->assembly) },
        { "vk.pipeline.raster", &state->raster, sizeof(state->raster) },
        { "vk.pipeline.multisample", &state->multisample,
          sizeof(state->multisample) },
        { "vk.pipeline.depth_stencil", &state->depth_stencil,
          sizeof(state->depth_stencil) },
        { "vk.pipeline.blend", &state->blend, sizeof(state->blend) },
        { "vk.pipeline.blend_attachment", &state->color_attachment,
          sizeof(state->color_attachment) },
    };
    for (size_t i = 0; i < G_N_ELEMENTS(values); ++i) {
        XemuShaderDrawBlob blob = {
            .name = values[i].name,
            .data = values[i].data,
            .byte_count = values[i].bytes,
        };
        pgraph_vk_input_stage_blob(token, &blob, budget, &status);
    }
    pgraph_vk_input_status(token, "capture.vk.pipeline_abi", 1);
    pgraph_vk_input_status(token, "capture.vk.dynamic_blend_constant_mask",
                           pipeline->dynamic_blend_constant_mask);
    pgraph_vk_input_status(token, "capture.vk.dynamic_line_width",
                           pipeline->has_dynamic_line_width);
    pgraph_vk_input_status(token, "capture.pipeline.status", status);
}

static void pgraph_vk_input_record_vertex(PGRAPHState *pg,
                                          PGRAPHVkInputReadback *readback,
                                          VkBuffer source, size_t offset)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    VkBufferMemoryBarrier source_barrier = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT |
                         VK_ACCESS_TRANSFER_WRITE_BIT |
                         VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = source,
        .offset = offset,
        .size = readback->raw_bytes,
    };
    vkCmdPipelineBarrier(r->command_buffer,
                         VK_PIPELINE_STAGE_HOST_BIT |
                             VK_PIPELINE_STAGE_TRANSFER_BIT |
                             VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 1,
                         &source_barrier, 0, NULL);
    VkBufferCopy copy = {
        .srcOffset = offset,
        .size = readback->raw_bytes,
    };
    vkCmdCopyBuffer(r->command_buffer, source, readback->buffer, 1, &copy);
    VkBufferMemoryBarrier host_barrier = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = readback->buffer,
        .size = VK_WHOLE_SIZE,
    };
    source_barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    source_barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    VkBufferMemoryBarrier barriers[] = { source_barrier, host_barrier };
    vkCmdPipelineBarrier(r->command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_VERTEX_INPUT_BIT |
                             VK_PIPELINE_STAGE_HOST_BIT,
                         0, 0, NULL, G_N_ELEMENTS(barriers), barriers, 0, NULL);
    readback->recorded = true;
}

static void pgraph_vk_input_stage_vertices(
    PGRAPHState *pg, uint64_t token, PGRAPHVkShaderInputs *inputs,
    uint16_t inline_map, VkDeviceSize base_offset, int32_t start, int32_t count,
    VkDeviceSize index_offset, uint32_t index_count, size_t *budget)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    uint32_t status = 0;
    uint64_t first = 0, vertex_count = 0;
    bool range_valid = false;
    if (index_count) {
        StorageBuffer *indices = &r->storage_buffers[BUFFER_INDEX_STAGING];
        size_t bytes = (size_t)index_count * sizeof(uint32_t);
        if (indices->mapped && index_offset <= indices->buffer_offset &&
            bytes <= indices->buffer_offset - index_offset) {
            const uint8_t *data = indices->mapped + index_offset;
            uint32_t minimum = UINT32_MAX, maximum = 0;
            for (uint32_t i = 0; i < index_count; ++i) {
                uint32_t value;
                memcpy(&value, data + i * sizeof(value), sizeof(value));
                minimum = MIN(minimum, value);
                maximum = MAX(maximum, value);
            }
            first = minimum;
            vertex_count = (uint64_t)maximum - minimum + 1;
            range_valid = true;
            XemuShaderDrawBlob blob = {
                .name = "vertex.indices",
                .data = data,
                .byte_count = bytes,
                .format = VK_FORMAT_R32_UINT,
                .components = 1,
                .stride = sizeof(uint32_t),
                .count = index_count,
                .offset = index_offset,
                .integer = 1,
            };
            pgraph_vk_input_stage_blob(token, &blob, budget, &status);
            if (xemu_shader_capture_session_token(token)) {
                StorageBuffer *bound = &r->storage_buffers[BUFFER_INDEX];
                XemuShaderCaptureResource resource = {
                    .owner = bound->capture_owner,
                    .byte_size = bound->buffer_size,
                    .offset = index_offset,
                    .size = bytes,
                    .access = XEMU_SHADER_CAPTURE_RESOURCE_READ,
                    .kind = XEMU_SHADER_CAPTURE_RESOURCE_BUFFER,
                    .slot = NV2A_VERTEXSHADER_ATTRIBUTES,
                };
                xemu_shader_capture_session_resource_snapshot(token, &resource,
                                                              blob.name);
            }
        } else {
            status |= PGRAPH_VK_INPUT_MISSING;
        }
    } else if (start >= 0 && count > 0) {
        first = start;
        vertex_count = count;
        range_valid = true;
    } else {
        status |= PGRAPH_VK_INPUT_UNSUPPORTED;
    }
    pgraph_vk_input_status(token, "capture.vertices.inline_mask", inline_map);
    pgraph_vk_input_status(token, "capture.vertices.uniform_mask",
                           r->shader_binding->state.vsh.uniform_attrs);
    pgraph_vk_input_status(token, "capture.vertices.compressed_mask",
                           pg->compressed_attrs);
    pgraph_vk_input_status(token, "capture.vertices.swizzle_mask",
                           pg->swizzle_attrs);
    pgraph_vk_input_status(token, "capture.vertices.first", first);
    pgraph_vk_input_status(token, "capture.vertices.count",
                           MIN(vertex_count, UINT32_MAX));
    for (uint32_t slot = 0; slot < NV2A_VERTEXSHADER_ATTRIBUTES; ++slot) {
        char name[64];
        snprintf(name, sizeof(name), "vertex.current%u", slot);
        XemuShaderDrawBlob current = {
            .name = name,
            .data = pg->vertex_attributes[slot].inline_value,
            .byte_count = sizeof(pg->vertex_attributes[slot].inline_value),
            .slot = slot,
            .format = VK_FORMAT_R32G32B32A32_SFLOAT,
            .components = 4,
            .stride = 16,
            .count = 1,
        };
        pgraph_vk_input_stage_blob(token, &current, budget, &status);
        int desc = r->vertex_attribute_to_description_location[slot];
        snprintf(name, sizeof(name), "capture.vertex.%u.enabled", slot);
        pgraph_vk_input_status(token, name, desc >= 0);
        if (desc < 0) {
            continue;
        }
        const VkVertexInputAttributeDescription *attribute =
            &r->vertex_attribute_descriptions[desc];
        const VkVertexInputBindingDescription *binding =
            &r->vertex_binding_descriptions[desc];
        size_t element_size, components;
        get_size_and_count_for_format(attribute->format, &element_size,
                                      &components);
        size_t element_bytes = element_size * components;
        snprintf(name, sizeof(name), "capture.vertex.%u.format", slot);
        pgraph_vk_input_status(token, name, attribute->format);
        snprintf(name, sizeof(name), "capture.vertex.%u.guest_format", slot);
        pgraph_vk_input_status(token, name, pg->vertex_attributes[slot].format);
        snprintf(name, sizeof(name), "capture.vertex.%u.stride", slot);
        pgraph_vk_input_status(token, name, binding->stride);
        if (!range_valid) {
            continue;
        }
        uint64_t offset = r->vertex_attribute_offsets[slot];
        uint64_t step = binding->stride;
        if (vertex_count > UINT32_MAX || base_offset > UINT64_MAX - offset ||
            attribute->offset > UINT64_MAX - offset - base_offset ||
            (step &&
             first > (UINT64_MAX - offset - base_offset - attribute->offset) /
                         step) ||
            (step && vertex_count - 1 > (SIZE_MAX - element_bytes) / step)) {
            status |= PGRAPH_VK_INPUT_LIMIT;
            if (inputs) {
                pgraph_vk_shader_inputs_fail_budget(
                    inputs,
                    "Vulkan capture vertex range exceeds the evidence budget");
            }
            continue;
        }
        offset += base_offset + attribute->offset + first * step;
        size_t bytes = (vertex_count - 1) * step + element_bytes;
        if (bytes > PGRAPH_VK_INPUT_BLOB_BUDGET) {
            status |= PGRAPH_VK_INPUT_LIMIT;
            if (inputs) {
                pgraph_vk_shader_inputs_fail_budget(
                    inputs, "Vulkan capture vertex stream exceeds 16 MiB");
            }
            continue;
        }
        snprintf(name, sizeof(name), "vertex.attribute%u", slot);
        XemuShaderDrawBlob blob = {
            .name = name,
            .byte_count = bytes,
            .slot = slot,
            .format = attribute->format,
            .components = components,
            .stride = binding->stride,
            .count = vertex_count,
            .offset = offset,
            .normalized = attribute->format == VK_FORMAT_R8_UNORM ||
                          attribute->format == VK_FORMAT_R8G8_UNORM ||
                          attribute->format == VK_FORMAT_R8G8B8_UNORM ||
                          attribute->format == VK_FORMAT_R8G8B8A8_UNORM ||
                          attribute->format == VK_FORMAT_R16_SNORM ||
                          attribute->format == VK_FORMAT_R16G16_SNORM ||
                          attribute->format == VK_FORMAT_R16G16B16_SNORM ||
                          attribute->format == VK_FORMAT_R16G16B16A16_SNORM,
            .integer = attribute->format == VK_FORMAT_R32_SINT,
        };
        if (inline_map & (1 << slot)) {
            StorageBuffer *buffer =
                &r->storage_buffers[BUFFER_VERTEX_INLINE_STAGING];
            if (!buffer->mapped || offset > buffer->buffer_offset ||
                bytes > buffer->buffer_offset - offset) {
                status |= PGRAPH_VK_INPUT_MISSING;
                continue;
            }
            blob.data = buffer->mapped + offset;
            pgraph_vk_input_stage_blob(token, &blob, budget, &status);
            if (xemu_shader_capture_session_token(token)) {
                StorageBuffer *bound =
                    &r->storage_buffers[BUFFER_VERTEX_INLINE];
                XemuShaderCaptureResource resource = {
                    .owner = bound->capture_owner,
                    .byte_size = bound->buffer_size,
                    .offset = offset,
                    .size = bytes,
                    .access = XEMU_SHADER_CAPTURE_RESOURCE_READ,
                    .kind = XEMU_SHADER_CAPTURE_RESOURCE_BUFFER,
                    .slot = slot,
                };
                xemu_shader_capture_session_resource_snapshot(token, &resource,
                                                              blob.name);
            }
        } else if (inputs) {
            StorageBuffer *buffer = &r->storage_buffers[BUFFER_VERTEX_RAM];
            if (xemu_shader_capture_session_token(token)) {
                XemuShaderCaptureResource resource = {
                    .owner = buffer->capture_owner,
                    .byte_size = buffer->buffer_size,
                    .offset = offset,
                    .size = bytes,
                    .access = XEMU_SHADER_CAPTURE_RESOURCE_READ,
                    .kind = XEMU_SHADER_CAPTURE_RESOURCE_BUFFER,
                    .slot = slot,
                };
                xemu_shader_capture_session_resource(token, &resource);
            }
            size_t copy_offset = offset & ~(size_t)3;
            size_t padding = offset - copy_offset;
            if (bytes > SIZE_MAX - padding - 3) {
                status |= PGRAPH_VK_INPUT_LIMIT;
                pgraph_vk_shader_inputs_fail_budget(
                    inputs,
                    "Vulkan capture vertex copy exceeds the evidence budget");
                continue;
            }
            size_t copy_bytes = ROUND_UP(bytes + padding, 4);
            if (copy_offset > buffer->buffer_size ||
                copy_bytes > buffer->buffer_size - copy_offset) {
                status |= PGRAPH_VK_INPUT_MISSING;
                continue;
            }
            PGRAPHVkInputReadback *readback = pgraph_vk_input_allocate_bytes(
                r, inputs, copy_bytes, bytes, &status);
            if (!readback) {
                continue;
            }
            readback->before = -2;
            readback->blob = blob;
            g_strlcpy(readback->blob_name, name, sizeof(readback->blob_name));
            readback->blob.name = readback->blob_name;
            readback->blob_data_offset = padding;
            pgraph_vk_input_record_vertex(pg, readback, buffer->buffer,
                                          copy_offset);
        } else {
            status |= PGRAPH_VK_INPUT_MISSING;
        }
    }
    if (inputs) {
        inputs->vertex_status |= status;
    }
    pgraph_vk_input_status(token, "capture.vertices.status", status);
}

static void pgraph_vk_shader_inputs_begin(PGRAPHState *pg, uint64_t token,
                                          uint16_t inline_map,
                                          VkDeviceSize base_offset,
                                          int32_t start, int32_t count,
                                          VkDeviceSize index_offset,
                                          uint32_t index_count)
{
    bool session = xemu_shader_capture_session_token(token);
    if (!token || !xemu_shader_draw_request_wants_inputs(token) ||
        (!session && !xemu_shader_draw_request_has_geometry(token))) {
        return;
    }
    PGRAPHVkState *r = pg->vk_renderer_state;
    pg->shader_browser_input_snapshots++;
    pgraph_vk_input_status(token, "capture.geometry.available",
                           xemu_shader_draw_request_has_geometry(token));
    /* Canceled tokens retain their GPU buffers until this batch's fence. */
    PGRAPHVkShaderInputs *inputs = NULL;
    if (r->shader_browser_input_events < PGRAPH_VK_INPUT_MAX_EVENTS) {
        inputs = g_try_new0(PGRAPHVkShaderInputs, 1);
    }
    size_t budget = PGRAPH_VK_INPUT_BUDGET;
    if (!inputs) {
        pgraph_vk_input_stage_registers(pg, token);
        pgraph_vk_input_stage_pipeline(r, token, &budget);
        pgraph_vk_input_stage_program(r, token, &budget);
        pgraph_vk_input_stage_textures(pg, token, NULL);
        pgraph_vk_input_stage_vertices(pg, token, NULL, inline_map, base_offset,
                                       start, count, index_offset, index_count,
                                       &budget);
        uint32_t status =
            r->shader_browser_input_events >= PGRAPH_VK_INPUT_MAX_EVENTS ?
                PGRAPH_VK_INPUT_LIMIT :
                PGRAPH_VK_INPUT_ALLOCATION;
        pgraph_vk_input_status(token, "capture.before.status", status);
        pgraph_vk_input_status(token, "capture.after.status", status);
        if (session) {
            xemu_shader_capture_session_fail_budget(
                token, "Vulkan capture could not retain another pending event");
        }
        return;
    }
    inputs->token = token;
    inputs->budget = budget;
    inputs->next = r->shader_browser_inputs;
    r->shader_browser_inputs = inputs;
    r->shader_browser_input_events++;
    bool snapshots = !session || xemu_shader_capture_session_snapshots(token);
    uint32_t before_status = 0, after_status = 0;
    PGRAPHVkInputReadback *before = NULL;
    if (snapshots && r->color_binding) {
        SurfaceBinding *color = r->color_binding;
        uint32_t width = color->width, height = color->height;
        pgraph_apply_scaling_factor(pg, &width, &height);
        inputs->color_image = color->image;
        inputs->color_format = color->host_fmt.vk_format;
        before = pgraph_vk_input_allocate(r, inputs, inputs->color_format,
                                          width, height, &before_status);
        inputs->after = pgraph_vk_input_allocate(
            r, inputs, inputs->color_format, width, height, &after_status);
        if (before) {
            before->before = 1;
        }
        if (inputs->after) {
            inputs->after->before = 0;
        }
        pgraph_vk_input_status(token, "capture.before.width", width);
        pgraph_vk_input_status(token, "capture.before.height", height);
        pgraph_vk_input_status(token, "capture.after.width", width);
        pgraph_vk_input_status(token, "capture.after.height", height);
    } else {
        before_status = after_status = PGRAPH_VK_INPUT_MISSING;
    }
    pgraph_vk_input_status(token, "capture.snapshots.requested", snapshots);
    pgraph_vk_input_status(token, "capture.before.status", before_status);
    pgraph_vk_input_status(token, "capture.after.status", after_status);
    pgraph_vk_input_stage_registers(pg, token);
    pgraph_vk_input_stage_pipeline(r, token, &inputs->budget);
    pgraph_vk_input_stage_program(r, token, &inputs->budget);
    bool in_render_pass = r->in_render_pass;
    end_render_pass(r);
    if (before) {
        pgraph_vk_input_record(pg, before, inputs->color_image,
                               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, 0);
    }
    pgraph_vk_input_stage_vertices(pg, token, inputs, inline_map, base_offset,
                                   start, count, index_offset, index_count,
                                   &inputs->budget);
    pgraph_vk_input_stage_textures(pg, token, inputs);
    size_t pending_bytes = 0;
    for (PGRAPHVkInputReadback *readback = inputs->readbacks; readback;
         readback = readback->next) {
        pending_bytes += readback->rgba_bytes + readback->storage_bytes;
    }
    /* Immediate CPU evidence is already staged, so these reservations only
     * cover the payload that still depends on this submission's fence. */
    if (pending_bytes &&
        !xemu_shader_capture_session_reserve(token, pending_bytes)) {
        pgraph_vk_shader_inputs_fail_budget(
            inputs, "Vulkan pending evidence exceeds the recorder CPU budget");
        pgraph_vk_input_status(token, "capture.readback.status",
                               PGRAPH_VK_INPUT_LIMIT);
    }
    if (in_render_pass) {
        begin_render_pass(pg);
    }
}

static void finish_vk_command(PGRAPHState *pg, uint64_t token, bool emitted,
                              uint32_t vertex_count, uint32_t index_count)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    PGRAPHVkShaderInputs *inputs = pgraph_vk_shader_inputs_find(r, token);
    if (emitted) {
        pgraph_vk_capture_record(pg, r->command_buffer, token,
                                 XEMU_SHADER_CAPTURE_COMMAND_DRAW);
        pgraph_vk_capture_draw_replay(pg, token, index_count);
    }
    if (inputs) {
        inputs->emitted = emitted;
        if (inputs->after &&
            (emitted || xemu_shader_capture_session_token(token))) {
            bool in_render_pass = r->in_render_pass;
            end_render_pass(r);
            pgraph_vk_input_record(pg, inputs->after, inputs->color_image,
                                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0,
                                   0);
            if (in_render_pass) {
                begin_render_pass(pg);
            }
        }
    }
    pgraph_shader_browser_capture_finish(pg, token, emitted, vertex_count,
                                         index_count);
    if (!inputs && xemu_shader_draw_request_wants_inputs(token)) {
        xemu_shader_draw_request_inputs_complete(token);
    }
}

#endif

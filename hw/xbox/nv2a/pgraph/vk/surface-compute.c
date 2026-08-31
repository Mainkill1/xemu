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

#include "hw/xbox/nv2a/pgraph/pgraph.h"
#include "qemu/fast-hash.h"
#include "qemu/lru.h"
#include "qemu/error-report.h"
#include "buffer-layout.h"
#include "hw/xbox/nv2a/pgraph/lazy-cache.h"
#include "renderer.h"
#include "surface-compute.h"
#include <vulkan/vulkan_core.h>

// TODO: Swizzle/Unswizzle
// TODO: Float depth format (low priority, but would be better for accuracy)

// FIXME: Below pipeline creation assumes identical 3 buffer setup. For
//        swizzle shader we will need more flexibility.

enum {
    COMPUTE_PIPELINE_CACHE_MAX_ENTRIES = 100,
    COMPUTE_PIPELINE_CACHE_BLOCK_ENTRIES = 16,
};

const char *pack_d24_unorm_s8_uint_to_z24s8_glsl =
    "layout(push_constant) uniform PushConstants { uint width_in, width_out, count_out; };\n"
    "layout(set = 0, binding = 0) buffer DepthIn { uint depth_in[]; };\n"
    "layout(set = 0, binding = 1) buffer StencilIn { uint stencil_in[]; };\n"
    "layout(set = 0, binding = 2) buffer DepthStencilOut { uint depth_stencil_out[]; };\n"
    "uint get_input_idx(uint idx_out) {\n"
    "    uint scale = width_in / width_out;\n"
    "    uint y = (idx_out / width_out) * scale;\n"
    "    uint x = (idx_out % width_out) * scale;\n"
    "    return y * width_in + x;\n"
    "}\n"
    "void main() {\n"
    "    uint idx_out = gl_GlobalInvocationID.x;\n"
    "    uint idx_in = get_input_idx(idx_out);\n"
    "    uint depth_value = depth_in[idx_in];\n"
    "    uint stencil_value = (stencil_in[idx_in / 4] >> ((idx_in % 4) * 8)) & 0xff;\n"
    "    depth_stencil_out[idx_out] = depth_value << 8 | stencil_value;\n"
    "}\n";

const char *unpack_z24s8_to_d24_unorm_s8_uint_glsl =
    "layout(push_constant) uniform PushConstants { uint width_in, width_out, count_out; };\n"
    "layout(set = 0, binding = 0) buffer DepthOut { uint depth_out[]; };\n"
    "layout(set = 0, binding = 1) buffer StencilOut { uint stencil_out[]; };\n"
    "layout(set = 0, binding = 2) buffer DepthStencilIn { uint depth_stencil_in[]; };\n"
    "uint get_input_idx(uint idx_out) {\n"
    "    uint scale = width_out / width_in;\n"
    "    uint y = (idx_out / width_out) / scale;\n"
    "    uint x = (idx_out % width_out) / scale;\n"
    "    return y * width_in + x;\n"
    "}\n"
    "void main() {\n"
    "    uint idx_out = gl_GlobalInvocationID.x;\n"
    "    uint idx_in = get_input_idx(idx_out);\n"
    "    depth_out[idx_out] = depth_stencil_in[idx_in] >> 8;\n"
    "    if (idx_out % 4 == 0) {\n"
    "       uint stencil_value = 0;\n"
    "       for (int i = 0; i < 4; i++) {\n" // Include next 3 pixels
    "           if (idx_out + i < count_out) {\n"
    "               uint v = depth_stencil_in[get_input_idx(idx_out + i)] & 0xff;\n"
    "               stencil_value |= v << (i * 8);\n"
    "           }\n"
    "       }\n"
    "       stencil_out[idx_out / 4] = stencil_value;\n"
    "    }\n"
    "}\n";

const char *pack_d32_sfloat_s8_uint_to_z24s8_glsl =
    "layout(push_constant) uniform PushConstants { uint width_in, width_out, count_out; };\n"
    "layout(set = 0, binding = 0) buffer DepthIn { float depth_in[]; };\n"
    "layout(set = 0, binding = 1) buffer StencilIn { uint stencil_in[]; };\n"
    "layout(set = 0, binding = 2) buffer DepthStencilOut { uint depth_stencil_out[]; };\n"
    "uint get_input_idx(uint idx_out) {\n"
    "    uint scale = width_in / width_out;\n"
    "    uint y = (idx_out / width_out) * scale;\n"
    "    uint x = (idx_out % width_out) * scale;\n"
    "    return y * width_in + x;\n"
    "}\n"
    "void main() {\n"
    "    uint idx_out = gl_GlobalInvocationID.x;\n"
    "    uint idx_in = get_input_idx(idx_out);\n"
    "    uint depth_value = int(depth_in[idx_in] * float(0xffffff));\n"
    "    uint stencil_value = (stencil_in[idx_in / 4] >> ((idx_in % 4) * 8)) & 0xff;\n"
    "    depth_stencil_out[idx_out] = depth_value << 8 | stencil_value;\n"
    "}\n";

const char *unpack_z24s8_to_d32_sfloat_s8_uint_glsl =
    "layout(push_constant) uniform PushConstants { uint width_in, width_out, count_out; };\n"
    "layout(set = 0, binding = 0) buffer DepthOut { float depth_out[]; };\n"
    "layout(set = 0, binding = 1) buffer StencilOut { uint stencil_out[]; };\n"
    "layout(set = 0, binding = 2) buffer DepthStencilIn { uint depth_stencil_in[]; };\n"
    "uint get_input_idx(uint idx_out) {\n"
    "    uint scale = width_out / width_in;\n"
    "    uint y = (idx_out / width_out) / scale;\n"
    "    uint x = (idx_out % width_out) / scale;\n"
    "    return y * width_in + x;\n"
    "}\n"
    "void main() {\n"
    "    uint idx_out = gl_GlobalInvocationID.x;\n"
    "    uint idx_in = get_input_idx(idx_out);\n"
    // Conversion to float depth must be the same as in fragment shader
    "    depth_out[idx_out] = uintBitsToFloat(floatBitsToUint(float(depth_stencil_in[idx_in] >> 8) / 16777216.0) + 1u);\n"
    "    if (idx_out % 4 == 0) {\n"
    "       uint stencil_value = 0;\n"
    "       for (int i = 0; i < 4; i++) {\n" // Include next 3 pixels
    "           if (idx_out + i < count_out) {\n"
    "               uint v = depth_stencil_in[get_input_idx(idx_out + i)] & 0xff;\n"
    "               stencil_value |= v << (i * 8);\n"
    "           }\n"
    "       }\n"
    "       stencil_out[idx_out / 4] = stencil_value;\n"
    "    }\n"
    "}\n";

static gchar *get_compute_shader_glsl(VkFormat host_fmt, bool pack,
                                      int workgroup_size)
{
    const char *template;

    switch (host_fmt) {
    case VK_FORMAT_D24_UNORM_S8_UINT:
        template = pack ? pack_d24_unorm_s8_uint_to_z24s8_glsl :
                          unpack_z24s8_to_d24_unorm_s8_uint_glsl;
        break;
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        template = pack ? pack_d32_sfloat_s8_uint_to_z24s8_glsl :
                          unpack_z24s8_to_d32_sfloat_s8_uint_glsl;
        break;
    default:
        assert(!"Unsupported host fmt");
        break;
    }
    assert(template);

    gchar *glsl = g_strdup_printf(
        "#version 450\n"
        "layout(local_size_x = %d, local_size_y = 1, local_size_z = 1) in;\n"
        "%s", workgroup_size, template);
    assert(glsl);

    return glsl;
}

static void create_descriptor_pool(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    VkDescriptorPoolSize pool_sizes[] = {
        {
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 3 * ARRAY_SIZE(r->compute.descriptor_sets),
        },
    };

    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .poolSizeCount = ARRAY_SIZE(pool_sizes),
        .pPoolSizes = pool_sizes,
        .maxSets = ARRAY_SIZE(r->compute.descriptor_sets),
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
    };
    VK_CHECK(vkCreateDescriptorPool(r->device, &pool_info, NULL,
                                    &r->compute.descriptor_pool));
}

static void destroy_descriptor_pool(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    vkDestroyDescriptorPool(r->device, r->compute.descriptor_pool, NULL);
    r->compute.descriptor_pool = VK_NULL_HANDLE;
}

static void create_descriptor_set_layout(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    const int num_buffers = 3;

    VkDescriptorSetLayoutBinding bindings[num_buffers];
    for (int i = 0; i < num_buffers; i++) {
        bindings[i] = (VkDescriptorSetLayoutBinding){
            .binding = i,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        };
    }
    VkDescriptorSetLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = ARRAY_SIZE(bindings),
        .pBindings = bindings,
    };
    VK_CHECK(vkCreateDescriptorSetLayout(r->device, &layout_info, NULL,
                                         &r->compute.descriptor_set_layout));
}

static void destroy_descriptor_set_layout(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    vkDestroyDescriptorSetLayout(r->device, r->compute.descriptor_set_layout,
                                 NULL);
    r->compute.descriptor_set_layout = VK_NULL_HANDLE;
}

static void create_descriptor_sets(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    VkDescriptorSetLayout layouts[ARRAY_SIZE(r->compute.descriptor_sets)];
    for (int i = 0; i < ARRAY_SIZE(layouts); i++) {
        layouts[i] = r->compute.descriptor_set_layout;
    }
    VkDescriptorSetAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = r->compute.descriptor_pool,
        .descriptorSetCount = ARRAY_SIZE(r->compute.descriptor_sets),
        .pSetLayouts = layouts,
    };
    VK_CHECK(vkAllocateDescriptorSets(r->device, &alloc_info,
                                      r->compute.descriptor_sets));
}

static void destroy_descriptor_sets(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    vkFreeDescriptorSets(r->device, r->compute.descriptor_pool,
                         ARRAY_SIZE(r->compute.descriptor_sets),
                         r->compute.descriptor_sets);
    for (int i = 0; i < ARRAY_SIZE(r->compute.descriptor_sets); i++) {
        r->compute.descriptor_sets[i] = VK_NULL_HANDLE;
    }
}

static void create_compute_pipeline_layout(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    VkPushConstantRange push_constant_range = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .size = 3 * sizeof(uint32_t),
    };
    VkPipelineLayoutCreateInfo pipeline_layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &r->compute.descriptor_set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_constant_range,
    };
    VK_CHECK(vkCreatePipelineLayout(r->device, &pipeline_layout_info, NULL,
                                    &r->compute.pipeline_layout));
}

static void destroy_compute_pipeline_layout(PGRAPHVkState *r)
{
    vkDestroyPipelineLayout(r->device, r->compute.pipeline_layout, NULL);
    r->compute.pipeline_layout = VK_NULL_HANDLE;
}

static VkPipeline create_compute_pipeline(PGRAPHVkState *r, const char *glsl)
{
    ShaderModuleInfo *module = pgraph_vk_create_shader_module_from_glsl(
        r, VK_SHADER_STAGE_COMPUTE_BIT, glsl);

    VkComputePipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .layout = r->compute.pipeline_layout,
        .stage =
            (VkPipelineShaderStageCreateInfo){
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                .pName = "main",
                .module = module->module,
            },
    };
    VkPipeline pipeline;
    VK_CHECK(vkCreateComputePipelines(r->device, r->vk_pipeline_cache, 1,
                                       &pipeline_info, NULL,
                                       &pipeline));

    pgraph_vk_destroy_shader_module(r, module);

    return pipeline;
}

static void update_descriptor_sets(PGRAPHState *pg,
                                   VkDescriptorBufferInfo *buffers, int count)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    assert(count == 3);
    VkWriteDescriptorSet descriptor_writes[3];

    assert(r->compute.descriptor_set_index <
           ARRAY_SIZE(r->compute.descriptor_sets));

    for (int i = 0; i < count; i++) {
        descriptor_writes[i] = (VkWriteDescriptorSet){
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet =
                r->compute.descriptor_sets[r->compute.descriptor_set_index],
            .dstBinding = i,
            .dstArrayElement = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .pBufferInfo = &buffers[i],
        };
    }
    vkUpdateDescriptorSets(r->device, count, descriptor_writes, 0, NULL);

    r->compute.descriptor_set_index += 1;
}

static void compute_pipeline_retire_completed(PGRAPHVkState *r);

static bool compute_pipeline_has_evictable_node(PGRAPHVkState *r)
{
    LruNode *node;

    QTAILQ_FOREACH_REVERSE(node, &r->compute.pipeline_cache.global,
                           next_global) {
        if (!lru_is_node_in_use(&r->compute.pipeline_cache, node)) {
            return true;
        }

        ComputePipeline *pipeline =
            container_of(node, ComputePipeline, node);
        if (!pipeline->active_in_aux_command_buffer &&
            !pipeline->active_in_submission) {
            return true;
        }
    }

    return false;
}

bool pgraph_vk_compute_needs_finish(PGRAPHVkState *r)
{
    compute_pipeline_retire_completed(r);

    bool need_descriptor_write_reset = (r->compute.descriptor_set_index >=
                                        ARRAY_SIZE(r->compute.descriptor_sets));
    if (need_descriptor_write_reset) {
        return true;
    }

    if (r->compute.pipeline_cache.num_free ||
        r->compute.pipeline_cache_num_entries <
            COMPUTE_PIPELINE_CACHE_MAX_ENTRIES) {
        return false;
    }

    return !compute_pipeline_has_evictable_node(r);
}

static void compute_pipeline_retire_node(Lru *lru, LruNode *node, void *opaque)
{
    PGRAPHVkState *r = opaque;
    ComputePipeline *pipeline = container_of(node, ComputePipeline, node);

    if (pipeline->active_in_aux_command_buffer && !r->in_aux_command_buffer) {
        pipeline->active_in_aux_command_buffer = false;
    }

    if (!pipeline->active_in_submission) {
        return;
    }

    bool pending = false;
    if (pipeline->active_submission_slot <
        ARRAY_SIZE(r->submission_slots)) {
        PGRAPHVkSubmissionSlot *slot =
            &r->submission_slots[pipeline->active_submission_slot];
        pending = slot->state.in_flight &&
                  slot->state.submission_serial ==
                      pipeline->active_submission_serial;
    }

    bool recorded_in_active_command_buffer =
        r->in_command_buffer &&
        pipeline->active_submission_slot == r->active_submission_slot &&
        pipeline->active_submission_serial == r->submit_count + 1;

    if (!pending && !recorded_in_active_command_buffer) {
        pipeline->active_in_submission = false;
        pipeline->active_submission_serial = 0;
        pipeline->active_submission_slot = 0;
    }
}

static void compute_pipeline_retire_completed(PGRAPHVkState *r)
{
    lru_visit_active(&r->compute.pipeline_cache,
                     compute_pipeline_retire_node, r);
}

void pgraph_vk_compute_finish_complete(PGRAPHVkState *r)
{
    r->compute.descriptor_set_index = 0;
    compute_pipeline_retire_completed(r);
}

static uint32_t get_workgroup_size_for_output_units(PGRAPHVkState *r,
                                                     uint32_t output_units)
{
    // FIXME: Smarter workgroup size calculation could factor in multiple
    //        submissions. For now we will just pick the highest number that
    //        evenly divides output_units.
    return pgraph_vk_compute_workgroup_size(
        output_units, r->device_props.limits.maxComputeWorkGroupSize[0],
        r->device_props.limits.maxComputeWorkGroupInvocations);
}

static void grow_compute_pipeline_cache(PGRAPHVkState *r, size_t count)
{
    assert(count > 0);
    assert(count <= COMPUTE_PIPELINE_CACHE_MAX_ENTRIES -
                        r->compute.pipeline_cache_num_entries);

    ComputePipeline *entries = g_new0(ComputePipeline, count);
    g_ptr_array_add(r->compute.pipeline_cache_blocks, entries);
    for (size_t i = 0; i < count; i++) {
        lru_add_free(&r->compute.pipeline_cache, &entries[i].node);
    }
    r->compute.pipeline_cache_num_entries += count;
}

static bool compute_pipeline_cache_try_lookup(PGRAPHVkState *r, uint64_t hash,
                                              const void *key,
                                              LruNode **node)
{
    size_t count = pgraph_lazy_cache_growth_for_lookup(
        &r->compute.pipeline_cache, r->compute.pipeline_cache_num_entries,
        COMPUTE_PIPELINE_CACHE_MAX_ENTRIES,
        COMPUTE_PIPELINE_CACHE_BLOCK_ENTRIES, hash, key);
    if (count) {
        grow_compute_pipeline_cache(r, count);
    }
    return lru_try_lookup(&r->compute.pipeline_cache, hash, key,
                          LRU_LOOKUP_ALLOW_EVICT, node);
}

static void compute_pipeline_mark_recorded(PGRAPHVkState *r,
                                           ComputePipeline *pipeline)
{
    if (r->in_aux_command_buffer) {
        pipeline->active_in_aux_command_buffer = true;
    }
    if (r->in_command_buffer) {
        pipeline->active_in_submission = true;
        pipeline->active_submission_slot = r->active_submission_slot;
        pipeline->active_submission_serial = r->submit_count + 1;
    }
}

static ComputePipeline *get_compute_pipeline(PGRAPHState *pg,
                                             VkFormat host_fmt, bool pack,
                                             uint32_t output_units)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    int workgroup_size = get_workgroup_size_for_output_units(r, output_units);

    ComputePipelineKey key;
    memset(&key, 0, sizeof(key));

    key.host_fmt = host_fmt;
    key.pack = pack;
    key.workgroup_size = workgroup_size;

    uint64_t hash = fast_hash((void *)&key, sizeof(key));
    LruNode *node = NULL;
    if (!compute_pipeline_cache_try_lookup(r, hash, &key, &node)) {
        if (!r->in_command_buffer && !r->in_aux_command_buffer) {
            pgraph_vk_finish(pg, VK_FINISH_REASON_NEED_BUFFER_SPACE);
            compute_pipeline_cache_try_lookup(r, hash, &key, &node);
        }
        if (!node) {
            error_report("Vulkan compute pipeline cache exhausted");
            abort();
        }
    }
    ComputePipeline *pipeline = container_of(node, ComputePipeline, node);

    assert(pipeline);

    return pipeline;
}

//
// Pack depth+stencil into NV097_SET_SURFACE_FORMAT_ZETA_Z24S8
// formatted buffer with depth in bits 31-8 and stencil in bits 7-0.
//
void pgraph_vk_pack_depth_stencil(PGRAPHState *pg, SurfaceBinding *surface,
                                  VkCommandBuffer cmd, VkBuffer src,
                                  VkBuffer dst, bool downscale)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    unsigned int input_width = surface->width, input_height = surface->height;
    pgraph_apply_scaling_factor(pg, &input_width, &input_height);

    unsigned int output_width = surface->width, output_height = surface->height;
    if (!downscale) {
        pgraph_apply_scaling_factor(pg, &output_width, &output_height);
    }

    VkDeviceSize depth_size;
    VkDeviceSize stencil_size;
    VkDeviceSize output_size;
    VkDeviceSize output_size_in_units;
    bool valid =
        pgraph_vk_buffer_image_size(input_width, input_height, 4,
                                    &depth_size) &&
        pgraph_vk_buffer_image_size(input_width, input_height, 1,
                                    &stencil_size) &&
        pgraph_vk_buffer_checked_align_up(stencil_size, sizeof(uint32_t),
                                          &stencil_size) &&
        pgraph_vk_buffer_image_size(output_width, output_height, 4,
                                    &output_size) &&
        pgraph_vk_buffer_image_size(output_width, output_height, 1,
                                    &output_size_in_units);
    if (!valid || output_size_in_units > UINT32_MAX) {
        error_report("Vulkan depth/stencil pack buffer size overflow");
        return;
    }
    assert(depth_size <= r->device_props.limits.maxStorageBufferRange);
    assert(stencil_size <= r->device_props.limits.maxStorageBufferRange);
    assert(output_size <= r->device_props.limits.maxStorageBufferRange);

    VkDescriptorBufferInfo buffers[] = {
        {
            .buffer = src,
            .offset = 0,
            .range = depth_size,
        },
        {
            .buffer = src,
            .offset = ROUND_UP(
                depth_size,
                r->device_props.limits.minStorageBufferOffsetAlignment),
            .range = stencil_size,
        },
        {
            .buffer = dst,
            .offset = 0,
            .range = output_size,
        },
    };

    ComputePipeline *pipeline = get_compute_pipeline(
        pg, surface->host_fmt.vk_format, true,
        (uint32_t)output_size_in_units);
    if (!pipeline) {
        return;
    }

    update_descriptor_sets(pg, buffers, ARRAY_SIZE(buffers));

    size_t workgroup_size_in_units = pipeline->key.workgroup_size;
    assert(output_size_in_units % workgroup_size_in_units == 0);
    size_t group_count = output_size_in_units / workgroup_size_in_units;

    assert(r->device_props.limits.maxComputeWorkGroupSize[0] >= workgroup_size_in_units);
    assert(r->device_props.limits.maxComputeWorkGroupCount[0] >= group_count);

    // FIXME: Smarter workgroup scaling

    pgraph_vk_begin_debug_marker(r, cmd, RGBA_PINK, __func__);
    compute_pipeline_mark_recorded(r, pipeline);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
    vkCmdBindDescriptorSets(
        cmd, VK_PIPELINE_BIND_POINT_COMPUTE, r->compute.pipeline_layout, 0, 1,
        &r->compute.descriptor_sets[r->compute.descriptor_set_index - 1], 0,
        NULL);

    uint32_t push_constants[3] = {
        input_width, output_width, (uint32_t)output_size_in_units
    };
    vkCmdPushConstants(cmd, r->compute.pipeline_layout,
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push_constants),
                       push_constants);

    // FIXME: Check max group count

    vkCmdDispatch(cmd, group_count, 1, 1);
    pgraph_vk_end_debug_marker(r, cmd);
}

void pgraph_vk_unpack_depth_stencil(PGRAPHState *pg, SurfaceBinding *surface,
                                    VkCommandBuffer cmd, VkBuffer src,
                                    VkBuffer dst)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    unsigned int input_width = surface->width, input_height = surface->height;

    unsigned int output_width = surface->width, output_height = surface->height;
    pgraph_apply_scaling_factor(pg, &output_width, &output_height);

    VkDeviceSize depth_size;
    VkDeviceSize stencil_size;
    VkDeviceSize input_size;
    VkDeviceSize output_size_in_units;
    bool valid =
        pgraph_vk_buffer_image_size(output_width, output_height, 4,
                                    &depth_size) &&
        pgraph_vk_buffer_image_size(output_width, output_height, 1,
                                    &stencil_size) &&
        pgraph_vk_buffer_checked_align_up(stencil_size, sizeof(uint32_t),
                                          &stencil_size) &&
        pgraph_vk_buffer_image_size(input_width, input_height, 4,
                                    &input_size) &&
        pgraph_vk_buffer_image_size(output_width, output_height, 1,
                                    &output_size_in_units);
    if (!valid || output_size_in_units > UINT32_MAX) {
        error_report("Vulkan depth/stencil unpack buffer size overflow");
        return;
    }
    assert(depth_size <= r->device_props.limits.maxStorageBufferRange);
    assert(stencil_size <= r->device_props.limits.maxStorageBufferRange);
    assert(input_size <= r->device_props.limits.maxStorageBufferRange);

    VkDescriptorBufferInfo buffers[] = {
        {
            .buffer = dst,
            .offset = 0,
            .range = depth_size,
        },
        {
            .buffer = dst,
            .offset = ROUND_UP(
                depth_size,
                r->device_props.limits.minStorageBufferOffsetAlignment),
            .range = stencil_size,
        },
        {
            .buffer = src,
            .offset = 0,
            .range = input_size,
        },
    };
    ComputePipeline *pipeline = get_compute_pipeline(
        pg, surface->host_fmt.vk_format, false,
        (uint32_t)output_size_in_units);
    if (!pipeline) {
        return;
    }

    update_descriptor_sets(pg, buffers, ARRAY_SIZE(buffers));

    size_t workgroup_size_in_units = pipeline->key.workgroup_size;
    assert(output_size_in_units % workgroup_size_in_units == 0);
    size_t group_count = output_size_in_units / workgroup_size_in_units;

    assert(r->device_props.limits.maxComputeWorkGroupSize[0] >= workgroup_size_in_units);
    assert(r->device_props.limits.maxComputeWorkGroupCount[0] >= group_count);

    // FIXME: Smarter workgroup scaling

    pgraph_vk_begin_debug_marker(r, cmd, RGBA_PINK, __func__);
    compute_pipeline_mark_recorded(r, pipeline);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
    vkCmdBindDescriptorSets(
        cmd, VK_PIPELINE_BIND_POINT_COMPUTE, r->compute.pipeline_layout, 0, 1,
        &r->compute.descriptor_sets[r->compute.descriptor_set_index - 1], 0,
        NULL);

    assert(output_width >= input_width);
    uint32_t push_constants[3] = {
        input_width, output_width, (uint32_t)output_size_in_units
    };
    vkCmdPushConstants(cmd, r->compute.pipeline_layout,
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push_constants),
                       push_constants);
    vkCmdDispatch(cmd, group_count, 1, 1);
    pgraph_vk_end_debug_marker(r, cmd);
}

static void pipeline_cache_entry_init(Lru *lru, LruNode *node,
                                      const void *state)
{
    PGRAPHVkState *r = container_of(lru, PGRAPHVkState, compute.pipeline_cache);
    ComputePipeline *snode = container_of(node, ComputePipeline, node);

    memcpy(&snode->key, state, sizeof(snode->key));

    if (snode->key.workgroup_size == 1) {
        fprintf(stderr,
                "Warning: Needed compute shader with workgroup size = 1\n");
    }

    gchar *glsl = get_compute_shader_glsl(
        snode->key.host_fmt, snode->key.pack, snode->key.workgroup_size);
    assert(glsl);
    snode->pipeline = create_compute_pipeline(r, glsl);
    g_free(glsl);
}

static void pipeline_cache_release_node_resources(PGRAPHVkState *r, ComputePipeline *snode)
{
    vkDestroyPipeline(r->device, snode->pipeline, NULL);
    snode->pipeline = VK_NULL_HANDLE;
    snode->active_in_aux_command_buffer = false;
    snode->active_in_submission = false;
    snode->active_submission_slot = 0;
    snode->active_submission_serial = 0;
}

static bool pipeline_cache_entry_pre_evict(Lru *lru, LruNode *node)
{
    ComputePipeline *snode = container_of(node, ComputePipeline, node);

    return !snode->active_in_aux_command_buffer &&
           !snode->active_in_submission;
}

static void pipeline_cache_entry_post_evict(Lru *lru, LruNode *node)
{
    PGRAPHVkState *r = container_of(lru, PGRAPHVkState, compute.pipeline_cache);
    ComputePipeline *snode = container_of(node, ComputePipeline, node);
    pipeline_cache_release_node_resources(r, snode);
}

static bool pipeline_cache_entry_compare(Lru *lru, LruNode *node,
                                         const void *key)
{
    ComputePipeline *snode = container_of(node, ComputePipeline, node);
    return memcmp(&snode->key, key, sizeof(ComputePipelineKey));
}

static void pipeline_cache_init(PGRAPHVkState *r)
{
    lru_init(&r->compute.pipeline_cache);
    r->compute.pipeline_cache_blocks =
        g_ptr_array_new_with_free_func(g_free);
    r->compute.pipeline_cache_num_entries = 0;
    r->compute.pipeline_cache.init_node = pipeline_cache_entry_init;
    r->compute.pipeline_cache.compare_nodes = pipeline_cache_entry_compare;
    r->compute.pipeline_cache.pre_node_evict = pipeline_cache_entry_pre_evict;
    r->compute.pipeline_cache.post_node_evict = pipeline_cache_entry_post_evict;
}

static void pipeline_cache_finalize(PGRAPHVkState *r)
{
    pgraph_vk_compute_finish_complete(r);
    lru_flush(&r->compute.pipeline_cache);
    g_ptr_array_free(r->compute.pipeline_cache_blocks, true);
    r->compute.pipeline_cache_blocks = NULL;
    r->compute.pipeline_cache_num_entries = 0;
}

void pgraph_vk_init_compute(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    create_descriptor_pool(pg);
    create_descriptor_set_layout(pg);
    create_descriptor_sets(pg);
    create_compute_pipeline_layout(pg);
    pipeline_cache_init(r);
}

void pgraph_vk_finalize_compute(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    assert(!r->in_command_buffer);

    pipeline_cache_finalize(r);
    destroy_compute_pipeline_layout(r);
    destroy_descriptor_sets(pg);
    destroy_descriptor_set_layout(pg);
    destroy_descriptor_pool(pg);
}

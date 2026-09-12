/*
 * Geforce NV2A PGRAPH Vulkan Renderer
 *
 * Copyright (c) 2024-2025 Matt Borgerson
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

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/fast-hash.h"
#include "qemu/mstring.h"
#include "hw/xbox/nv2a/pgraph/uniform-stage-update.h"
#include "ui/xemu-settings.h"
#include "ui/xemu-tweaks.h"
#include "device-inventory.h"
#include "renderer.h"

#include <glib/gstdio.h>

#define VSH_UBO_BINDING 0
#define PSH_UBO_BINDING 1
#define PSH_TEX_BINDING 2

/* Bump when shader generation or any fixed glslang input policy changes. */
#define SPIRV_CACHE_GENERATOR_ABI 1U
#define SPIRV_POLICY_VALIDATE             (1U << 0)
#define SPIRV_POLICY_GLSL_460             (1U << 1)
#define SPIRV_POLICY_PROFILE_NONE         (1U << 2)
#define SPIRV_POLICY_MESSAGES_DEFAULT     (1U << 3)
#define SPIRV_POLICY_SPV_RULES            (1U << 4)
#define SPIRV_POLICY_VULKAN_RULES         (1U << 5)
#define SPIRV_POLICY_DEBUG                (1U << 6)
#define SPIRV_POLICY_DISABLE_OPTIMIZER    (1U << 7)
#define SPIRV_POLICY_DEBUG_INFO           (1U << 8)
#define SPIRV_POLICY_NONSEMANTIC_DEBUG    (1U << 9)
#define SPIRV_POLICY_NONSEMANTIC_SOURCE   (1U << 10)

const size_t MAX_UNIFORM_ATTR_VALUES_SIZE = NV2A_VERTEXSHADER_ATTRIBUTES * 4 * sizeof(float);

static inline void sync_uniform_dirty_summary(PGRAPHVkState *r)
{
    r->uniforms_changed =
        r->uniform_stage_dirty[PGRAPH_UNIFORM_STAGE_VSH] ||
        r->uniform_stage_dirty[PGRAPH_UNIFORM_STAGE_PSH];
}

static bool any_dirty_flag_set(const bool *dirty, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (dirty[i]) {
            return true;
        }
    }
    return false;
}

static void get_uniform_stage_update_needs(PGRAPHState *pg,
                                           bool update_stage[])
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    PGRAPHPolygonOffsetUniformKey polygon_offset_key =
        pgraph_polygon_offset_uniform_key(
            pg->primitive_mode, pgraph_reg_r(pg, NV_PGRAPH_SETUPRASTER),
            pgraph_reg_r(pg, NV_PGRAPH_ZOFFSETBIAS),
            pgraph_reg_r(pg, NV_PGRAPH_ZOFFSETFACTOR));
    PGRAPHUniformStageUpdateInputs inputs = {
        .texture_bindings_changed = r->texture_bindings_changed,
        .psh_effective_inputs_changed =
            pgraph_polygon_offset_uniform_key_changed(
                r->polygon_offset_key_valid, r->polygon_offset_key,
                polygon_offset_key),
        .inline_values_in_vsh_ubo =
            pg->uniform_attrs && !r->use_push_constants_for_uniform_attrs,
        .vsh_rows_dirty =
            any_dirty_flag_set(pg->vsh_constants_dirty,
                               NV2A_VERTEXSHADER_CONSTANTS) ||
            any_dirty_flag_set(pg->ltctxa_dirty, NV2A_LTCTXA_COUNT) ||
            any_dirty_flag_set(pg->ltctxb_dirty, NV2A_LTCTXB_COUNT) ||
            any_dirty_flag_set(pg->ltc1_dirty, NV2A_LTC1_COUNT),
        .force_full_update =
            !r->shader_binding ||
            !r->storage_buffers[BUFFER_UNIFORM_STAGING].buffer_offset,
    };

    for (unsigned int stage = 0; stage < PGRAPH_UNIFORM_STAGE_COUNT; stage++) {
        inputs.source_changed[stage] = pgraph_uniform_source_stage_changed(
            &pg->uniform_source_epochs, &r->last_uniform_source_epochs,
            stage);
        inputs.layout_changed[stage] = r->uniform_layout_changed[stage];
    }

    pgraph_uniform_stage_update_needs(&inputs, update_stage);
}

static void create_descriptor_pool(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    size_t num_sets = ARRAY_SIZE(r->descriptor_sets);

    VkDescriptorPoolSize pool_sizes[] = {
        {
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 2 * num_sets,
        },
        {
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = NV2A_MAX_TEXTURES * num_sets,
        },
        {
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            .descriptorCount = num_sets,
        },
    };
    uint32_t pool_size_count = ARRAY_SIZE(pool_sizes) -
                               !r->ubershader_runtime_enabled;

    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .poolSizeCount = pool_size_count,
        .pPoolSizes = pool_sizes,
        .maxSets = ARRAY_SIZE(r->descriptor_sets),
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
    };
    VK_CHECK(vkCreateDescriptorPool(r->device, &pool_info, NULL,
                                    &r->descriptor_pool));
}

static void destroy_descriptor_pool(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    vkDestroyDescriptorPool(r->device, r->descriptor_pool, NULL);
    r->descriptor_pool = VK_NULL_HANDLE;
}

static void create_descriptor_set_layout(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    VkDescriptorSetLayoutBinding bindings[3 + NV2A_MAX_TEXTURES];
    uint32_t binding_count = pgraph_vk_descriptor_layout_binding_count(
        r->ubershader_runtime_enabled);

    bindings[0] = (VkDescriptorSetLayoutBinding){
        .binding = VSH_UBO_BINDING,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
    };
    bindings[1] = (VkDescriptorSetLayoutBinding){
        .binding = PSH_UBO_BINDING,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    for (int i = 0; i < NV2A_MAX_TEXTURES; i++) {
        bindings[2 + i] = (VkDescriptorSetLayoutBinding){
            .binding = PSH_TEX_BINDING + i,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        };
    }
    if (r->ubershader_runtime_enabled) {
        bindings[PGRAPH_VK_PSH_UBER_UBO_BINDING] =
            (VkDescriptorSetLayoutBinding) {
            .binding = PGRAPH_VK_PSH_UBER_UBO_BINDING,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        };
    }
    VkDescriptorSetLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = binding_count,
        .pBindings = bindings,
    };
    VK_CHECK(vkCreateDescriptorSetLayout(r->device, &layout_info, NULL,
                                         &r->descriptor_set_layout));
}

static void destroy_descriptor_set_layout(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    vkDestroyDescriptorSetLayout(r->device, r->descriptor_set_layout, NULL);
    r->descriptor_set_layout = VK_NULL_HANDLE;
}

static void create_descriptor_sets(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    VkDescriptorSetLayout layouts[ARRAY_SIZE(r->descriptor_sets)];
    for (int i = 0; i < ARRAY_SIZE(layouts); i++) {
        layouts[i] = r->descriptor_set_layout;
    }

    VkDescriptorSetAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = r->descriptor_pool,
        .descriptorSetCount = ARRAY_SIZE(r->descriptor_sets),
        .pSetLayouts = layouts,
    };
    VK_CHECK(
        vkAllocateDescriptorSets(r->device, &alloc_info, r->descriptor_sets));
}

static void destroy_descriptor_sets(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    vkFreeDescriptorSets(r->device, r->descriptor_pool,
                         ARRAY_SIZE(r->descriptor_sets), r->descriptor_sets);
    for (int i = 0; i < ARRAY_SIZE(r->descriptor_sets); i++) {
        r->descriptor_sets[i] = VK_NULL_HANDLE;
    }
}

void pgraph_vk_update_descriptor_sets(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    ShaderBinding *binding = r->shader_binding;
    bool force_reupload = r->descriptor_set_index == 0;
    bool uses_uber_controls =
        r->ubershader_runtime_enabled &&
        binding->fragment_route == PGRAPH_VK_FRAGMENT_UBERSHADER;
    assert(!uses_uber_controls || r->uber_controls_valid);
    bool need_uniform_write[PGRAPH_UNIFORM_STAGE_COUNT] = {
        force_reupload || r->uniform_stage_dirty[PGRAPH_UNIFORM_STAGE_VSH],
        force_reupload || r->uniform_stage_dirty[PGRAPH_UNIFORM_STAGE_PSH],
    };
    if (!r->storage_buffers[BUFFER_UNIFORM_STAGING].buffer_offset) {
        need_uniform_write[PGRAPH_UNIFORM_STAGE_VSH] = true;
        need_uniform_write[PGRAPH_UNIFORM_STAGE_PSH] = true;
    }
    bool any_uniform_write =
        need_uniform_write[PGRAPH_UNIFORM_STAGE_VSH] ||
        need_uniform_write[PGRAPH_UNIFORM_STAGE_PSH];
    bool need_uber_control_write =
        uses_uber_controls &&
        (force_reupload || !r->uploaded_uber_controls_valid ||
         memcmp(&r->uploaded_uber_controls, &r->uber_controls,
                sizeof(r->uber_controls)) != 0);
    bool need_descriptor_update = pgraph_vk_descriptor_update_needed(
        r->texture_bindings_changed, force_reupload, any_uniform_write);

    if (!need_descriptor_update && !need_uber_control_write) {
        return; // Nothing changed
    }

    ShaderUniformLayout *layouts[] = { &binding->vsh.module_info->uniforms,
                                       &binding->psh.module_info->uniforms };
    VkDeviceSize required_end =
        r->storage_buffers[BUFFER_UNIFORM_STAGING].buffer_offset;
    VkDeviceSize alignment =
        r->device_props.limits.minUniformBufferOffsetAlignment;
    for (int i = 0; i < ARRAY_SIZE(layouts); i++) {
        if (need_uniform_write[i]) {
            required_end = ROUND_UP(required_end, alignment);
            required_end += layouts[i]->total_size;
        }
    }
    if (need_uber_control_write) {
        required_end = ROUND_UP(required_end, alignment);
        required_end += sizeof(r->uber_controls);
    }
    bool need_ubo_staging_buffer_reset =
        (any_uniform_write || need_uber_control_write) &&
        required_end > r->storage_buffers[BUFFER_UNIFORM_STAGING].buffer_size;

    bool need_descriptor_write_reset = need_descriptor_update &&
        (r->descriptor_set_index >= ARRAY_SIZE(r->descriptor_sets));

    if (need_descriptor_write_reset || need_ubo_staging_buffer_reset) {
        pgraph_vk_finish(pg, VK_FINISH_REASON_NEED_BUFFER_SPACE);
        need_uniform_write[PGRAPH_UNIFORM_STAGE_VSH] = true;
        need_uniform_write[PGRAPH_UNIFORM_STAGE_PSH] = true;
        any_uniform_write = true;
        need_uber_control_write = uses_uber_controls;
        need_descriptor_update = true;
    }

    VkWriteDescriptorSet descriptor_writes[3 + NV2A_MAX_TEXTURES];

    assert(r->descriptor_set_index < ARRAY_SIZE(r->descriptor_sets));

    if (any_uniform_write) {
        for (int i = 0; i < ARRAY_SIZE(layouts); i++) {
            if (!need_uniform_write[i]) {
                continue;
            }
            void *data = layouts[i]->allocation;
            VkDeviceSize size = layouts[i]->total_size;
            r->uniform_buffer_offsets[i] = pgraph_vk_append_to_buffer(
                pg, BUFFER_UNIFORM_STAGING, &data, &size, 1,
                r->device_props.limits.minUniformBufferOffsetAlignment);
            r->uniform_stage_dirty[i] = false;
        }

        sync_uniform_dirty_summary(r);
    }

    if (need_uber_control_write) {
        void *data = &r->uber_controls;
        VkDeviceSize size = sizeof(r->uber_controls);
        r->uber_control_offset = pgraph_vk_append_to_buffer(
            pg, BUFFER_UNIFORM_STAGING, &data, &size, 1,
            r->device_props.limits.minUniformBufferOffsetAlignment);
        memcpy(&r->uploaded_uber_controls, &r->uber_controls,
               sizeof(r->uploaded_uber_controls));
        r->uploaded_uber_controls_valid = true;
        assert(r->uber_control_offset <= UINT32_MAX);
    }

    /* A control-only upload reuses the last descriptor set and its UBO. */
    if (pgraph_vk_reuses_descriptor_set_for_control_update(
            need_uber_control_write, need_descriptor_update)) {
        return;
    }

    VkDescriptorBufferInfo ubo_buffer_infos[3];
    uint32_t descriptor_write_count = 2 + NV2A_MAX_TEXTURES;
    for (int i = 0; i < ARRAY_SIZE(layouts); i++) {
        ubo_buffer_infos[i] = (VkDescriptorBufferInfo){
            .buffer = r->storage_buffers[BUFFER_UNIFORM].buffer,
            .offset = r->uniform_buffer_offsets[i],
            .range = layouts[i]->total_size,
        };
        descriptor_writes[i] = (VkWriteDescriptorSet){
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = r->descriptor_sets[r->descriptor_set_index],
            .dstBinding = i == 0 ? VSH_UBO_BINDING : PSH_UBO_BINDING,
            .dstArrayElement = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .pBufferInfo = &ubo_buffer_infos[i],
        };
    }
    if (r->ubershader_runtime_enabled) {
        ubo_buffer_infos[2] = (VkDescriptorBufferInfo){
            .buffer = r->storage_buffers[BUFFER_UNIFORM].buffer,
            .offset = 0,
            .range = sizeof(r->uber_controls),
        };
        descriptor_writes[descriptor_write_count++] = (VkWriteDescriptorSet){
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = r->descriptor_sets[r->descriptor_set_index],
            .dstBinding = PGRAPH_VK_PSH_UBER_UBO_BINDING,
            .dstArrayElement = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            .descriptorCount = 1,
            .pBufferInfo = &ubo_buffer_infos[2],
        };
    }

    VkDescriptorImageInfo image_infos[NV2A_MAX_TEXTURES];
    for (int i = 0; i < NV2A_MAX_TEXTURES; i++) {
        image_infos[i] = (VkDescriptorImageInfo){
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .imageView = r->texture_bindings[i]->image_view,
            .sampler = r->texture_bindings[i]->sampler,
        };
        descriptor_writes[2 + i] = (VkWriteDescriptorSet){
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = r->descriptor_sets[r->descriptor_set_index],
            .dstBinding = PSH_TEX_BINDING + i,
            .dstArrayElement = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .pImageInfo = &image_infos[i],
        };
    }

    vkUpdateDescriptorSets(r->device, descriptor_write_count,
                           descriptor_writes, 0, NULL);

    r->descriptor_set_index++;
}

static void update_shader_uniform_locs(ShaderBinding *binding)
{
    for (int i = 0; i < ARRAY_SIZE(binding->vsh.uniform_locs); i++) {
        binding->vsh.uniform_locs[i] = uniform_index(
            &binding->vsh.module_info->uniforms, VshUniformInfo[i].name);
    }

    for (int i = 0; i < ARRAY_SIZE(binding->psh.uniform_locs); i++) {
        binding->psh.uniform_locs[i] = uniform_index(
            &binding->psh.module_info->uniforms, PshUniformInfo[i].name);
    }
}

static void init_fragment_module_key(ShaderModuleCacheKey *key,
                                     const PshState *state,
                                     PGRAPHVkFragmentRoute route)
{
    memset(key, 0, sizeof(*key));
    key->kind = VK_SHADER_STAGE_FRAGMENT_BIT;
    key->fragment_route = route;
    key->psh.state = *state;
    if (route == PGRAPH_VK_FRAGMENT_UBERSHADER) {
        pgraph_vk_canonicalize_uber_combiner_state(&key->psh.state);
    }
    key->psh.glsl_opts.vulkan = true;
    key->psh.glsl_opts.ubo_binding = PSH_UBO_BINDING;
    key->psh.glsl_opts.tex_binding = PSH_TEX_BINDING;
    key->psh.glsl_opts.ubershader =
        route == PGRAPH_VK_FRAGMENT_UBERSHADER;
    key->psh.glsl_opts.uber_binding = PGRAPH_VK_PSH_UBER_UBO_BINDING;
}

static ShaderModuleInfo *
get_and_ref_shader_module_for_key(PGRAPHVkState *r,
                                  const ShaderModuleCacheKey *key)
{
    uint64_t hash = fast_hash((void *)key, sizeof(ShaderModuleCacheKey));
    LruNode *node = lru_lookup(&r->shader_module_cache, hash, key);
    ShaderModuleCacheEntry *module =
        container_of(node, ShaderModuleCacheEntry, node);
    pgraph_vk_ref_shader_module(module->module_info);
    return module->module_info;
}

static void shader_cache_entry_init(Lru *lru, LruNode *node, const void *key)
{
    PGRAPHVkState *r = container_of(lru, PGRAPHVkState, shader_cache);
    ShaderBinding *binding = container_of(node, ShaderBinding, node);
    const ShaderBindingKey *binding_key = key;
    binding->state = binding_key->state;
    binding->fragment_route = binding_key->fragment_route;

    NV2A_VK_DPRINTF("cache miss");
    nv2a_profile_inc_counter(NV2A_PROF_SHADER_GEN);

    ShaderModuleCacheKey module_key;

    bool need_geometry_shader = pgraph_glsl_need_geom(&binding->state.geom);
    if (need_geometry_shader) {
        memset(&module_key, 0, sizeof(module_key));
        module_key.kind = VK_SHADER_STAGE_GEOMETRY_BIT;
        module_key.geom.state = binding->state.geom;
        module_key.geom.glsl_opts.vulkan = true;
        binding->geom.module_info =
            get_and_ref_shader_module_for_key(r, &module_key);
    } else {
        binding->geom.module_info = NULL;
    }

    memset(&module_key, 0, sizeof(module_key));
    module_key.kind = VK_SHADER_STAGE_VERTEX_BIT;
    module_key.vsh.state = binding->state.vsh;
    module_key.vsh.glsl_opts.vulkan = true;
    module_key.vsh.glsl_opts.prefix_outputs = need_geometry_shader;
    module_key.vsh.glsl_opts.use_push_constants_for_uniform_attrs =
        r->use_push_constants_for_uniform_attrs;
    module_key.vsh.glsl_opts.ubo_binding = VSH_UBO_BINDING;
    binding->vsh.module_info =
        get_and_ref_shader_module_for_key(r, &module_key);

    init_fragment_module_key(&module_key, &binding->state.psh,
                             binding->fragment_route);
    binding->psh.module_info =
        get_and_ref_shader_module_for_key(r, &module_key);
    assert(binding->psh.module_info->uses_uber_controls ==
           (binding->fragment_route == PGRAPH_VK_FRAGMENT_UBERSHADER));

    update_shader_uniform_locs(binding);
}

static void shader_cache_entry_post_evict(Lru *lru, LruNode *node)
{
    PGRAPHVkState *r = container_of(lru, PGRAPHVkState, shader_cache);
    ShaderBinding *snode = container_of(node, ShaderBinding, node);

    ShaderModuleInfo *modules[] = {
        snode->vsh.module_info,
        snode->geom.module_info,
        snode->psh.module_info,
    };
    for (int i = 0; i < ARRAY_SIZE(modules); i++) {
        if (modules[i]) {
            pgraph_vk_unref_shader_module(r, modules[i]);
        }
    }
}

static bool shader_cache_entry_compare(Lru *lru, LruNode *node, const void *key)
{
    ShaderBinding *snode = container_of(node, ShaderBinding, node);
    const ShaderBindingKey *binding_key = key;

    ShaderBindingKey snode_key = {
        .state = snode->state,
        .fragment_route = snode->fragment_route,
    };

    return pgraph_vk_shader_binding_key_different(&snode_key, binding_key);
}

static uint32_t shader_spirv_compiler_policy(void)
{
    uint32_t policy = SPIRV_POLICY_VALIDATE | SPIRV_POLICY_GLSL_460 |
                      SPIRV_POLICY_PROFILE_NONE |
                      SPIRV_POLICY_MESSAGES_DEFAULT |
                      SPIRV_POLICY_SPV_RULES | SPIRV_POLICY_VULKAN_RULES;
    if (g_config.display.vulkan.debug_shaders) {
        policy |= SPIRV_POLICY_DEBUG | SPIRV_POLICY_DISABLE_OPTIMIZER |
                  SPIRV_POLICY_DEBUG_INFO |
                  SPIRV_POLICY_NONSEMANTIC_DEBUG |
                  SPIRV_POLICY_NONSEMANTIC_SOURCE;
    }
    return policy;
}

static const char *shader_spirv_cache_filename(uint32_t api_version,
                                               bool debug_shaders)
{
    switch (pgraph_vk_shader_target_for_api(api_version)) {
    case PGRAPH_VK_SHADER_TARGET_VULKAN_1_3:
        return debug_shaders ? "spirv-v1-vk13-spv16-debug.bin" :
                               "spirv-v1-vk13-spv16.bin";
    case PGRAPH_VK_SHADER_TARGET_VULKAN_1_2:
        return debug_shaders ? "spirv-v1-vk12-spv15-debug.bin" :
                               "spirv-v1-vk12-spv15.bin";
    default:
        return debug_shaders ? "spirv-v1-vk11-spv13-debug.bin" :
                               "spirv-v1-vk11-spv13.bin";
    }
}

static void shader_spirv_cache_init(PGRAPHVkState *r)
{
    const char *base = xemu_settings_get_base_path();
    if (!base || !base[0]) {
        return;
    }
    glslang_version_t compiler_version;
    glslang_get_version(&compiler_version);
    glslang_target_client_version_t client_target;
    glslang_target_language_version_t spirv_target;
    pgraph_vk_glsl_target_versions(r->vk_api_version, &client_target,
                                   &spirv_target);
    const char *flavor = compiler_version.flavor && compiler_version.flavor[0] ?
                             compiler_version.flavor : "unknown";
    PGRAPHVkSpirvCachePolicy policy = {
        .generator_abi = SPIRV_CACHE_GENERATOR_ABI,
        .compiler_major = compiler_version.major,
        .compiler_minor = compiler_version.minor,
        .compiler_patch = compiler_version.patch,
        .client_target = client_target,
        .spirv_target = spirv_target,
        .compiler_flags = shader_spirv_compiler_policy(),
        .compiler_flavor = flavor,
        .compiler_flavor_size = strlen(flavor),
    };
    if (!pgraph_vk_spirv_cache_init(&r->spirv_cache, &policy)) {
        return;
    }

    r->spirv_cache_directory =
        g_build_filename(base, "cache", "vulkan", NULL);
    r->spirv_cache_path =
        g_build_filename(r->spirv_cache_directory,
                         shader_spirv_cache_filename(
                             r->vk_api_version,
                             g_config.display.vulkan.debug_shaders),
                         NULL);
    r->spirv_cache_initialized = true;
    r->spirv_cache_session_eligible = g_config.perf.cache_shaders;
    qemu_event_init(&r->spirv_cache_writeback_complete, false);
    r->spirv_cache_writeback_complete_initialized = true;

    if (!r->spirv_cache_session_eligible) {
        return;
    }

    GStatBuf stat_buf;
    if (g_stat(r->spirv_cache_path, &stat_buf)) {
        if (errno != ENOENT) {
            pgraph_vk_spirv_cache_note_rejection(&r->spirv_cache);
        }
        return;
    }
    if (stat_buf.st_size <= 0 ||
        (uint64_t)stat_buf.st_size > PGRAPH_VK_SPIRV_CACHE_MAX_FILE_SIZE) {
        pgraph_vk_spirv_cache_note_rejection(&r->spirv_cache);
        return;
    }
    size_t contents_size = (size_t)stat_buf.st_size;
    uint8_t *contents = g_try_malloc(contents_size);
    FILE *file = qemu_fopen(r->spirv_cache_path, "rb");
    if (!contents || !file ||
        fread(contents, 1, contents_size, file) != contents_size ||
        fgetc(file) != EOF) {
        pgraph_vk_spirv_cache_note_rejection(&r->spirv_cache);
        if (file) {
            fclose(file);
        }
        g_free(contents);
        return;
    }
    fclose(file);
    pgraph_vk_spirv_cache_load(&r->spirv_cache,
                               contents, contents_size);
    g_free(contents);
}

static bool shader_spirv_write(void *opaque, const char *path,
                               const uint8_t *data, size_t size)
{
    (void)opaque;
    return g_file_set_contents(path, (const char *)data, (gssize)size, NULL);
}

static bool shader_spirv_replace(void *opaque, const char *temporary,
                                 const char *published)
{
    (void)opaque;
    return g_rename(temporary, published) == 0;
}

static void shader_spirv_remove(void *opaque, const char *path)
{
    (void)opaque;
    g_unlink(path);
}

void pgraph_vk_process_spirv_cache_writeback(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    bool was_dirty = pgraph_vk_spirv_cache_is_dirty(&r->spirv_cache);
    bool active = r->spirv_cache_initialized &&
                  pgraph_vk_spirv_cache_is_active(
                      &r->spirv_cache, r->spirv_cache_session_eligible,
                      g_config.perf.cache_shaders);
    bool written = !was_dirty || !active;

    if (active && was_dirty &&
        !g_mkdir_with_parents(r->spirv_cache_directory, 0700)) {
        char *temporary = g_strdup_printf("%s.tmp.%08x",
                                          r->spirv_cache_path,
                                          g_random_int());
        const PGRAPHVkSpirvCacheFileOps ops = {
            .write = shader_spirv_write,
            .replace = shader_spirv_replace,
            .remove = shader_spirv_remove,
        };
        written = pgraph_vk_spirv_cache_publish(
            &r->spirv_cache, temporary, r->spirv_cache_path, &ops, NULL);
        g_free(temporary);
    }
    if (active && was_dirty && !written) {
        pgraph_vk_spirv_cache_note_rejection(&r->spirv_cache);
    }

    const PGRAPHVkSpirvCacheStats *stats =
        pgraph_vk_spirv_cache_stats(&r->spirv_cache);
    fprintf(stderr,
            "nv2a/vk: SPIR-V prewarm hits=%" PRIu64
            " misses=%" PRIu64 " rejections=%" PRIu64
            " fallbacks=%" PRIu64 " records=%zu source_bytes=%zu"
            " spirv_bytes=%zu loaded_bytes=%" PRIu64
            " queued_bytes=%" PRIu64 " write=%s\n",
            stats->hits, stats->misses, stats->rejections,
            stats->fallbacks, stats->records, stats->source_bytes,
            stats->spirv_bytes, stats->loaded_bytes, stats->queued_bytes,
            !active ? "disabled" :
            (written ? (was_dirty ? "published" : "clean") : "failed"));
}

static void shader_spirv_cache_finalize(PGRAPHVkState *r)
{
    if (r->spirv_cache_writeback_complete_initialized) {
        qatomic_set(&r->spirv_cache_writeback_pending, false);
        r->spirv_cache_writeback_requested = false;
        qemu_event_destroy(&r->spirv_cache_writeback_complete);
        r->spirv_cache_writeback_complete_initialized = false;
    }
    pgraph_vk_spirv_cache_destroy(&r->spirv_cache);
    g_free(r->spirv_cache_directory);
    g_free(r->spirv_cache_path);
    r->spirv_cache_directory = NULL;
    r->spirv_cache_path = NULL;
    r->spirv_cache_initialized = false;
    r->spirv_cache_session_eligible = false;
}

static bool shader_spirv_cache_active(PGRAPHVkState *r)
{
    return r->spirv_cache_initialized &&
           pgraph_vk_spirv_cache_is_active(
               &r->spirv_cache, r->spirv_cache_session_eligible,
               g_config.perf.cache_shaders);
}

static bool hybrid_compile_job(
    void *opaque, const PGRAPHVkHybridCompileRequest *request,
    uint8_t **spirv_data, size_t *spirv_size)
{
    (void)opaque;

    if (!request || !spirv_data || !spirv_size ||
        request->config_size != sizeof(PGRAPHVkGlslCompileConfig) ||
        !request->config || !request->glsl || request->glsl_size < 2 ||
        ((const char *)request->glsl)[request->glsl_size - 1] != '\0') {
        return false;
    }

    const PGRAPHVkGlslCompileConfig *config = request->config;
    GByteArray *spirv = pgraph_vk_compile_glsl_to_spv_config(
        config, request->stage, request->glsl);
    if (!spirv) {
        return false;
    }

    *spirv_size = spirv->len;
    *spirv_data = g_byte_array_free(spirv, false);
    return *spirv_data && *spirv_size;
}

static void hybrid_work_clear(PGRAPHVkHybridShaderWork *work)
{
    g_free(work->glsl);
    memset(work, 0, sizeof(*work));
}

static PGRAPHVkHybridShaderWork *hybrid_find_work_by_source(
    PGRAPHVkState *r, uint32_t stage, const char *glsl, size_t glsl_size)
{
    for (size_t i = 0; i < ARRAY_SIZE(r->hybrid_work); i++) {
        PGRAPHVkHybridShaderWork *work = &r->hybrid_work[i];
        if (work->in_use && pgraph_vk_hybrid_source_matches(
                                work->module_key.kind, work->glsl,
                                work->glsl_size, stage, glsl, glsl_size)) {
            return work;
        }
    }
    return NULL;
}

static PGRAPHVkHybridShaderWork *hybrid_find_work_by_key(
    PGRAPHVkState *r, const ShaderModuleCacheKey *key)
{
    for (size_t i = 0; i < ARRAY_SIZE(r->hybrid_work); i++) {
        PGRAPHVkHybridShaderWork *work = &r->hybrid_work[i];
        if (work->in_use && pgraph_vk_hybrid_key_matches(
                                &work->module_key, sizeof(work->module_key),
                                key, sizeof(*key))) {
            return work;
        }
    }
    return NULL;
}

static PGRAPHVkHybridShaderWork *hybrid_find_work_by_completion(
    PGRAPHVkState *r, uint64_t generation, uint64_t ticket)
{
    for (size_t i = 0; i < ARRAY_SIZE(r->hybrid_work); i++) {
        PGRAPHVkHybridShaderWork *work = &r->hybrid_work[i];
        if (work->in_use && work->metadata.generation == generation &&
            work->metadata.ticket == ticket) {
            return work;
        }
    }
    return NULL;
}

static PGRAPHVkHybridShaderWork *hybrid_allocate_work(PGRAPHVkState *r)
{
    PGRAPHVkHybridShaderWork *oldest = NULL;

    for (size_t i = 0; i < ARRAY_SIZE(r->hybrid_work); i++) {
        PGRAPHVkHybridShaderWork *work = &r->hybrid_work[i];
        if (!work->in_use) {
            return work;
        }
        if (work->metadata.status != PGRAPH_VK_HYBRID_WORK_PENDING &&
            (!oldest || work->last_epoch < oldest->last_epoch)) {
            oldest = work;
        }
    }
    if (oldest) {
        hybrid_work_clear(oldest);
    }
    return oldest;
}

static ShaderModuleCacheEntry *find_shader_module_for_key(
    PGRAPHVkState *r, const ShaderModuleCacheKey *key)
{
    uint64_t hash = fast_hash((void *)key, sizeof(*key));
    LruNode *node = lru_find_existing(&r->shader_module_cache, hash, key);

    return node ? container_of(node, ShaderModuleCacheEntry, node) : NULL;
}

static ShaderBinding *find_shader_binding_for_key(
    PGRAPHVkState *r, const ShaderBindingKey *key)
{
    uint64_t hash = fast_hash((void *)key, sizeof(*key));
    LruNode *node = lru_find_existing(&r->shader_cache, hash, key);

    return node ? container_of(node, ShaderBinding, node) : NULL;
}

void pgraph_vk_process_hybrid_completions(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    PGRAPHVkHybridCompileResult result;

    if (!r->hybrid_compiler_initialized ||
        !pgraph_vk_hybrid_compiler_has_result(&r->hybrid_compiler)) {
        return;
    }

    while (pgraph_vk_hybrid_compiler_take_result(&r->hybrid_compiler,
                                                  &result)) {
        PGRAPHVkHybridShaderWork *work = hybrid_find_work_by_completion(
            r, result.generation, result.ticket);
        if (!work || result.stage != GLSLANG_STAGE_FRAGMENT ||
            pgraph_vk_hybrid_validate_completion_metadata(
                work ? &work->metadata : NULL, result.generation,
                result.ticket, r->hybrid_generation) !=
                PGRAPH_VK_HYBRID_COMPLETION_METADATA_MATCH) {
            pgraph_vk_hybrid_compile_result_destroy(&result);
            continue;
        }
        assert(r->hybrid_pending_jobs > 0);
        r->hybrid_pending_jobs--;

        bool published = result.success && result.spirv && result.spirv_size;
        if (published) {
            GByteArray *spirv = g_byte_array_new_take(result.spirv,
                                                       result.spirv_size);
            result.spirv = NULL;
            result.spirv_size = 0;
            r->hybrid_materializing_key = &work->module_key;
            r->hybrid_materializing_glsl = work->glsl;
            r->hybrid_materializing_glsl_size = work->glsl_size;
            r->hybrid_materializing_spirv = spirv;
            uint64_t hash = fast_hash(&work->module_key,
                                      sizeof(work->module_key));
            LruNode *node = lru_lookup(&r->shader_module_cache, hash,
                                       &work->module_key);
            ShaderModuleCacheEntry *entry = container_of(
                node, ShaderModuleCacheEntry, node);
            published = entry->module_info != NULL;
            r->hybrid_materializing_key = NULL;
            r->hybrid_materializing_glsl = NULL;
            r->hybrid_materializing_glsl_size = 0;
            r->hybrid_materializing_spirv = NULL;
            g_byte_array_unref(spirv);
        }
        if (published) {
            r->hybrid_selection_epoch = pgraph_vk_hybrid_next_selection_epoch(
                r->hybrid_selection_epoch);
            hybrid_work_clear(work);
        } else {
            pgraph_vk_hybrid_note_compile_failure(
                &work->metadata, result.generation, result.ticket,
                r->hybrid_generation, r->hybrid_route_epoch, 32);
            work->last_epoch = r->hybrid_route_epoch;
        }
        pgraph_vk_hybrid_compile_result_destroy(&result);
    }
}

static void shader_module_cache_entry_init(Lru *lru, LruNode *node,
                                           const void *key)
{
    PGRAPHVkState *r = container_of(lru, PGRAPHVkState, shader_module_cache);
    ShaderModuleCacheEntry *module =
        container_of(node, ShaderModuleCacheEntry, node);
    memcpy(&module->key, key, sizeof(ShaderModuleCacheKey));

    MString *code;

    switch (module->key.kind) {
    case VK_SHADER_STAGE_VERTEX_BIT:
        code = pgraph_glsl_gen_vsh(&module->key.vsh.state,
                                   module->key.vsh.glsl_opts);
        break;
    case VK_SHADER_STAGE_GEOMETRY_BIT:
        code = pgraph_glsl_gen_geom(&module->key.geom.state,
                                    module->key.geom.glsl_opts);
        break;
    case VK_SHADER_STAGE_FRAGMENT_BIT:
        code = pgraph_glsl_gen_psh(&module->key.psh.state,
                                   module->key.psh.glsl_opts);
        break;
    default:
        assert(!"Invalid shader module kind");
        code = NULL;
    }

    const char *glsl = mstring_get_str(code);
    size_t glsl_size = 0;
    bool glsl_size_known = false;
    ShaderModuleInfo *module_info = NULL;
    if (r->hybrid_materializing_key && r->hybrid_materializing_spirv &&
        memcmp(r->hybrid_materializing_key, &module->key,
               sizeof(module->key)) == 0) {
        module_info = pgraph_vk_create_shader_module_from_spirv(
            r, module->key.kind, r->hybrid_materializing_glsl,
            r->hybrid_materializing_spirv);
        if (module_info && shader_spirv_cache_active(r)) {
            glsl_size = r->hybrid_materializing_glsl_size;
            glsl_size_known = true;
            pgraph_vk_spirv_cache_add(
                &r->spirv_cache, module->key.kind,
                r->hybrid_materializing_glsl, glsl_size,
                module_info->spirv->data, module_info->spirv->len);
        }
    }
    if (!module_info && shader_spirv_cache_active(r)) {
        glsl_size = strlen(glsl);
        glsl_size_known = true;
        const uint8_t *cached_spirv = NULL;
        size_t cached_spirv_size = 0;
        if (pgraph_vk_spirv_cache_lookup(
                &r->spirv_cache, module->key.kind, glsl, glsl_size,
                &cached_spirv, &cached_spirv_size) ==
            PGRAPH_VK_SPIRV_CACHE_HIT) {
            GByteArray *spirv = g_byte_array_sized_new(cached_spirv_size);
            g_byte_array_append(spirv, cached_spirv, cached_spirv_size);
            module_info = pgraph_vk_create_shader_module_from_spirv(
                r, module->key.kind, glsl, spirv);
            g_byte_array_unref(spirv);
            if (!module_info) {
                pgraph_vk_spirv_cache_reject_hit(
                    &r->spirv_cache, module->key.kind, glsl, glsl_size);
            }
        }
    }
    if (!module_info) {
        module_info = pgraph_vk_create_shader_module_from_glsl(
            r, module->key.kind, glsl);
        if (module_info && shader_spirv_cache_active(r)) {
            if (!glsl_size_known) {
                glsl_size = strlen(glsl);
            }
            pgraph_vk_spirv_cache_add(
                &r->spirv_cache, module->key.kind, glsl, glsl_size,
                module_info->spirv->data, module_info->spirv->len);
        }
    }
    if (!module_info) {
        mstring_unref(code);
        error_report("nv2a/vk: failed to construct generated shader module");
        abort();
    }
    module->module_info = module_info;
    pgraph_vk_ref_shader_module(module->module_info);
    mstring_unref(code);
}

static void shader_module_cache_entry_post_evict(Lru *lru, LruNode *node)
{
    PGRAPHVkState *r = container_of(lru, PGRAPHVkState, shader_module_cache);
    ShaderModuleCacheEntry *module =
        container_of(node, ShaderModuleCacheEntry, node);
    pgraph_vk_unref_shader_module(r, module->module_info);
    module->module_info = NULL;
}

static bool shader_module_cache_entry_compare(Lru *lru, LruNode *node,
                                              const void *key)
{
    ShaderModuleCacheEntry *module =
        container_of(node, ShaderModuleCacheEntry, node);
    return memcmp(&module->key, key, sizeof(ShaderModuleCacheKey));
}

static void shader_cache_init(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    const size_t shader_cache_size = 1024;
    lru_init(&r->shader_cache);
    r->shader_cache_entries = g_malloc_n(shader_cache_size, sizeof(ShaderBinding));
    assert(r->shader_cache_entries != NULL);
    for (int i = 0; i < shader_cache_size; i++) {
        lru_add_free(&r->shader_cache, &r->shader_cache_entries[i].node);
    }
    r->shader_cache.init_node = shader_cache_entry_init;
    r->shader_cache.compare_nodes = shader_cache_entry_compare;
    r->shader_cache.post_node_evict = shader_cache_entry_post_evict;

    /* FIXME: Make this configurable */
    const size_t shader_module_cache_size = 50 * 1024;
    lru_init(&r->shader_module_cache);
    r->shader_module_cache_entries =
        g_malloc_n(shader_module_cache_size, sizeof(ShaderModuleCacheEntry));
    assert(r->shader_module_cache_entries != NULL);
    for (int i = 0; i < shader_module_cache_size; i++) {
        lru_add_free(&r->shader_module_cache,
                     &r->shader_module_cache_entries[i].node);
    }

    r->shader_module_cache.init_node = shader_module_cache_entry_init;
    r->shader_module_cache.compare_nodes = shader_module_cache_entry_compare;
    r->shader_module_cache.post_node_evict =
        shader_module_cache_entry_post_evict;

    shader_spirv_cache_init(r);
}

static void shader_cache_finalize(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    lru_flush(&r->shader_cache);
    g_free(r->shader_cache_entries);
    r->shader_cache_entries = NULL;

    lru_flush(&r->shader_module_cache);
    g_free(r->shader_module_cache_entries);
    r->shader_module_cache_entries = NULL;
    if (shader_spirv_cache_active(r) &&
        pgraph_vk_spirv_cache_is_dirty(&r->spirv_cache)) {
        pgraph_vk_process_spirv_cache_writeback(pg);
    }
    shader_spirv_cache_finalize(r);
}

static ShaderBinding *get_shader_binding_for_key(PGRAPHVkState *r,
                                                 const ShaderBindingKey *key)
{
    uint64_t hash = fast_hash((void *)key, sizeof(*key));
    LruNode *node = lru_lookup(&r->shader_cache, hash, key);
    ShaderBinding *binding = container_of(node, ShaderBinding, node);
    NV2A_VK_DPRINTF("shader state hash: %016" PRIx64 " %p", hash, binding);
    return binding;
}

static void get_uber_control_source(PGRAPHState *pg, const PshState *state,
                                    PGRAPHUberControlSource *source)
{
    memset(source, 0, sizeof(*source));
    source->combiner_control = state->combiner_control;
    memcpy(source->rgb_inputs, state->rgb_inputs, sizeof(source->rgb_inputs));
    memcpy(source->alpha_inputs, state->alpha_inputs,
           sizeof(source->alpha_inputs));
    memcpy(source->rgb_outputs, state->rgb_outputs,
           sizeof(source->rgb_outputs));
    memcpy(source->alpha_outputs, state->alpha_outputs,
           sizeof(source->alpha_outputs));
    source->final_inputs_0 = state->final_inputs_0;
    source->final_inputs_1 = state->final_inputs_1;

    pgraph_glsl_get_psh_combiner_constants(pg, source->constants);
}

static bool update_uber_controls(PGRAPHState *pg, const PshState *state)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    PGRAPHUberControlSource source;

    get_uber_control_source(pg, state, &source);
    r->uber_controls_valid = pgraph_vk_pack_ubershader_controls(
        &r->uber_controls, &source, NULL);
    return r->uber_controls_valid;
}

static PGRAPHVkFragmentRoute select_fragment_route(PGRAPHState *pg,
                                                    const ShaderState *state)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    ShaderModuleCacheKey module_key;
    PGRAPHVkHybridShaderWork *work;
    MString *code;
    const char *glsl;
    size_t glsl_size;

    r->uber_controls_valid = false;
    if (!r->ubershader_runtime_enabled ||
        !r->hybrid_compiler_initialized) {
        return PGRAPH_VK_FRAGMENT_SPECIALIZED;
    }

    if (!update_uber_controls(pg, &state->psh)) {
        return PGRAPH_VK_FRAGMENT_SPECIALIZED;
    }

    ShaderBindingKey binding_key = {
        .state = *state,
        .fragment_route = PGRAPH_VK_FRAGMENT_SPECIALIZED,
    };
    if (find_shader_binding_for_key(r, &binding_key)) {
        return PGRAPH_VK_FRAGMENT_SPECIALIZED;
    }
    init_fragment_module_key(&module_key, &state->psh,
                             PGRAPH_VK_FRAGMENT_SPECIALIZED);
    if (find_shader_module_for_key(r, &module_key)) {
        return PGRAPH_VK_FRAGMENT_SPECIALIZED;
    }

    r->hybrid_route_epoch = pgraph_vk_hybrid_next_selection_epoch(
        r->hybrid_route_epoch);
    work = hybrid_find_work_by_key(r, &module_key);
    if (work) {
        PGRAPHVkHybridRouteInput fast_input = {
            .matching_status = work->metadata.status,
            .epoch = r->hybrid_route_epoch,
            .retry_after_epoch = work->metadata.retry_after_epoch,
            .attempts = work->metadata.attempts,
            .max_attempts = work->metadata.max_attempts,
            .fallback_pipeline_ready = true,
            .fallback_draw_resources_ready = true,
            .queue_has_capacity = true,
        };
        PGRAPHVkHybridDecision fast_decision =
            pgraph_vk_hybrid_choose(&fast_input);
        if (!fast_decision.request_specialization) {
            work->last_epoch = r->hybrid_route_epoch;
            return PGRAPH_VK_FRAGMENT_UBERSHADER;
        }
    }

    code = pgraph_glsl_gen_psh(&module_key.psh.state,
                               module_key.psh.glsl_opts);
    glsl = mstring_get_str(code);
    glsl_size = strlen(glsl);

    const uint8_t *cached_spirv;
    size_t cached_spirv_size;
    if (shader_spirv_cache_active(r) &&
        pgraph_vk_spirv_cache_lookup(
            &r->spirv_cache, module_key.kind, glsl, glsl_size,
            &cached_spirv, &cached_spirv_size) ==
        PGRAPH_VK_SPIRV_CACHE_HIT) {
        uint64_t hash = fast_hash(&module_key, sizeof(module_key));
        lru_lookup(&r->shader_module_cache, hash, &module_key);
        mstring_unref(code);
        return PGRAPH_VK_FRAGMENT_SPECIALIZED;
    }

    work = hybrid_find_work_by_source(
        r, module_key.kind, glsl, glsl_size);
    PGRAPHVkHybridRouteInput input = {
        .matching_status = work ? work->metadata.status :
                                  PGRAPH_VK_HYBRID_WORK_ABSENT,
        .epoch = r->hybrid_route_epoch,
        .retry_after_epoch = work ? work->metadata.retry_after_epoch : 0,
        .attempts = work ? work->metadata.attempts : 0,
        .max_attempts = work ? work->metadata.max_attempts : 3,
        /* The existing draw path completes these synchronously if absent. */
        .fallback_pipeline_ready = true,
        .fallback_draw_resources_ready = true,
        .queue_has_capacity = pgraph_vk_hybrid_compiler_can_submit_async(
            &r->hybrid_compiler, glsl_size + 1,
            sizeof(PGRAPHVkGlslCompileConfig)),
    };
    PGRAPHVkHybridDecision decision = pgraph_vk_hybrid_choose(&input);

    if (decision.request_specialization) {
        if (!work) {
            work = hybrid_allocate_work(r);
            if (work) {
                work->in_use = true;
                work->module_key = module_key;
                work->glsl = g_strndup(glsl, glsl_size);
                work->glsl_size = glsl_size;
                pgraph_vk_hybrid_work_init(&work->metadata, 3);
            }
        }
        if (work) {
            PGRAPHVkGlslCompileConfig config = {
                .api_version = r->vk_api_version,
                .debug_shaders = g_config.display.vulkan.debug_shaders,
            };
            uint64_t ticket = pgraph_vk_hybrid_allocate_ticket(
                &r->hybrid_ticket_allocator);
            PGRAPHVkHybridCompileRequest request = {
                .generation = r->hybrid_generation,
                .ticket = ticket,
                .stage = GLSLANG_STAGE_FRAGMENT,
                .glsl = glsl,
                /* Worker owns the NUL; cache identity excludes it. */
                .glsl_size = glsl_size + 1,
                .config = &config,
                .config_size = sizeof(config),
            };
            PGRAPHVkHybridCompileIdentity owner = { 0 };
            PGRAPHVkHybridCompilerSubmitResult submit =
                ticket ? pgraph_vk_hybrid_compiler_submit_async(
                             &r->hybrid_compiler, &request, &owner) :
                         PGRAPH_VK_HYBRID_COMPILER_STOPPED;
            if (submit == PGRAPH_VK_HYBRID_COMPILER_ACCEPTED) {
                bool marked = pgraph_vk_hybrid_mark_pending(
                    &work->metadata, false, owner.generation, owner.ticket,
                    r->hybrid_route_epoch);
                assert(marked);
                r->hybrid_pending_jobs++;
            } else if (submit == PGRAPH_VK_HYBRID_COMPILER_QUEUE_FULL ||
                       submit == PGRAPH_VK_HYBRID_COMPILER_BYTE_LIMIT) {
                pgraph_vk_hybrid_note_queue_deferral(
                    &work->metadata, false, r->hybrid_generation,
                    r->hybrid_route_epoch, 8);
            } else {
                /* Source work is deduplicated before submission. */
                hybrid_work_clear(work);
                decision.route = PGRAPH_VK_HYBRID_USE_SYNCHRONOUS;
            }
            if (work->in_use) {
                work->last_epoch = r->hybrid_route_epoch;
            }
        }
    }
    mstring_unref(code);

    return decision.route == PGRAPH_VK_HYBRID_USE_SYNCHRONOUS ?
               PGRAPH_VK_FRAGMENT_SPECIALIZED :
               PGRAPH_VK_FRAGMENT_UBERSHADER;
}

static bool apply_uniform_updates(ShaderUniformLayout *layout,
                                  const UniformInfo *info, int *locs,
                                  void *values, size_t count)
{
    bool changed = false;

    for (int i = 0; i < count; i++) {
        if (locs[i] != -1) {
            changed |= uniform_copy(layout, locs[i],
                                    (char *)values + info[i].val_offs, 4,
                                    (info[i].size * info[i].count) / 4);
        }
    }

    return changed;
}

static bool update_uniform_rows(ShaderUniformLayout *layout, int loc,
                                uint32_t values[][4], bool dirty[],
                                unsigned int row_count, bool full_update)
{
    /*
     * Every source-array writer marks its row dirty. A binding change still
     * needs a full copy because the selected layout may hold another binding's
     * previous values.
     */
    if (loc == -1) {
        memset(dirty, 0, row_count * sizeof(*dirty));
        return false;
    }

    if (full_update) {
        bool changed = uniform_copy(layout, loc, values, sizeof(uint32_t),
                                    row_count * 4);
        memset(dirty, 0, row_count * sizeof(*dirty));
        return changed;
    }

    bool changed = false;
    for (unsigned int row = 0; row < row_count; row++) {
        if (dirty[row]) {
            changed |= uniform_copy_array_element(
                layout, loc, row, values[row], sizeof(uint32_t));
            dirty[row] = false;
        }
    }
    return changed;
}

static void update_shader_uniforms(PGRAPHState *pg, const bool update_stage[])
{
    NV2A_VK_DGROUP_BEGIN("%s", __func__);

    PGRAPHVkState *r = pg->vk_renderer_state;
    nv2a_profile_inc_counter(NV2A_PROF_SHADER_BIND);

    assert(r->shader_binding);
    ShaderBinding *binding = r->shader_binding;
    bool vsh_layout_changed =
        r->uniform_layout_changed[PGRAPH_UNIFORM_STAGE_VSH];
    bool psh_layout_changed =
        r->uniform_layout_changed[PGRAPH_UNIFORM_STAGE_PSH];
    bool vsh_changed = false;
    bool psh_changed = false;

    if (update_stage[PGRAPH_UNIFORM_STAGE_VSH]) {
        VshUniformValues vsh_values;
        VshUniformLocs vsh_uniform_locs;
        memcpy(vsh_uniform_locs, binding->vsh.uniform_locs,
               sizeof(vsh_uniform_locs));
        vsh_uniform_locs[VshUniform_c] = -1;
        vsh_uniform_locs[VshUniform_ltctxa] = -1;
        vsh_uniform_locs[VshUniform_ltctxb] = -1;
        vsh_uniform_locs[VshUniform_ltc1] = -1;
        pgraph_glsl_set_vsh_uniform_values(pg, &binding->state.vsh,
                                           vsh_uniform_locs, &vsh_values);
        vsh_changed = apply_uniform_updates(
            &binding->vsh.module_info->uniforms, VshUniformInfo,
            vsh_uniform_locs, &vsh_values, VshUniform__COUNT);
        ShaderUniformLayout *vsh_layout = &binding->vsh.module_info->uniforms;

        /* A layout change needs a full copy from every source array. */
        vsh_changed |= update_uniform_rows(
            vsh_layout, binding->vsh.uniform_locs[VshUniform_c],
            pg->vsh_constants, pg->vsh_constants_dirty,
            NV2A_VERTEXSHADER_CONSTANTS, vsh_layout_changed);
        vsh_changed |= update_uniform_rows(
            vsh_layout, binding->vsh.uniform_locs[VshUniform_ltctxa],
            pg->ltctxa, pg->ltctxa_dirty, NV2A_LTCTXA_COUNT,
            vsh_layout_changed);
        vsh_changed |= update_uniform_rows(
            vsh_layout, binding->vsh.uniform_locs[VshUniform_ltctxb],
            pg->ltctxb, pg->ltctxb_dirty, NV2A_LTCTXB_COUNT,
            vsh_layout_changed);
        vsh_changed |= update_uniform_rows(
            vsh_layout, binding->vsh.uniform_locs[VshUniform_ltc1], pg->ltc1,
            pg->ltc1_dirty, NV2A_LTC1_COUNT, vsh_layout_changed);

        r->last_uniform_source_epochs.stage[PGRAPH_UNIFORM_STAGE_VSH] =
            pg->uniform_source_epochs.stage[PGRAPH_UNIFORM_STAGE_VSH];
    }

    if (update_stage[PGRAPH_UNIFORM_STAGE_PSH]) {
        PshUniformValues psh_values;
        pgraph_glsl_set_psh_uniform_values(pg, binding->psh.uniform_locs,
                                           &psh_values);
        for (int i = 0; i < 4; i++) {
            assert(r->texture_bindings[i] != NULL);
            float scale = r->texture_bindings[i]->key.scale;

            BasicColorFormatInfo f_basic = kelvin_color_format_info_map[
                r->texture_bindings[i]->key.state.color_format];
            if (!f_basic.linear) {
                scale = 1.0;
            }

            psh_values.texScale[i] = scale;
        }

        psh_changed = apply_uniform_updates(
            &binding->psh.module_info->uniforms, PshUniformInfo,
            binding->psh.uniform_locs, &psh_values, PshUniform__COUNT);

        r->last_uniform_source_epochs.stage[PGRAPH_UNIFORM_STAGE_PSH] =
            pg->uniform_source_epochs.stage[PGRAPH_UNIFORM_STAGE_PSH];
    }

    r->uniform_stage_dirty[PGRAPH_UNIFORM_STAGE_VSH] |=
        vsh_changed || vsh_layout_changed;
    r->uniform_stage_dirty[PGRAPH_UNIFORM_STAGE_PSH] |=
        psh_changed || psh_layout_changed;
    sync_uniform_dirty_summary(r);

    nv2a_profile_inc_counter(r->uniforms_changed ?
                                 NV2A_PROF_SHADER_UBO_DIRTY :
                                 NV2A_PROF_SHADER_UBO_NOTDIRTY);

    NV2A_VK_DGROUP_END();
}

void pgraph_vk_bind_shaders(PGRAPHState *pg)
{
    NV2A_VK_DGROUP_BEGIN("%s", __func__);

    PGRAPHVkState *r = pg->vk_renderer_state;

    pgraph_vk_process_hybrid_completions(pg);

    r->shader_bindings_changed = false;
    r->uniform_layout_changed[PGRAPH_UNIFORM_STAGE_VSH] = false;
    r->uniform_layout_changed[PGRAPH_UNIFORM_STAGE_PSH] = false;

    bool selection_changed = pgraph_vk_hybrid_selection_changed(
        r->hybrid_bound_selection_epoch, r->hybrid_selection_epoch);
    bool shader_state_dirty = selection_changed ||
        !r->shader_binding ||
        pgraph_glsl_check_shader_state_dirty(pg, &r->shader_binding->state);
    ShaderState new_state;
    if (shader_state_dirty) {
        new_state = pgraph_glsl_get_shader_state(pg);
    } else {
        new_state = r->shader_binding->state;
    }
    PGRAPHVkFragmentRoute fragment_route;
    if (!shader_state_dirty &&
        r->shader_binding->fragment_route ==
            PGRAPH_VK_FRAGMENT_SPECIALIZED) {
        fragment_route = PGRAPH_VK_FRAGMENT_SPECIALIZED;
    } else {
        fragment_route = select_fragment_route(pg, &new_state);
    }
    r->hybrid_bound_selection_epoch = r->hybrid_selection_epoch;

    if (shader_state_dirty ||
        r->shader_binding->fragment_route != fragment_route) {
        ShaderBinding *old_binding = r->shader_binding;
        if (!old_binding ||
            old_binding->fragment_route != fragment_route ||
            memcmp(&old_binding->state, &new_state, sizeof(ShaderState))) {
            ShaderBindingKey key = {
                .state = new_state,
                .fragment_route = fragment_route,
            };
            r->shader_binding = get_shader_binding_for_key(r, &key);
            r->shader_bindings_changed = true;
            r->uniform_layout_changed[PGRAPH_UNIFORM_STAGE_VSH] =
                !old_binding ||
                old_binding->vsh.module_info !=
                    r->shader_binding->vsh.module_info;
            r->uniform_layout_changed[PGRAPH_UNIFORM_STAGE_PSH] =
                !old_binding ||
                old_binding->psh.module_info !=
                    r->shader_binding->psh.module_info;
        }
    } else {
        nv2a_profile_inc_counter(NV2A_PROF_SHADER_BIND_NOTDIRTY);
    }

    bool update_stage[PGRAPH_UNIFORM_STAGE_COUNT];
    get_uniform_stage_update_needs(pg, update_stage);
    r->last_uniform_source_epochs.unclassified =
        pg->uniform_source_epochs.unclassified;
    r->last_uniform_source_epochs.total = pg->uniform_source_epochs.total;

    if (!update_stage[PGRAPH_UNIFORM_STAGE_VSH] &&
        !update_stage[PGRAPH_UNIFORM_STAGE_PSH]) {
        nv2a_profile_inc_counter(NV2A_PROF_SHADER_UBO_NOTDIRTY);
        NV2A_VK_DGROUP_END();
        return;
    }

    update_shader_uniforms(pg, update_stage);
    if (update_stage[PGRAPH_UNIFORM_STAGE_PSH]) {
        r->polygon_offset_key = pgraph_polygon_offset_uniform_key(
            pg->primitive_mode, pgraph_reg_r(pg, NV_PGRAPH_SETUPRASTER),
            pgraph_reg_r(pg, NV_PGRAPH_ZOFFSETBIAS),
            pgraph_reg_r(pg, NV_PGRAPH_ZOFFSETFACTOR));
        r->polygon_offset_key_valid = true;
    }

    NV2A_VK_DGROUP_END();
}

void pgraph_vk_init_shaders(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    r->ubershader_runtime_enabled =
        xemu_tweak_enabled(XEMU_TWEAK_VK_HYBRID_UBERSHADERS);
    pgraph_vk_init_glsl_compiler();
    create_descriptor_pool(pg);
    create_descriptor_set_layout(pg);
    create_descriptor_sets(pg);
    shader_cache_init(pg);

    r->hybrid_generation = 1;
    r->hybrid_selection_epoch = 1;
    if (r->ubershader_runtime_enabled) {
        PGRAPHVkHybridCompilerConfig compiler_config = {
            .max_async_jobs = 32,
            .max_async_bytes = 8 * MiB,
            .compile = hybrid_compile_job,
        };
        r->hybrid_compiler_initialized = pgraph_vk_hybrid_compiler_init(
            &r->hybrid_compiler, &compiler_config);
        if (!r->hybrid_compiler_initialized) {
            error_report("nv2a/vk: failed to start hybrid shader compiler; "
                         "using synchronous specialization");
        }
    }

    r->use_push_constants_for_uniform_attrs =
        (r->device_props.limits.maxPushConstantsSize >=
         MAX_UNIFORM_ATTR_VALUES_SIZE);
}

void pgraph_vk_stop_hybrid_compiler(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    if (r->hybrid_compiler_initialized) {
        pgraph_vk_hybrid_compiler_stop(&r->hybrid_compiler);
        pgraph_vk_hybrid_compiler_join(&r->hybrid_compiler);
        pgraph_vk_hybrid_compiler_destroy(&r->hybrid_compiler);
        r->hybrid_compiler_initialized = false;
    }
    r->hybrid_generation = pgraph_vk_hybrid_next_selection_epoch(
        r->hybrid_generation);
    r->hybrid_pending_jobs = 0;
    for (size_t i = 0; i < ARRAY_SIZE(r->hybrid_work); i++) {
        hybrid_work_clear(&r->hybrid_work[i]);
    }
}

void pgraph_vk_finalize_shaders(PGRAPHState *pg)
{
    pgraph_vk_stop_hybrid_compiler(pg);
    shader_cache_finalize(pg);
    destroy_descriptor_sets(pg);
    destroy_descriptor_set_layout(pg);
    destroy_descriptor_pool(pg);
    pgraph_vk_finalize_glsl_compiler();
}

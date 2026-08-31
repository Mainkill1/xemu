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
#include "ui/xemu-settings.h"
#include "qemu/error-report.h"
#include "xemu-version.h"
#include "renderer.h"
#include "shader-cache-file.h"

#include <assert.h>
#include <glib/gstdio.h>
#include <glslang/Include/glslang_c_interface.h>
#include <stdio.h>

#define SHADER_CACHE_DIRECTORY "vulkan-shader-cache"
#define SHADER_CACHE_FILE_PREFIX "shader-"
#define SHADER_CACHE_FILE_SUFFIX ".bin"

typedef struct ShaderCacheDiskEntry {
    char *path;
    uint64_t size;
    int64_t mtime;
} ShaderCacheDiskEntry;

typedef struct ShaderCacheDiskState {
    char *directory;
    GQueue entries;
    uint64_t total_size;
    bool writable;
} ShaderCacheDiskState;

static ShaderCacheDiskState shader_cache_disk;

static void shader_cache_disk_entry_free(gpointer data)
{
    ShaderCacheDiskEntry *entry = data;
    g_free(entry->path);
    g_free(entry);
}

static gint shader_cache_disk_entry_compare(gconstpointer a,
                                            gconstpointer b,
                                            gpointer user_data)
{
    const ShaderCacheDiskEntry *entry_a = a;
    const ShaderCacheDiskEntry *entry_b = b;

    if (entry_a->mtime < entry_b->mtime) {
        return -1;
    }
    if (entry_a->mtime > entry_b->mtime) {
        return 1;
    }
    return strcmp(entry_a->path, entry_b->path);
}

static ShaderCacheDiskEntry *shader_cache_disk_find(const char *path,
                                                     GList **link_out)
{
    for (GList *link = shader_cache_disk.entries.head; link;
         link = link->next) {
        ShaderCacheDiskEntry *entry = link->data;
        if (!strcmp(entry->path, path)) {
            if (link_out) {
                *link_out = link;
            }
            return entry;
        }
    }
    return NULL;
}

static void shader_cache_disk_forget_link(GList *link)
{
    ShaderCacheDiskEntry *entry = link->data;
    assert(entry->size <= shader_cache_disk.total_size);
    shader_cache_disk.total_size -= entry->size;
    g_queue_delete_link(&shader_cache_disk.entries, link);
    shader_cache_disk_entry_free(entry);
}

static bool shader_cache_disk_evict_one(const char *exclude_path)
{
    for (GList *link = shader_cache_disk.entries.head; link;
         link = link->next) {
        ShaderCacheDiskEntry *entry = link->data;
        if (exclude_path && !strcmp(entry->path, exclude_path)) {
            continue;
        }
        if (qemu_unlink(entry->path) != 0 && errno != ENOENT) {
            shader_cache_disk.writable = false;
            return false;
        }
        shader_cache_disk_forget_link(link);
        return true;
    }
    return false;
}

static void shader_cache_disk_forget_unlinked(const char *path)
{
    GList *link = NULL;
    if (shader_cache_disk_find(path, &link)) {
        shader_cache_disk_forget_link(link);
    }
}

static bool shader_cache_disk_make_room(const char *path, uint64_t new_size)
{
    ShaderCacheDiskEntry *existing = shader_cache_disk_find(path, NULL);

    while (!pgraph_vk_shader_cache_budget_allows(
        shader_cache_disk.entries.length, shader_cache_disk.total_size,
        existing != NULL, existing ? existing->size : 0, new_size)) {
        if (!shader_cache_disk_evict_one(path)) {
            return false;
        }
    }
    return shader_cache_disk.writable;
}

static void shader_cache_disk_init(void)
{
    g_queue_init(&shader_cache_disk.entries);
    shader_cache_disk.total_size = 0;
    shader_cache_disk.writable = false;

    if (!g_config.perf.cache_shaders) {
        return;
    }

    shader_cache_disk.directory = g_build_filename(
        xemu_settings_get_base_path(), SHADER_CACHE_DIRECTORY, NULL);
    if (g_mkdir_with_parents(shader_cache_disk.directory, 0700) != 0) {
        warn_report("Unable to create Vulkan shader cache directory");
        return;
    }
    shader_cache_disk.writable = true;

    g_autoptr(GError) error = NULL;
    g_autoptr(GDir) directory =
        g_dir_open(shader_cache_disk.directory, 0, &error);
    if (!directory) {
        warn_report("Unable to scan Vulkan shader cache: %s",
                    error->message);
        shader_cache_disk.writable = false;
        return;
    }

    const uint64_t max_file_size = sizeof(PGRAPHVkShaderCacheFileHeader) +
        PGRAPH_VK_SHADER_CACHE_MAX_SOURCE_SIZE +
        PGRAPH_VK_SHADER_CACHE_MAX_SPIRV_SIZE;
    const char *name;
    while ((name = g_dir_read_name(directory))) {
        if (!shader_cache_disk.writable) {
            break;
        }
        if (!g_str_has_prefix(name, SHADER_CACHE_FILE_PREFIX) ||
            !g_str_has_suffix(name, SHADER_CACHE_FILE_SUFFIX)) {
            continue;
        }

        g_autofree char *path =
            g_build_filename(shader_cache_disk.directory, name, NULL);
        GStatBuf stat_buffer;
        if (g_lstat(path, &stat_buffer) != 0 ||
            !S_ISREG(stat_buffer.st_mode) || stat_buffer.st_size < 0 ||
            (uint64_t)stat_buffer.st_size > max_file_size) {
            qemu_unlink(path);
            continue;
        }

        ShaderCacheDiskEntry *entry = g_new0(ShaderCacheDiskEntry, 1);
        entry->path = g_strdup(path);
        entry->size = stat_buffer.st_size;
        entry->mtime = stat_buffer.st_mtime;
        if (shader_cache_disk.total_size > UINT64_MAX - entry->size) {
            shader_cache_disk_entry_free(entry);
            qemu_unlink(path);
            continue;
        }
        shader_cache_disk.total_size += entry->size;
        g_queue_insert_sorted(&shader_cache_disk.entries, entry,
                              shader_cache_disk_entry_compare, NULL);

        while ((shader_cache_disk.entries.length >
                    PGRAPH_VK_SHADER_CACHE_MAX_FILES ||
                shader_cache_disk.total_size >
                    PGRAPH_VK_SHADER_CACHE_MAX_TOTAL_SIZE) &&
               shader_cache_disk_evict_one(NULL)) {
        }
    }
}

static void shader_cache_disk_finalize(void)
{
    g_queue_clear_full(&shader_cache_disk.entries,
                       shader_cache_disk_entry_free);
    g_clear_pointer(&shader_cache_disk.directory, g_free);
    shader_cache_disk.total_size = 0;
    shader_cache_disk.writable = false;
}

static const glslang_resource_t
    resource_limits = { .max_lights = 32,
                        .max_clip_planes = 6,
                        .max_texture_units = 32,
                        .max_texture_coords = 32,
                        .max_vertex_attribs = 64,
                        .max_vertex_uniform_components = 4096,
                        .max_varying_floats = 64,
                        .max_vertex_texture_image_units = 32,
                        .max_combined_texture_image_units = 80,
                        .max_texture_image_units = 32,
                        .max_fragment_uniform_components = 4096,
                        .max_draw_buffers = 32,
                        .max_vertex_uniform_vectors = 128,
                        .max_varying_vectors = 8,
                        .max_fragment_uniform_vectors = 16,
                        .max_vertex_output_vectors = 16,
                        .max_fragment_input_vectors = 15,
                        .min_program_texel_offset = -8,
                        .max_program_texel_offset = 7,
                        .max_clip_distances = 8,
                        .max_compute_work_group_count_x = 65535,
                        .max_compute_work_group_count_y = 65535,
                        .max_compute_work_group_count_z = 65535,
                        .max_compute_work_group_size_x = 1024,
                        .max_compute_work_group_size_y = 1024,
                        .max_compute_work_group_size_z = 64,
                        .max_compute_uniform_components = 1024,
                        .max_compute_texture_image_units = 16,
                        .max_compute_image_uniforms = 8,
                        .max_compute_atomic_counters = 8,
                        .max_compute_atomic_counter_buffers = 1,
                        .max_varying_components = 60,
                        .max_vertex_output_components = 64,
                        .max_geometry_input_components = 64,
                        .max_geometry_output_components = 128,
                        .max_fragment_input_components = 128,
                        .max_image_units = 8,
                        .max_combined_image_units_and_fragment_outputs = 8,
                        .max_combined_shader_output_resources = 8,
                        .max_image_samples = 0,
                        .max_vertex_image_uniforms = 0,
                        .max_tess_control_image_uniforms = 0,
                        .max_tess_evaluation_image_uniforms = 0,
                        .max_geometry_image_uniforms = 0,
                        .max_fragment_image_uniforms = 8,
                        .max_combined_image_uniforms = 8,
                        .max_geometry_texture_image_units = 16,
                        .max_geometry_output_vertices = 256,
                        .max_geometry_total_output_components = 1024,
                        .max_geometry_uniform_components = 1024,
                        .max_geometry_varying_components = 64,
                        .max_tess_control_input_components = 128,
                        .max_tess_control_output_components = 128,
                        .max_tess_control_texture_image_units = 16,
                        .max_tess_control_uniform_components = 1024,
                        .max_tess_control_total_output_components = 4096,
                        .max_tess_evaluation_input_components = 128,
                        .max_tess_evaluation_output_components = 128,
                        .max_tess_evaluation_texture_image_units = 16,
                        .max_tess_evaluation_uniform_components = 1024,
                        .max_tess_patch_components = 120,
                        .max_patch_vertices = 32,
                        .max_tess_gen_level = 64,
                        .max_viewports = 16,
                        .max_vertex_atomic_counters = 0,
                        .max_tess_control_atomic_counters = 0,
                        .max_tess_evaluation_atomic_counters = 0,
                        .max_geometry_atomic_counters = 0,
                        .max_fragment_atomic_counters = 8,
                        .max_combined_atomic_counters = 8,
                        .max_atomic_counter_bindings = 1,
                        .max_vertex_atomic_counter_buffers = 0,
                        .max_tess_control_atomic_counter_buffers = 0,
                        .max_tess_evaluation_atomic_counter_buffers = 0,
                        .max_geometry_atomic_counter_buffers = 0,
                        .max_fragment_atomic_counter_buffers = 1,
                        .max_combined_atomic_counter_buffers = 1,
                        .max_atomic_counter_buffer_size = 16384,
                        .max_transform_feedback_buffers = 4,
                        .max_transform_feedback_interleaved_components = 64,
                        .max_cull_distances = 8,
                        .max_combined_clip_and_cull_distances = 8,
                        .max_samples = 4,
                        .max_mesh_output_vertices_nv = 256,
                        .max_mesh_output_primitives_nv = 512,
                        .max_mesh_work_group_size_x_nv = 32,
                        .max_mesh_work_group_size_y_nv = 1,
                        .max_mesh_work_group_size_z_nv = 1,
                        .max_task_work_group_size_x_nv = 32,
                        .max_task_work_group_size_y_nv = 1,
                        .max_task_work_group_size_z_nv = 1,
                        .max_mesh_view_count_nv = 4,
                        .maxDualSourceDrawBuffersEXT = 1,
                        .limits = {
                            .non_inductive_for_loops = 1,
                            .while_loops = 1,
                            .do_while_loops = 1,
                            .general_uniform_indexing = 1,
                            .general_attribute_matrix_vector_indexing = 1,
                            .general_varying_indexing = 1,
                            .general_sampler_indexing = 1,
                            .general_variable_indexing = 1,
                            .general_constant_matrix_vector_indexing = 1,
                        } };

static PGRAPHVkShaderCacheIdentity shader_cache_identity(
    glslang_stage_t stage)
{
    glslang_version_t compiler_version;
    glslang_get_version(&compiler_version);

    uint64_t build_hash = PGRAPH_VK_SHADER_CACHE_HASH_OFFSET;
    build_hash = pgraph_vk_shader_cache_hash_update(
        build_hash, xemu_version, strlen(xemu_version) + 1);
    build_hash = pgraph_vk_shader_cache_hash_update(
        build_hash, xemu_commit, strlen(xemu_commit) + 1);

    PGRAPHVkShaderCacheIdentity identity = {
        .stage = stage,
        .flags = PGRAPH_VK_SHADER_CACHE_FLAG_VALIDATE |
                 (g_config.display.vulkan.debug_shaders ?
                      PGRAPH_VK_SHADER_CACHE_FLAG_DEBUG : 0),
        .client = GLSLANG_CLIENT_VULKAN,
        .client_version = GLSLANG_TARGET_VULKAN_1_3,
        .target_language = GLSLANG_TARGET_SPV,
        .target_language_version = GLSLANG_TARGET_SPV_1_6,
        .default_version = 460,
        .default_profile = GLSLANG_NO_PROFILE,
        .messages = GLSLANG_MSG_DEFAULT_BIT,
        .glslang_major = compiler_version.major,
        .glslang_minor = compiler_version.minor,
        .glslang_patch = compiler_version.patch,
        .glslang_flavor_hash = pgraph_vk_shader_cache_hash(
            compiler_version.flavor ? compiler_version.flavor : "",
            compiler_version.flavor ? strlen(compiler_version.flavor) : 0),
        .resource_hash = pgraph_vk_shader_cache_hash(
            &resource_limits, sizeof(resource_limits)),
        .build_hash = build_hash,
    };
    return identity;
}

static char *shader_cache_path(
    const PGRAPHVkShaderCacheIdentity *identity, const char *source,
    size_t source_size)
{
    uint64_t key_hash = pgraph_vk_shader_cache_key_hash(
        identity, source, source_size);
    g_autofree char *filename = g_strdup_printf(
        SHADER_CACHE_FILE_PREFIX "%08x-%016" PRIx64 SHADER_CACHE_FILE_SUFFIX,
        identity->stage, key_hash);
    return g_build_filename(shader_cache_disk.directory, filename, NULL);
}

static void shader_cache_discard_file(const char *path)
{
    if (qemu_unlink(path) == 0 || errno == ENOENT) {
        shader_cache_disk_forget_unlinked(path);
    }
}

static GByteArray *shader_cache_load(
    const PGRAPHVkShaderCacheIdentity *identity, const char *source,
    size_t source_size)
{
    if (!g_config.perf.cache_shaders || !shader_cache_disk.directory ||
        !source_size || source_size > PGRAPH_VK_SHADER_CACHE_MAX_SOURCE_SIZE) {
        return NULL;
    }

    g_autofree char *path = shader_cache_path(identity, source, source_size);
    GStatBuf stat_buffer;
    const uint64_t max_file_size = sizeof(PGRAPHVkShaderCacheFileHeader) +
        PGRAPH_VK_SHADER_CACHE_MAX_SOURCE_SIZE +
        PGRAPH_VK_SHADER_CACHE_MAX_SPIRV_SIZE;
    if (g_lstat(path, &stat_buffer) != 0) {
        return NULL;
    }
    if (!S_ISREG(stat_buffer.st_mode) || stat_buffer.st_size < 0 ||
        (uint64_t)stat_buffer.st_size > max_file_size ||
        stat_buffer.st_size < sizeof(PGRAPHVkShaderCacheFileHeader)) {
        shader_cache_discard_file(path);
        return NULL;
    }

    size_t file_size = stat_buffer.st_size;
    g_autofree uint8_t *file_data = g_malloc(file_size);
    FILE *file = qemu_fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    bool read_complete = fread(file_data, 1, file_size, file) == file_size &&
                         fgetc(file) == EOF && !ferror(file);
    fclose(file);

    const uint8_t *spirv = NULL;
    size_t spirv_size = 0;
    if (!read_complete || !pgraph_vk_shader_cache_file_validate(
                              file_data, file_size, identity, source,
                              source_size, &spirv, &spirv_size)) {
        warn_report("Ignoring invalid Vulkan shader artifact");
        shader_cache_discard_file(path);
        return NULL;
    }

    SpvReflectShaderModule probe;
    SpvReflectResult reflect_result =
        spvReflectCreateShaderModule(spirv_size, spirv, &probe);
    if (reflect_result != SPV_REFLECT_RESULT_SUCCESS) {
        warn_report("Ignoring malformed Vulkan shader artifact");
        shader_cache_discard_file(path);
        return NULL;
    }
    spvReflectDestroyShaderModule(&probe);
    return g_byte_array_new_take(g_memdup2(spirv, spirv_size), spirv_size);
}

static void shader_cache_save(
    const PGRAPHVkShaderCacheIdentity *identity, const char *source,
    size_t source_size, const GByteArray *spirv)
{
    if (!g_config.perf.cache_shaders || !shader_cache_disk.directory ||
        !shader_cache_disk.writable) {
        return;
    }

    PGRAPHVkShaderCacheFileHeader header;
    if (!pgraph_vk_shader_cache_header_init(
            &header, identity, source, source_size, spirv->data, spirv->len)) {
        return;
    }
    size_t file_size = sizeof(header) + source_size + spirv->len;
    g_autofree uint8_t *file_data = g_malloc(file_size);
    memcpy(file_data, &header, sizeof(header));
    memcpy(file_data + sizeof(header), source, source_size);
    memcpy(file_data + sizeof(header) + source_size,
           spirv->data, spirv->len);
    if (!pgraph_vk_shader_cache_file_validate(
            file_data, file_size, identity, source, source_size, NULL, NULL)) {
        return;
    }

    g_autofree char *path = shader_cache_path(identity, source, source_size);
    if (!shader_cache_disk_make_room(path, file_size)) {
        return;
    }

    g_autoptr(GError) error = NULL;
    if (!g_file_set_contents_full(
            path, (const char *)file_data, file_size,
            G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE,
            0600, &error)) {
        warn_report("Failed to save Vulkan shader artifact: %s",
                    error->message);
        return;
    }

    GList *old_link = NULL;
    if (shader_cache_disk_find(path, &old_link)) {
        shader_cache_disk_forget_link(old_link);
    }
    ShaderCacheDiskEntry *entry = g_new0(ShaderCacheDiskEntry, 1);
    entry->path = g_strdup(path);
    entry->size = file_size;
    GStatBuf stat_buffer;
    if (g_lstat(path, &stat_buffer) == 0 &&
        S_ISREG(stat_buffer.st_mode)) {
        entry->mtime = stat_buffer.st_mtime;
    }
    assert(shader_cache_disk.total_size <= UINT64_MAX - entry->size);
    shader_cache_disk.total_size += entry->size;
    g_queue_insert_sorted(&shader_cache_disk.entries, entry,
                          shader_cache_disk_entry_compare, NULL);
}

void pgraph_vk_init_glsl_compiler(void)
{
    glslang_initialize_process();
    shader_cache_disk_init();
}

void pgraph_vk_finalize_glsl_compiler(void)
{
    shader_cache_disk_finalize();
    glslang_finalize_process();
}

GByteArray *pgraph_vk_compile_glsl_to_spv(glslang_stage_t stage,
                                          const char *glsl_source)
{
    size_t source_size = strlen(glsl_source);
    PGRAPHVkShaderCacheIdentity cache_identity =
        shader_cache_identity(stage);
    GByteArray *cached = shader_cache_load(
        &cache_identity, glsl_source, source_size);
    if (cached) {
        return cached;
    }

    const glslang_input_t input = {
        .language = GLSLANG_SOURCE_GLSL,
        .stage = stage,
        .client = GLSLANG_CLIENT_VULKAN,
        .client_version = GLSLANG_TARGET_VULKAN_1_3,
        .target_language = GLSLANG_TARGET_SPV,
        .target_language_version = GLSLANG_TARGET_SPV_1_6,
        .code = glsl_source,
        .default_version = 460,
        .default_profile = GLSLANG_NO_PROFILE,
        .force_default_version_and_profile = false,
        .forward_compatible = false,
        .messages = GLSLANG_MSG_DEFAULT_BIT,
        .resource = &resource_limits,
    };

    glslang_shader_t *shader = glslang_shader_create(&input);

    if (!glslang_shader_preprocess(shader, &input)) {
        fprintf(stderr,
                "GLSL preprocessing failed\n"
                "[INFO]: %s\n"
                "[DEBUG]: %s\n"
                "%s\n",
                glslang_shader_get_info_log(shader),
                glslang_shader_get_info_debug_log(shader), input.code);
        assert(!"glslang preprocess failed");
        glslang_shader_delete(shader);
        return NULL;
    }

    if (!glslang_shader_parse(shader, &input)) {
        fprintf(stderr,
                "GLSL parsing failed\n"
                "[INFO]: %s\n"
                "[DEBUG]: %s\n"
                "%s\n",
                glslang_shader_get_info_log(shader),
                glslang_shader_get_info_debug_log(shader),
                glslang_shader_get_preprocessed_code(shader));
        assert(!"glslang parse failed");
        glslang_shader_delete(shader);
        return NULL;
    }

    glslang_program_t *program = glslang_program_create();
    glslang_program_add_shader(program, shader);

    if (!glslang_program_link(program, GLSLANG_MSG_SPV_RULES_BIT |
                                           GLSLANG_MSG_VULKAN_RULES_BIT)) {
        fprintf(stderr,
                "GLSL linking failed\n"
                "[INFO]: %s\n"
                "[DEBUG]: %s\n",
                glslang_program_get_info_log(program),
                glslang_program_get_info_debug_log(program));
        assert(!"glslang link failed");
        glslang_program_delete(program);
        glslang_shader_delete(shader);
        return NULL;
    }

    glslang_spv_options_t spv_options = {
        .validate = true,
    };

    if (g_config.display.vulkan.debug_shaders) {
        spv_options.disable_optimizer = true;
        spv_options.generate_debug_info = true;
        spv_options.emit_nonsemantic_shader_debug_info = true;
        spv_options.emit_nonsemantic_shader_debug_source = true;

        // XXX: Note emit_nonsemantic_shader_debug_source actually does nothing
        // as of 2024.07.25. To actually get glsl source embedded in spv, we
        // must do the following...
        //
        // ref: https://github.com/KhronosGroup/glslang/issues/3252
        glslang_program_add_source_text(program, input.stage, input.code,
                                        strlen(input.code));
    }
    glslang_program_SPIRV_generate_with_options(program, stage, &spv_options);

    const char *spirv_messages = glslang_program_SPIRV_get_messages(program);
    if (spirv_messages) {
        printf("%s\b", spirv_messages);
    }

    size_t num_program_bytes =
        glslang_program_SPIRV_get_size(program) * sizeof(uint32_t);

    guint8 *data = g_malloc(num_program_bytes);
    glslang_program_SPIRV_get(program, (unsigned int *)data);

    glslang_program_delete(program);
    glslang_shader_delete(shader);

    GByteArray *spirv = g_byte_array_new_take(data, num_program_bytes);
    shader_cache_save(&cache_identity, glsl_source, source_size, spirv);
    return spirv;
}

VkShaderModule pgraph_vk_create_shader_module_from_spv(PGRAPHVkState *r, GByteArray *spv)
{
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = spv->len,
        .pCode = (uint32_t *)spv->data,
    };
    VkShaderModule module;
    VK_CHECK(
        vkCreateShaderModule(r->device, &create_info, NULL, &module));
    return module;
}

static void block_to_uniforms(const SpvReflectBlockVariable *block, ShaderUniformLayout *layout)
{
    assert(!layout->uniforms);

    layout->num_uniforms = block->member_count;
    layout->uniforms = g_malloc0_n(block->member_count, sizeof(ShaderUniform));
    layout->total_size = block->size;
    layout->allocation = g_malloc0(block->size);

    for (uint32_t k = 0; k < block->member_count; ++k) {
        const SpvReflectBlockVariable *member = &block->members[k];

        assert(member->array.dims_count < 2);

        int dim = 1;
        for (int i = 0; i < member->array.dims_count; i++) {
            dim *= member->array.dims[i];
        }
        int stride = MAX(member->array.stride, member->numeric.matrix.stride);
        if (member->numeric.matrix.column_count) {
            dim *= member->numeric.matrix.column_count;
            if (member->array.stride) {
                stride =
                    member->array.stride / member->numeric.matrix.column_count;
            }
        }
        layout->uniforms[k] = (ShaderUniform){
            .name = strdup(member->name),
            .offset = member->offset,
            .dim_v = MAX(1, member->numeric.vector.component_count),
            .dim_a = dim,
            .stride = stride,
        };

        // fprintf(stderr, "<%s offset=%zd dim_v=%zd dim_a=%zd stride=%zd>\n",
        //     layout->uniforms[k].name,
        //     layout->uniforms[k].offset,
        //     layout->uniforms[k].dim_v,
        //     layout->uniforms[k].dim_a,
        //     layout->uniforms[k].stride
        //     );
    }
    // fprintf(stderr, "--\n");
}

static void init_layout_from_spv(ShaderModuleInfo *info)
{
    SpvReflectResult result = spvReflectCreateShaderModule(
        info->spirv->len, info->spirv->data, &info->reflect_module);
    assert(result == SPV_REFLECT_RESULT_SUCCESS &&
           "Failed to create SPIR-V shader module");

    uint32_t descriptor_set_count = 0;
    result = spvReflectEnumerateDescriptorSets(&info->reflect_module,
                                               &descriptor_set_count, NULL);
    assert(result == SPV_REFLECT_RESULT_SUCCESS &&
           "Failed to enumerate descriptor sets");

    info->descriptor_sets =
        g_malloc_n(descriptor_set_count, sizeof(SpvReflectDescriptorSet *));
    result = spvReflectEnumerateDescriptorSets(
        &info->reflect_module, &descriptor_set_count, info->descriptor_sets);
    assert(result == SPV_REFLECT_RESULT_SUCCESS &&
           "Failed to enumerate descriptor sets");

    info->uniforms.num_uniforms = 0;
    info->uniforms.uniforms = NULL;

    for (uint32_t i = 0; i < descriptor_set_count; ++i) {
        const SpvReflectDescriptorSet *descriptor_set =
            info->descriptor_sets[i];
        for (uint32_t j = 0; j < descriptor_set->binding_count; ++j) {
            const SpvReflectDescriptorBinding *binding =
                descriptor_set->bindings[j];
            if (binding->descriptor_type !=
                SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
                continue;
            }

            const SpvReflectBlockVariable *block = &binding->block;
            block_to_uniforms(block, &info->uniforms);
        }
    }

    info->push_constants.num_uniforms = 0;
    info->push_constants.uniforms = NULL;
    assert(info->reflect_module.push_constant_block_count < 2);
    if (info->reflect_module.push_constant_block_count) {
        block_to_uniforms(&info->reflect_module.push_constant_blocks[0],
                          &info->push_constants);
    }
}

static glslang_stage_t vk_shader_stage_to_glslang_stage(VkShaderStageFlagBits stage)
{
    switch (stage) {
    case VK_SHADER_STAGE_GEOMETRY_BIT:
        return GLSLANG_STAGE_GEOMETRY;
    case VK_SHADER_STAGE_VERTEX_BIT:
        return GLSLANG_STAGE_VERTEX;
    case VK_SHADER_STAGE_FRAGMENT_BIT:
        return GLSLANG_STAGE_FRAGMENT;
    case VK_SHADER_STAGE_COMPUTE_BIT:
        return GLSLANG_STAGE_COMPUTE;
    default:
        assert(0);
    }
}

ShaderModuleInfo *pgraph_vk_create_shader_module_from_glsl(
    PGRAPHVkState *r, VkShaderStageFlagBits stage, const char *glsl)
{
    ShaderModuleInfo *info = g_malloc0(sizeof(*info));
    info->refcnt = 0;
    info->glsl = strdup(glsl);
    info->spirv = pgraph_vk_compile_glsl_to_spv(
        vk_shader_stage_to_glslang_stage(stage), glsl);
    info->module = pgraph_vk_create_shader_module_from_spv(r, info->spirv);
    init_layout_from_spv(info);
    return info;
}

static void finalize_uniform_layout(ShaderUniformLayout *layout)
{
    for (int i = 0; i < layout->num_uniforms; i++) {
        free((void*)layout->uniforms[i].name);
    }
    if (layout->uniforms) {
        g_free(layout->uniforms);
    }
}

void pgraph_vk_ref_shader_module(ShaderModuleInfo *info)
{
    info->refcnt++;
}

void pgraph_vk_unref_shader_module(PGRAPHVkState *r, ShaderModuleInfo *info)
{
    assert(info->refcnt >= 1);

    info->refcnt--;
    if (info->refcnt == 0) {
        pgraph_vk_destroy_shader_module(r, info);
    }
}

void pgraph_vk_destroy_shader_module(PGRAPHVkState *r, ShaderModuleInfo *info)
{
    assert(info->refcnt == 0);
    if (info->glsl) {
        free(info->glsl);
    }
    finalize_uniform_layout(&info->uniforms);
    finalize_uniform_layout(&info->push_constants);
    free(info->descriptor_sets);
    spvReflectDestroyShaderModule(&info->reflect_module);
    vkDestroyShaderModule(r->device, info->module, NULL);
    g_byte_array_unref(info->spirv);
    g_free(info);
}

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

#ifndef HW_XBOX_NV2A_PGRAPH_VK_RENDERER_H
#define HW_XBOX_NV2A_PGRAPH_VK_RENDERER_H

#include "qemu/osdep.h"
#include "qemu/thread.h"
#include "qemu/queue.h"
#include "qemu/lru.h"
#include "hw/hw.h"
#include "hw/xbox/nv2a/nv2a_int.h"
#include "hw/xbox/nv2a/nv2a_regs.h"
#include "hw/xbox/nv2a/pgraph/polygon-offset.h"
#include "hw/xbox/nv2a/pgraph/surface.h"
#include "hw/xbox/nv2a/pgraph/texture.h"
#include "hw/xbox/nv2a/pgraph/glsl/shaders.h"

#include <vulkan/vulkan.h>
#include <glslang/Include/glslang_c_interface.h>
#include <volk.h>
#include <spirv_reflect.h>
#include <vk_mem_alloc.h>

#include "blend-constants-cache.h"
#include "debug.h"
#include "constants.h"
#include "glsl.h"

#define HAVE_EXTERNAL_MEMORY 1

typedef struct QueueFamilyIndices {
    int queue_family;
} QueueFamilyIndices;

typedef struct MemorySyncRequirement {
    hwaddr addr, size;
} MemorySyncRequirement;

typedef struct RenderPassState {
    VkFormat color_format;
    VkFormat zeta_format;
} RenderPassState;

typedef struct RenderPass {
    RenderPassState state;
    VkRenderPass render_pass;
} RenderPass;

typedef struct PipelineKey {
    bool clear;
    RenderPassState render_pass_state;
    ShaderState shader_state;
    uint32_t regs[8];
    VkVertexInputBindingDescription binding_descriptions[NV2A_VERTEXSHADER_ATTRIBUTES];
    VkVertexInputAttributeDescription attribute_descriptions[NV2A_VERTEXSHADER_ATTRIBUTES];
} PipelineKey;

typedef struct PipelineBinding {
    LruNode node;
    PipelineKey key;
    VkPipelineLayout layout;
    VkPipeline pipeline;
    VkRenderPass render_pass;
    unsigned int draw_time;
    bool has_dynamic_line_width;
    uint32_t dynamic_blend_constant_mask;
} PipelineBinding;

enum Buffer {
    BUFFER_STAGING_DST,
    BUFFER_STAGING_SRC,
    BUFFER_TEXTURE_STAGING,
    BUFFER_COMPUTE_DST,
    BUFFER_COMPUTE_SRC,
    BUFFER_INDEX,
    BUFFER_INDEX_STAGING,
    BUFFER_VERTEX_RAM,
    BUFFER_VERTEX_RAM_STAGING,
    BUFFER_VERTEX_INLINE,
    BUFFER_VERTEX_INLINE_STAGING,
    BUFFER_UNIFORM,
    BUFFER_UNIFORM_STAGING,
    BUFFER_COUNT
};

typedef struct StorageBuffer {
    VkBuffer buffer;
    VkBufferUsageFlags usage;
    VmaAllocationCreateInfo alloc_info;
    VmaAllocation allocation;
    VkMemoryPropertyFlags properties;
    size_t buffer_offset;
    size_t buffer_size;
    uint8_t *mapped;
} StorageBuffer;

#define PGRAPH_VK_DEFAULT_DESCRIPTOR_SET_CAPACITY 1024
#define PGRAPH_VK_EXPANDED_DESCRIPTOR_SET_CAPACITY 2048

typedef struct SurfaceBinding {
    QTAILQ_ENTRY(SurfaceBinding) entry;
    MemAccessCallback *access_cb;
    NV2AState *d;

    hwaddr vram_addr;

    SurfaceShape shape;
    uintptr_t dma_addr;
    uintptr_t dma_len;
    bool color;
    bool swizzle;

    unsigned int width;
    unsigned int height;
    unsigned int pitch;
    size_t size;

    bool cleared;
    int frame_time;
    int draw_time;
    bool draw_dirty;
    bool download_pending;
    bool upload_pending;

    BasicSurfaceFormatInfo fmt;
    SurfaceFormatInfo host_fmt;

    VkImage image;
    VkImageView image_view;
    VmaAllocation allocation;

    // Used for scaling
    VkImage image_scratch;
    VkImageLayout image_scratch_current_layout;
    VmaAllocation allocation_scratch;

    bool initialized;

    /* Identifies this logical binding even when its allocation is recycled. */
    uint64_t lifetime_id;
} SurfaceBinding;

typedef struct ShaderModuleInfo {
    int refcnt;
    char *glsl;
    GByteArray *spirv;
    VkShaderModule module;
    SpvReflectShaderModule reflect_module;
    SpvReflectDescriptorSet **descriptor_sets;
    ShaderUniformLayout uniforms;
    ShaderUniformLayout push_constants;
} ShaderModuleInfo;

typedef struct ShaderModuleCacheKey {
    VkShaderStageFlagBits kind;
    union {
        struct {
            VshState state;
            GenVshGlslOptions glsl_opts;
        } vsh;
        struct {
            GeomState state;
            GenGeomGlslOptions glsl_opts;
        } geom;
        struct {
            PshState state;
            GenPshGlslOptions glsl_opts;
        } psh;
    };
} ShaderModuleCacheKey;

typedef struct ShaderModuleCacheEntry {
    LruNode node;
    ShaderModuleCacheKey key;
    ShaderModuleInfo *module_info;
} ShaderModuleCacheEntry;

typedef struct ShaderBinding {
    LruNode node;
    ShaderState state;
    struct {
        ShaderModuleInfo *module_info;
        VshUniformLocs uniform_locs;
    } vsh;
    struct {
        ShaderModuleInfo *module_info;
    } geom;
    struct {
        ShaderModuleInfo *module_info;
        PshUniformLocs uniform_locs;
    } psh;
} ShaderBinding;

typedef struct TextureKey {
    TextureShape state;
    VkFormat vk_format;
    hwaddr texture_vram_offset;
    hwaddr texture_length;
    hwaddr palette_vram_offset;
    hwaddr palette_length;
    float scale;
    uint32_t filter;
    uint32_t address;
    uint32_t border_color;
    uint32_t max_anisotropy;
} TextureKey;

typedef struct TextureBinding {
    LruNode node;
    TextureKey key;
    VkImage image;
    VkImageLayout current_layout;
    VkImageView image_view;
    VmaAllocation allocation;
    VkSampler sampler;
    bool possibly_dirty;
    uint64_t hash;
    unsigned int draw_time;
    uint32_t submit_time;
} TextureBinding;

#define NV2A_VK_NATIVE_BC_FORMAT_COUNT 3

typedef struct NativeBCFormatSupport {
    VkFormatProperties format_properties;
    VkImageFormatProperties image_properties[2];
    bool image_supported[2];
} NativeBCFormatSupport;

typedef struct QueryReport {
    QSIMPLEQ_ENTRY(QueryReport) entry;
    bool clear;
    hwaddr dma_report;
    uint32_t parameter;
    unsigned int query_count;
} QueryReport;

typedef struct PvideoState {
    bool enabled;
    hwaddr base;
    hwaddr limit;
    hwaddr offset;

    int pitch;
    int format;

    int in_width;
    int in_height;
    int out_width;
    int out_height;

    int in_s;
    int in_t;
    int out_x;
    int out_y;

    float scale_x;
    float scale_y;

    bool color_key_enabled;
    uint32_t color_key;
} PvideoState;

typedef struct PGRAPHVkDisplayState {
    ShaderModuleInfo *display_frag;

    VkDescriptorPool descriptor_pool;
    VkDescriptorSetLayout descriptor_set_layout;
    VkDescriptorSet descriptor_set;

    VkPipelineLayout pipeline_layout;
    VkPipeline pipeline;

    VkRenderPass render_pass;
    VkFramebuffer framebuffer;

    VkImage image;
    VkImageView image_view;
    VkDeviceMemory memory;
    VkSampler sampler;

    struct {
        PvideoState state;
        int width, height;
        VkImage image;
        VkImageView image_view;
        VmaAllocation allocation;
        VkSampler sampler;
    } pvideo;

    int width, height;
    int draw_time;

    struct {
        bool valid;
        uint64_t surface_lifetime_id;
        int surface_draw_time;
        int guest_frame_time;
        hwaddr scanout_address;
        uint32_t vga_line_offset;
        uint32_t display_width;
        uint32_t display_height;
        uint32_t surface_scale_factor;
        uint8_t interlace_mode;
    } reuse;

    // OpenGL Interop
#ifdef WIN32
    HANDLE handle;
#else
    int fd;
#endif
    GLuint gl_memory_obj;
    GLuint gl_texture_id;
} PGRAPHVkDisplayState;

typedef struct ComputePipelineKey {
    VkFormat host_fmt;
    bool pack;
    int workgroup_size;
} ComputePipelineKey;

typedef struct ComputePipeline {
    LruNode node;
    ComputePipelineKey key;
    VkPipeline pipeline;
} ComputePipeline;

typedef struct PGRAPHVkComputeState {
    VkDescriptorPool descriptor_pool;
    VkDescriptorSetLayout descriptor_set_layout;
    VkDescriptorSet descriptor_sets[1024];
    int descriptor_set_index;
    VkPipelineLayout pipeline_layout;
    Lru pipeline_cache;
    ComputePipeline *pipeline_cache_entries;
} PGRAPHVkComputeState;

typedef enum FinishReason {
    VK_FINISH_REASON_VERTEX_BUFFER_DIRTY,
    VK_FINISH_REASON_SURFACE_CREATE,
    VK_FINISH_REASON_SURFACE_DOWN,
    VK_FINISH_REASON_NEED_BUFFER_SPACE_PIPELINE_CACHE,
    VK_FINISH_REASON_NEED_BUFFER_SPACE_FRAMEBUFFER_SLOTS,
    VK_FINISH_REASON_NEED_BUFFER_SPACE_STORAGE_CAPACITY,
    VK_FINISH_REASON_NEED_BUFFER_SPACE_STORAGE_RECREATE,
    VK_FINISH_REASON_NEED_BUFFER_SPACE_UNIFORM_OR_DESCRIPTOR,
    VK_FINISH_REASON_NEED_BUFFER_SPACE_BUFFER_RESIZE,
    VK_FINISH_REASON_NEED_BUFFER_SPACE_SURFACE_COMPUTE,
    VK_FINISH_REASON_NEED_BUFFER_SPACE_TEXTURE_STAGING,
    VK_FINISH_REASON_NEED_BUFFER_SPACE_TEXTURE_COMPUTE,
    VK_FINISH_REASON_FRAMEBUFFER_DIRTY,
    VK_FINISH_REASON_PRESENTING,
    VK_FINISH_REASON_FLIP_STALL,
    VK_FINISH_REASON_FLUSH,
    VK_FINISH_REASON_STALLED,
    VK_FINISH_REASON_TEXTURE_DIRTY,
    VK_FINISH_REASON_COUNT,
} FinishReason;

typedef enum SingleTimeReason {
    VK_SINGLE_TIME_PVIDEO_UPLOAD,
    VK_SINGLE_TIME_DISPLAY_RENDER,
    VK_SINGLE_TIME_SURFACE_DOWNLOAD,
    VK_SINGLE_TIME_SURFACE_CREATE,
    VK_SINGLE_TIME_SURFACE_UPLOAD,
    VK_SINGLE_TIME_TEXTURE_UPLOAD,
    VK_SINGLE_TIME_DUMMY_TEXTURE_CREATE,
    VK_SINGLE_TIME_REASON_COUNT,
} SingleTimeReason;

typedef struct PGRAPHVkWaitStats {
    uint64_t call_count;
    uint64_t submit_count;
    uint64_t timed_submit_count;
    uint64_t submit_cpu_us;
    uint64_t wait_count;
    uint64_t wait_us;
} PGRAPHVkWaitStats;

typedef enum PerfCpuRegion {
    VK_PERF_CPU_DRAW_BEGIN_SURFACE_UPDATE,
    VK_PERF_CPU_DRAW_FLUSH,
    VK_PERF_CPU_PIPELINE_PREPARE,
    VK_PERF_CPU_BIND_TEXTURES,
    VK_PERF_CPU_BIND_SHADERS,
    VK_PERF_CPU_SHADER_STATE_PREPARE,
    VK_PERF_CPU_SHADER_UNIFORM_NEEDS,
    VK_PERF_CPU_SHADER_UNIFORM_UPDATE,
    VK_PERF_CPU_PIPELINE_STATE_LOOKUP,
    VK_PERF_CPU_TEXTURE_UPLOAD,
    VK_PERF_CPU_UPDATE_DESCRIPTOR_SETS,
    VK_PERF_CPU_REGION_COUNT,
} PerfCpuRegion;

typedef struct PGRAPHVkCpuStats {
    uint64_t call_count;
    uint64_t cpu_us;
} PGRAPHVkCpuStats;

typedef struct PGRAPHVkPerfTelemetry {
    FILE *file;
    bool enabled;
    uint64_t frame;
    int64_t last_flush_us;
    PGRAPHVkWaitStats finish[VK_FINISH_REASON_COUNT];
    PGRAPHVkWaitStats single_time[VK_SINGLE_TIME_REASON_COUNT];
    PGRAPHVkCpuStats cpu_regions[VK_PERF_CPU_REGION_COUNT];
    uint64_t shader_bind_call_count;
    uint64_t shader_state_check_count;
    uint64_t shader_state_dirty_count;
    uint64_t shader_binding_change_count;
    uint64_t vsh_uniform_update_request_count;
    uint64_t psh_uniform_update_request_count;
    uint64_t shader_uniform_no_update_count;
    uint64_t vsh_uniform_source_change_count;
    uint64_t vsh_uniform_layout_change_count;
    uint64_t vsh_uniform_inline_value_count;
    uint64_t vsh_uniform_dirty_row_count;
    uint64_t psh_uniform_source_change_count;
    uint64_t psh_uniform_layout_change_count;
    uint64_t psh_uniform_texture_binding_change_count;
    uint64_t psh_uniform_effective_input_change_count;
    uint64_t shader_uniform_force_full_update_count;
    uint64_t vsh_uniform_value_change_count;
    uint64_t psh_uniform_value_change_count;
    uint64_t submit_info_count;
    uint64_t command_buffer_count;
    uint64_t staged_bytes;
    uint64_t vertex_staged_bytes;
    uint64_t vertex_staging_copy_count;
    uint64_t vertex_staging_capacity_growth_count;
    uint64_t vertex_staging_fallback_finish_count;
    uint64_t vertex_dirty_check_count;
    uint64_t vertex_dirty_pages_checked;
    uint64_t vertex_dirty_hit_count;
    uint64_t vertex_dirty_hit_pages;
    uint64_t vertex_dirty_repeated_range_count;
    uint64_t vertex_dirty_repeated_range_hit_count;
    MemorySyncRequirement vertex_dirty_recent_ranges[8];
    uint8_t vertex_dirty_recent_range_count;
    uint8_t vertex_dirty_recent_range_next;
    uint64_t native_bc_upload_count;
    uint64_t native_bc_source_bytes;
    uint64_t native_bc_staged_bytes;
    uint64_t native_bc_prepare_cpu_us;
    uint64_t decoded_bc_upload_count;
    uint64_t decoded_bc_source_bytes;
    uint64_t decoded_bc_staged_bytes;
    uint64_t decoded_bc_prepare_cpu_us;
    uint64_t descriptor_set_highwater;
    uint64_t index_payload_count;
    uint64_t index_payload_bytes;
    uint64_t consecutive_duplicate_index_payload_count;
    uint64_t consecutive_duplicate_index_payload_bytes;
    uint64_t reused_index_payload_count;
    uint64_t reused_index_payload_bytes;
    bool reuse_identical_index_payloads;
    bool last_index_payload_valid;
    VkDeviceSize last_index_payload_offset;
    VkDeviceSize last_index_payload_size;
    uint64_t push_constant_count;
    uint64_t push_constant_bytes;
    uint64_t identical_push_constant_count;
    uint64_t identical_push_constant_bytes;
    uint64_t skipped_push_constant_count;
    bool skip_identical_push_constants;
    bool last_push_constant_valid;
    VkPipelineLayout last_push_constant_layout;
    size_t last_push_constant_size;
    float last_push_constant_values[NV2A_VERTEXSHADER_ATTRIBUTES][4];
    uint64_t in_flight_submission_count;
    uint64_t peak_in_flight_submission_count;
    uint64_t oldest_in_flight_serial;
    uint64_t newest_submitted_serial;
    uint64_t submission_serial;
    uint64_t retirement_queue_objects;
    uint64_t retirement_queue_bytes;
} PGRAPHVkPerfTelemetry;

typedef struct PGRAPHVkState {
    uint32_t vk_api_version;
    VkInstance instance;
    VkDebugUtilsMessengerEXT debug_messenger;
    int debug_depth;

    bool debug_utils_extension_enabled;
    bool custom_border_color_extension_enabled;
    bool memory_budget_extension_enabled;
    bool demote_to_helper_extension_enabled;

    VkPhysicalDevice physical_device;
    VkPhysicalDeviceFeatures enabled_physical_device_features;
    VkPhysicalDeviceProperties device_props;
    VkDevice device;
    VmaAllocator allocator;
    uint32_t allocator_last_submit_index;

    VkQueue queue;
    VkCommandPool command_pool;
    VkCommandBuffer command_buffers[2];

    VkCommandBuffer command_buffer;
    VkSemaphore command_buffer_semaphore;
    VkFence command_buffer_fence;
    unsigned int command_buffer_start_time;
    bool in_command_buffer;
    uint32_t submit_count;
    PGRAPHVkBlendConstantsCache blend_constants;

    VkCommandBuffer aux_command_buffer;
    bool in_aux_command_buffer;

    PGRAPHVkPerfTelemetry perf;

    uint64_t next_surface_lifetime_id;

    VkFramebuffer framebuffers[50];
    int framebuffer_index;
    bool framebuffer_dirty;

    VkRenderPass render_pass;
    GArray *render_passes; // RenderPass
    bool in_render_pass;
    bool in_draw;

    Lru pipeline_cache;
    VkPipelineCache vk_pipeline_cache;
    PipelineBinding *pipeline_cache_entries;
    PipelineBinding *pipeline_binding;
    bool pipeline_binding_changed;

    VkDescriptorPool descriptor_pool;
    VkDescriptorSetLayout descriptor_set_layout;
    VkDescriptorSet
        descriptor_sets[PGRAPH_VK_EXPANDED_DESCRIPTOR_SET_CAPACITY];
    uint32_t descriptor_set_capacity;
    int descriptor_set_index;

    StorageBuffer storage_buffers[BUFFER_COUNT];

    MemorySyncRequirement vertex_ram_buffer_syncs[NV2A_VERTEXSHADER_ATTRIBUTES];
    size_t num_vertex_ram_buffer_syncs;

    VkVertexInputAttributeDescription vertex_attribute_descriptions[NV2A_VERTEXSHADER_ATTRIBUTES];
    int vertex_attribute_to_description_location[NV2A_VERTEXSHADER_ATTRIBUTES];
    int num_active_vertex_attribute_descriptions;

    VkVertexInputBindingDescription vertex_binding_descriptions[NV2A_VERTEXSHADER_ATTRIBUTES];
    int num_active_vertex_binding_descriptions;
    hwaddr vertex_attribute_offsets[NV2A_VERTEXSHADER_ATTRIBUTES];

    QTAILQ_HEAD(, SurfaceBinding) surfaces;
    QTAILQ_HEAD(, SurfaceBinding) invalid_surfaces;
    SurfaceBinding *color_binding, *zeta_binding;
    bool downloads_pending;
    QemuEvent downloads_complete;
    bool download_dirty_surfaces_pending;
    QemuEvent dirty_surfaces_download_complete; // common

    Lru texture_cache;
    TextureBinding *texture_cache_entries;
    TextureBinding *texture_bindings[NV2A_MAX_TEXTURES];
    TextureBinding dummy_texture;
    bool texture_bindings_changed;
    VkFormatProperties *texture_format_properties;
    NativeBCFormatSupport
        native_bc_format_support[NV2A_VK_NATIVE_BC_FORMAT_COUNT];

    Lru shader_cache;
    ShaderBinding *shader_cache_entries;
    ShaderBinding *shader_binding;
    ShaderModuleInfo *quad_vert_module, *solid_frag_module;
    bool shader_bindings_changed;
    bool use_push_constants_for_uniform_attrs;

    Lru shader_module_cache;
    ShaderModuleCacheEntry *shader_module_cache_entries;

    // FIXME: Merge these into a structure
    size_t uniform_buffer_offsets[2];
    bool uniforms_changed;
    bool uniform_stage_dirty[PGRAPH_UNIFORM_STAGE_COUNT];
    bool uniform_layout_changed[PGRAPH_UNIFORM_STAGE_COUNT];
    PGRAPHUniformSourceEpochs last_uniform_source_epochs;
    bool polygon_offset_key_valid;
    PGRAPHPolygonOffsetUniformKey polygon_offset_key;

    VkQueryPool query_pool;
    int max_queries_in_flight; // FIXME: Move out to constant
    int num_queries_in_flight;
    bool new_query_needed;
    bool query_in_flight;
    uint32_t zpass_pixel_count_result;
    QSIMPLEQ_HEAD(, QueryReport) report_queue; // FIXME: Statically allocate

    SurfaceFormatInfo kelvin_surface_zeta_vk_map[3];

    uint32_t clear_parameter;

    PGRAPHVkDisplayState display;
    PGRAPHVkComputeState compute;
} PGRAPHVkState;

// renderer.c
void pgraph_vk_check_memory_budget(PGRAPHState *pg);

// debug.c
#define RGBA_RED     (float[4]){1,0,0,1}
#define RGBA_YELLOW  (float[4]){1,1,0,1}
#define RGBA_GREEN   (float[4]){0,1,0,1}
#define RGBA_BLUE    (float[4]){0,0,1,1}
#define RGBA_PINK    (float[4]){1,0,1,1}
#define RGBA_DEFAULT (float[4]){0,0,0,0}

void pgraph_vk_debug_init(void);
void pgraph_vk_insert_debug_marker(PGRAPHVkState *r, VkCommandBuffer cmd,
                                   float color[4], const char *format, ...) G_GNUC_PRINTF(4, 5);
void pgraph_vk_begin_debug_marker(PGRAPHVkState *r, VkCommandBuffer cmd,
                                  float color[4], const char *format, ...) G_GNUC_PRINTF(4, 5);
void pgraph_vk_end_debug_marker(PGRAPHVkState *r, VkCommandBuffer cmd);

// instance.c
void pgraph_vk_init_instance(PGRAPHState *pg, Error **errp);
void pgraph_vk_finalize_instance(PGRAPHState *pg);
QueueFamilyIndices pgraph_vk_find_queue_families(VkPhysicalDevice device);
uint32_t pgraph_vk_get_memory_type(PGRAPHState *pg, uint32_t type_bits,
                                   VkMemoryPropertyFlags properties);

// glsl.c
void pgraph_vk_init_glsl_compiler(void);
void pgraph_vk_finalize_glsl_compiler(void);
GByteArray *pgraph_vk_compile_glsl_to_spv(glslang_stage_t stage,
                                          const char *glsl_source);
VkShaderModule pgraph_vk_create_shader_module_from_spv(PGRAPHVkState *r,
                                                       GByteArray *spv);
ShaderModuleInfo *pgraph_vk_create_shader_module_from_glsl(
    PGRAPHVkState *r, VkShaderStageFlagBits stage, const char *glsl);
void pgraph_vk_ref_shader_module(ShaderModuleInfo *info);
void pgraph_vk_unref_shader_module(PGRAPHVkState *r, ShaderModuleInfo *info);
void pgraph_vk_destroy_shader_module(PGRAPHVkState *r, ShaderModuleInfo *info);

// buffer.c
void pgraph_vk_init_buffers(NV2AState *d);
void pgraph_vk_finalize_buffers(NV2AState *d);
bool pgraph_vk_buffer_has_space_for(PGRAPHState *pg, int index,
                                    VkDeviceSize size,
                                    VkDeviceAddress alignment);
bool pgraph_vk_grow_vertex_ram_staging_buffer(PGRAPHState *pg,
                                               VkDeviceSize required_size);
VkDeviceSize pgraph_vk_buffer_required_size(PGRAPHState *pg, int index,
                                            VkDeviceSize size,
                                            VkDeviceAddress alignment);
void pgraph_vk_ensure_buffer_capacity(PGRAPHState *pg, int index,
                                      VkDeviceSize required_size);
void pgraph_vk_ensure_buffer_pair_capacity(PGRAPHState *pg, int index,
                                           size_t required_size);
VkDeviceSize pgraph_vk_append_to_buffer(PGRAPHState *pg, int index, void **data,
                                        VkDeviceSize *sizes, size_t count,
                                        VkDeviceAddress alignment);

// command.c
void pgraph_vk_init_command_buffers(PGRAPHState *pg);
void pgraph_vk_finalize_command_buffers(PGRAPHState *pg);
VkCommandBuffer pgraph_vk_begin_single_time_commands(PGRAPHState *pg);
void pgraph_vk_end_single_time_commands(PGRAPHState *pg, VkCommandBuffer cmd,
                                        SingleTimeReason reason,
                                        uint64_t staged_bytes);

// perf.c
void pgraph_vk_perf_init(PGRAPHVkState *r);
void pgraph_vk_perf_finalize(PGRAPHVkState *r);
void pgraph_vk_perf_record_finish_call(PGRAPHVkState *r, FinishReason reason);
bool pgraph_vk_perf_should_time_finish(PGRAPHVkState *r, FinishReason reason);
void pgraph_vk_perf_record_finish_submit(PGRAPHVkState *r,
                                         FinishReason reason,
                                         bool timed,
                                         uint64_t submit_cpu_us,
                                         uint64_t wait_us,
                                         uint64_t staged_bytes,
                                         uint64_t submit_info_count,
                                         uint64_t command_buffer_count);
void pgraph_vk_perf_record_single_time_submit(PGRAPHVkState *r,
                                               SingleTimeReason reason,
                                               uint64_t submit_cpu_us,
                                               uint64_t wait_us,
                                               uint64_t staged_bytes);
void pgraph_vk_perf_record_vertex_staging_copy(PGRAPHVkState *r,
                                                uint64_t bytes);
void pgraph_vk_perf_record_vertex_staging_growth(PGRAPHVkState *r);
void pgraph_vk_perf_record_vertex_staging_fallback(PGRAPHVkState *r);
void pgraph_vk_perf_record_vertex_dirty_check(PGRAPHVkState *r, hwaddr addr,
                                               hwaddr size, bool dirty);
void pgraph_vk_perf_record_bc_upload(PGRAPHVkState *r, bool native,
                                     uint64_t source_bytes,
                                     uint64_t staged_bytes,
                                     uint64_t prepare_cpu_us);
bool pgraph_vk_perf_try_reuse_index_payload(PGRAPHVkState *r,
                                            const void *data,
                                            VkDeviceSize size,
                                            VkDeviceSize *staging_offset);
void pgraph_vk_perf_commit_index_payload(PGRAPHVkState *r,
                                         VkDeviceSize size,
                                         VkDeviceSize staging_offset);
bool pgraph_vk_perf_should_emit_push_constants(PGRAPHVkState *r,
                                               VkPipelineLayout layout,
                                               const float *values,
                                               size_t size);
void pgraph_vk_perf_begin_command_buffer(PGRAPHVkState *r);
void pgraph_vk_perf_record_cpu_region(PGRAPHVkState *r, PerfCpuRegion region,
                                      uint64_t cpu_us);
void pgraph_vk_perf_frame(PGRAPHVkState *r);

// image.c
void pgraph_vk_transition_image_layout(PGRAPHState *pg, VkCommandBuffer cmd,
                                       VkImage image, VkFormat format,
                                       VkImageLayout oldLayout,
                                       VkImageLayout newLayout);

// vertex.c
void pgraph_vk_bind_vertex_attributes(NV2AState *d, unsigned int min_element,
                                      unsigned int max_element,
                                      bool inline_data,
                                      unsigned int inline_stride,
                                      unsigned int provoking_element);
void pgraph_vk_bind_vertex_attributes_inline(NV2AState *d);
void pgraph_vk_update_vertex_ram_buffer(PGRAPHState *pg, hwaddr offset, void *data,
                                    VkDeviceSize size);
VkDeviceSize pgraph_vk_update_index_buffer(PGRAPHState *pg, void *data,
                                           VkDeviceSize size);
VkDeviceSize pgraph_vk_update_vertex_inline_buffer(PGRAPHState *pg, void **data,
                                                   VkDeviceSize *sizes,
                                                   size_t count);

// surface.c
void pgraph_vk_init_surfaces(PGRAPHState *pg);
void pgraph_vk_finalize_surfaces(PGRAPHState *pg);
void pgraph_vk_surface_flush(NV2AState *d);
void pgraph_vk_process_pending_downloads(NV2AState *d);
void pgraph_vk_surface_download_if_dirty(NV2AState *d, SurfaceBinding *surface);
SurfaceBinding *pgraph_vk_surface_get_within(NV2AState *d, hwaddr addr);
void pgraph_vk_wait_for_surface_download(SurfaceBinding *e);
void pgraph_vk_download_dirty_surfaces(NV2AState *d);
void pgraph_vk_download_surfaces_in_range_if_dirty(PGRAPHState *pg, hwaddr start, hwaddr size);
void pgraph_vk_upload_surface_data(NV2AState *d, SurfaceBinding *surface,
                                   bool force);
void pgraph_vk_surface_update(NV2AState *d, bool upload, bool color_write,
                              bool zeta_write);
SurfaceBinding *pgraph_vk_surface_get(NV2AState *d, hwaddr addr);
void pgraph_vk_set_surface_dirty(PGRAPHState *pg, bool color, bool zeta);
void pgraph_vk_set_surface_scale_factor(NV2AState *d, unsigned int scale);
unsigned int pgraph_vk_get_surface_scale_factor(NV2AState *d);
void pgraph_vk_reload_surface_scale_factor(PGRAPHState *pg);

// surface-compute.c
void pgraph_vk_init_compute(PGRAPHState *pg);
bool pgraph_vk_compute_needs_finish(PGRAPHVkState *r);
void pgraph_vk_compute_finish_complete(PGRAPHVkState *r);
void pgraph_vk_finalize_compute(PGRAPHState *pg);
void pgraph_vk_pack_depth_stencil(PGRAPHState *pg, SurfaceBinding *surface,
                                  VkCommandBuffer cmd, VkBuffer src,
                                  VkBuffer dst, bool downscale);
void pgraph_vk_unpack_depth_stencil(PGRAPHState *pg, SurfaceBinding *surface,
                                    VkCommandBuffer cmd, VkBuffer src,
                                    VkBuffer dst);

// display.c
void pgraph_vk_init_display(PGRAPHState *pg);
void pgraph_vk_finalize_display(PGRAPHState *pg);
void pgraph_vk_render_display(PGRAPHState *pg);

// texture.c
void pgraph_vk_init_textures(PGRAPHState *pg);
void pgraph_vk_finalize_textures(PGRAPHState *pg);
void pgraph_vk_bind_textures(NV2AState *d);
void pgraph_vk_mark_textures_possibly_dirty(NV2AState *d, hwaddr addr,
                                            hwaddr size);
void pgraph_vk_trim_texture_cache(PGRAPHState *pg);

// shaders.c
void pgraph_vk_init_shaders(PGRAPHState *pg);
void pgraph_vk_finalize_shaders(PGRAPHState *pg);
void pgraph_vk_update_descriptor_sets(PGRAPHState *pg);
void pgraph_vk_bind_shaders(PGRAPHState *pg);

// reports.c
void pgraph_vk_init_reports(PGRAPHState *pg);
void pgraph_vk_finalize_reports(PGRAPHState *pg);
void pgraph_vk_clear_report_value(NV2AState *d);
void pgraph_vk_get_report(NV2AState *d, uint32_t parameter);
void pgraph_vk_process_pending_reports(NV2AState *d);
void pgraph_vk_process_pending_reports_internal(NV2AState *d);

// draw.c
void pgraph_vk_init_pipelines(PGRAPHState *pg);
void pgraph_vk_finalize_pipelines(PGRAPHState *pg);
void pgraph_vk_clear_surface(NV2AState *d, uint32_t parameter);
void pgraph_vk_draw_begin(NV2AState *d);
void pgraph_vk_draw_end(NV2AState *d);
void pgraph_vk_finish(PGRAPHState *pg, FinishReason why);
void pgraph_vk_flush_draw(NV2AState *d);
void pgraph_vk_invalidate_blend_constants(PGRAPHState *pg);
void pgraph_vk_begin_command_buffer(PGRAPHState *pg);
void pgraph_vk_ensure_command_buffer(PGRAPHState *pg);
void pgraph_vk_ensure_not_in_render_pass(PGRAPHState *pg);

VkCommandBuffer pgraph_vk_begin_nondraw_commands(PGRAPHState *pg);
void pgraph_vk_end_nondraw_commands(PGRAPHState *pg, VkCommandBuffer cmd);

// blit.c
void pgraph_vk_image_blit(NV2AState *d);

// gpuprops.c
void pgraph_vk_determine_gpu_properties(NV2AState *d);
GPUProperties *pgraph_vk_get_gpu_properties(void);

#endif

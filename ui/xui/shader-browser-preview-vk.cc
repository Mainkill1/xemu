// SPDX-License-Identifier: GPL-2.0-or-later
#include "qemu/osdep.h"
#include "shader-browser-preview-vk.hh"
#include "shader-browser-preview-adapter.hh"
#include "shader-browser-preview-alpha.hh"

#ifdef CONFIG_VULKAN
#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>
#include <glslang/Include/glslang_c_interface.h>
#include <spirv_reflect.h>
#include <algorithm>
#include <cmath>
#include <array>
#include <chrono>
#include <cstring>
#include <thread>

namespace xemu::shader_browser {
namespace {
static_assert(
    sizeof(VkPipelineInputAssemblyStateCreateInfo) == 32 &&
        sizeof(VkPipelineRasterizationStateCreateInfo) == 64 &&
        sizeof(VkPipelineMultisampleStateCreateInfo) == 48 &&
        sizeof(VkPipelineDepthStencilStateCreateInfo) == 104 &&
        sizeof(VkPipelineColorBlendStateCreateInfo) == 56 &&
        offsetof(VkPipelineRasterizationStateCreateInfo, cullMode) == 32 &&
        offsetof(VkPipelineRasterizationStateCreateInfo, depthClampEnable) == 20 &&
        offsetof(VkPipelineDepthStencilStateCreateInfo, front) == 40 &&
        offsetof(VkPipelineColorBlendStateCreateInfo, blendConstants) == 40,
    "Captured Vulkan scalar raster decoding requires the 64-bit ABI1 layout");
// Resolve into this owner's table. Calling volkLoadInstance/volkLoadDevice
// here would replace the game renderer's process-global dispatch pointers.
#define PREVIEW_VK_INSTANCE_FUNCTIONS(X)      \
    X(DestroyInstance)                        \
    X(EnumeratePhysicalDevices)               \
    X(GetPhysicalDeviceQueueFamilyProperties) \
    X(GetPhysicalDeviceMemoryProperties)      \
    X(GetPhysicalDeviceFormatProperties)      \
    X(GetPhysicalDeviceFeatures)              \
    X(GetPhysicalDeviceProperties) X(CreateDevice) X(GetDeviceProcAddr)
#define PREVIEW_VK_DEVICE_FUNCTIONS(X)                                             \
    X(DestroyDevice)                                                               \
    X(GetDeviceQueue)                                                              \
    X(CreateCommandPool)                                                           \
    X(DestroyCommandPool)                                                          \
    X(AllocateCommandBuffers)                                                      \
    X(ResetCommandBuffer)                                                          \
    X(BeginCommandBuffer)                                                          \
    X(EndCommandBuffer)                                                            \
    X(CreateFence) X(DestroyFence) X(ResetFences) X(GetFenceStatus) X(             \
        QueueSubmit) X(DeviceWaitIdle) X(CreateBuffer) X(DestroyBuffer)            \
        X(GetBufferMemoryRequirements) X(AllocateMemory) X(FreeMemory) X(          \
            BindBufferMemory) X(MapMemory) X(UnmapMemory) X(CreateImage)           \
            X(DestroyImage) X(GetImageMemoryRequirements) X(BindImageMemory) X(    \
                CreateImageView) X(DestroyImageView) X(CreateSampler)              \
                X(DestroySampler) X(CreateRenderPass) X(DestroyRenderPass) X(      \
                    CreateFramebuffer) X(DestroyFramebuffer) X(CreateShaderModule) \
                    X(DestroyShaderModule) X(CreatePipelineLayout) X(              \
                        DestroyPipelineLayout) X(CreateGraphicsPipelines)          \
                        X(DestroyPipeline) X(CreateDescriptorSetLayout) X(         \
                            DestroyDescriptorSetLayout)                            \
                            X(CreateDescriptorPool) X(DestroyDescriptorPool) X(    \
                                AllocateDescriptorSets) X(UpdateDescriptorSets)    \
                                X(CmdPipelineBarrier) X(CmdCopyBufferToImage) X(   \
                                    CmdCopyImageToBuffer) X(CmdClearColorImage)    \
                                    X(CmdBeginRenderPass) X(CmdEndRenderPass) X(   \
                                        CmdBindPipeline) X(CmdBindDescriptorSets)  \
                                        X(CmdBindVertexBuffers) X(                 \
                                            CmdSetViewport) X(CmdSetScissor)       \
                                            X(CmdDraw) X(CmdBindIndexBuffer) X(    \
                                                CmdDrawIndexed)                    \
                                                X(CmdPushConstants) X(             \
                                                    CreateQueryPool)               \
                                                    X(DestroyQueryPool) X(         \
                                                        CmdResetQueryPool)         \
                                                        X(CmdWriteTimestamp) X(    \
                                                            GetQueryPoolResults)
struct Dispatch {
#define DECLARE(name) PFN_vk##name name = nullptr;
    PREVIEW_VK_INSTANCE_FUNCTIONS(DECLARE)
    PREVIEW_VK_DEVICE_FUNCTIONS(DECLARE)
#undef DECLARE
};
using Vertex = PreviewSceneVertex;
constexpr size_t kPipelineVariantCount = 2 * 2 * 2 * 3;
size_t PipelineVariantIndex(const PreviewRenderState &requested)
{
    const auto state = ClampPreviewRenderState(requested);
    return (((static_cast<size_t>(state.blend) * 2 + state.depth_test) * 2 +
             state.depth_write) * 3 + static_cast<size_t>(state.cull));
}
struct Uniform {
    std::string name;
    uint32_t offset, count, stride, components;
    uint32_t type = XEMU_SHADER_DRAW_UNIFORM_FLOAT;
    uint32_t matrix_stride = 0;
    uint32_t stage = 2;
};

bool MatrixColumnStride(const std::vector<uint32_t> &words, uint32_t type_id,
                        uint32_t member, uint32_t *stride)
{
    bool column_major = false, row_major = false;
    *stride = 0;
    // Older SPIRV-Reflect versions lose MatrixStride on arrays of matrices.
    // Read the member's layout decoration rather than assuming std140 padding.
    for (size_t offset = 5; offset < words.size();) {
        const uint32_t length = words[offset] >> 16;
        if (!length || length > words.size() - offset)
            return false;
        if ((words[offset] & 0xffff) == SpvOpMemberDecorate && length >= 4 &&
            words[offset + 1] == type_id && words[offset + 2] == member) {
            const uint32_t decoration = words[offset + 3];
            if (decoration == SpvDecorationColMajor)
                column_major = true;
            else if (decoration == SpvDecorationRowMajor)
                row_major = true;
            else if (decoration == SpvDecorationMatrixStride) {
                if (length != 5)
                    return false;
                *stride = words[offset + 4];
            }
        }
        offset += length;
    }
    return column_major && !row_major && *stride >= 8 && *stride <= 4096;
}

bool Compile(const std::string &source, glslang_stage_t stage,
             std::vector<uint32_t> *words, std::string *error)
{
    glslang_resource_t resources{};
    resources.max_vertex_attribs = 16;
    resources.max_vertex_uniform_components = 4096;
    resources.max_fragment_uniform_components = 4096;
    resources.max_varying_floats = 64;
    resources.max_varying_vectors = 16;
    resources.max_vertex_output_vectors = 16;
    resources.max_fragment_input_vectors = 16;
    resources.max_vertex_output_components = 64;
    resources.max_fragment_input_components = 64;
    resources.max_texture_image_units = 16;
    resources.max_combined_texture_image_units = 16;
    resources.max_draw_buffers = 1;
    resources.max_clip_distances = 8;
    resources.max_combined_clip_and_cull_distances = 8;
    resources.max_samples = 1;
    resources.max_geometry_input_components = 128;
    resources.max_geometry_output_components = 128;
    resources.max_geometry_output_vertices = 256;
    resources.max_geometry_total_output_components = 1024;
    resources.max_geometry_uniform_components = 1024;
    resources.max_geometry_texture_image_units = 16;
    resources.limits.non_inductive_for_loops = true;
    resources.limits.while_loops = true;
    resources.limits.do_while_loops = true;
    resources.limits.general_uniform_indexing = true;
    resources.limits.general_attribute_matrix_vector_indexing = true;
    resources.limits.general_varying_indexing = true;
    resources.limits.general_sampler_indexing = true;
    resources.limits.general_variable_indexing = true;
    resources.limits.general_constant_matrix_vector_indexing = true;
    glslang_input_t input{};
    input.language = GLSLANG_SOURCE_GLSL;
    input.stage = stage;
    input.client = GLSLANG_CLIENT_VULKAN;
    input.client_version = GLSLANG_TARGET_VULKAN_1_0;
    input.target_language = GLSLANG_TARGET_SPV;
    input.target_language_version = GLSLANG_TARGET_SPV_1_0;
    input.code = source.c_str();
    input.default_version = 450;
    input.default_profile = GLSLANG_NO_PROFILE;
    input.messages = static_cast<glslang_messages_t>(
        GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT);
    input.resource = &resources;
    glslang_shader_t *shader = glslang_shader_create(&input);
    if (!shader) {
        *error = "Preview Vulkan compiler allocation failed";
        return false;
    }
    bool ok = glslang_shader_preprocess(shader, &input) &&
              glslang_shader_parse(shader, &input);
    glslang_program_t *program = nullptr;
    if (!ok)
        *error = std::string("Preview Vulkan GLSL: ") +
                 glslang_shader_get_info_log(shader);
    if (ok) {
        program = glslang_program_create();
        ok = program != nullptr;
        if (ok) {
            glslang_program_add_shader(program, shader);
            ok = glslang_program_link(program, input.messages);
            if (!ok)
                *error = std::string("Preview Vulkan link: ") +
                         glslang_program_get_info_log(program);
        }
    }
    if (ok) {
        glslang_spv_options_t options{};
        options.disable_optimizer = true;
        glslang_program_SPIRV_generate_with_options(program, stage, &options);
        words->resize(glslang_program_SPIRV_get_size(program));
        ok = !words->empty();
        if (ok)
            glslang_program_SPIRV_get(program, words->data());
        else
            *error = "Preview Vulkan SPIR-V generation failed";
    }
    if (program)
        glslang_program_delete(program);
    glslang_shader_delete(shader);
    if (error->size() > 2048)
        error->resize(2048);
    return ok;
}
} // namespace

struct PreviewVkExecutor::Impl {
    Dispatch api;
    SDL_SharedObject *loader = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkQueryPool timestamp_pool = VK_NULL_HANDLE;
    uint32_t timestamp_valid_bits = 0;
    double timestamp_period_ns = 0;
    struct DrawQuery {
        PreviewWorkItem work;
        PreviewDrawTiming timing;
        bool pending = false;
        uint64_t issued_ns = 0;
    };
    std::array<DrawQuery, kPreviewSlotCount> draw_queries{};

    bool CollectDrawQuery(uint32_t slot)
    {
        auto &query = draw_queries[slot];
        if (!query.pending)
            return false;
        struct Result {
            uint64_t timestamp, available;
        } results[2]{};
        const VkResult status = api.GetQueryPoolResults(
            device, timestamp_pool, slot * 2, 2, sizeof(results), results,
            sizeof(Result),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
        if ((status == VK_SUCCESS || status == VK_NOT_READY) &&
            (!results[0].available || !results[1].available) &&
            SDL_GetTicksNS() - query.issued_ns <= kPreviewMaxDrawTimingNs)
            return false;
        query.pending = false;
        auto &timing = query.timing;
        timing.status = PreviewDrawTimingStatus::Failed;
        if ((status != VK_SUCCESS && status != VK_NOT_READY) ||
            !results[0].available || !results[1].available) {
            timing.message =
                "Vulkan timestamp availability failed or exceeded 10 s";
            return true;
        }
        if (ComputePreviewDrawInterval(
                results[0].timestamp, results[1].timestamp,
                timestamp_valid_bits, timestamp_period_ns, &timing.nanoseconds,
                &timing.message))
            timing.status = PreviewDrawTimingStatus::Measured;
        return true;
    }
    bool AdmitDrawQuery(const PreviewWorkItem &work)
    {
        auto &query = draw_queries[work.slot];
        query.pending = false;
        query.work = work;
        query.work.packet.reset();
        query.timing = {};
        auto &timing = query.timing;
        timing.result = work.result_key;
        timing.backend = PreviewBackend::Vulkan;
        if (!work.packet->profile_draw)
            return false;
        timing.provenance =
            work.packet->packet_kind == PreviewPacketKind::Replay ?
                PreviewDrawTimingProvenance::ReplayInstrumented :
                PreviewDrawTimingProvenance::SelectedPreviewInstrumented;
        timing.status = PreviewDrawTimingStatus::Unsupported;
        timing.actual_draw_commands =
            work.packet->captured_pipeline &&
                    work.packet->captured_pipeline->indices.empty() ?
                work.packet->captured_pipeline->ranges.size() :
                1;
        if (!timestamp_valid_bits || timestamp_valid_bits > 64 ||
            !std::isfinite(timestamp_period_ns) || timestamp_period_ns <= 0) {
            timing.message =
                "Preview Vulkan queue does not support timestamp intervals";
            return false;
        }
        timing.timestamp_valid_bits = timestamp_valid_bits;
        timing.timestamp_period_ns = timestamp_period_ns;
        if (!timestamp_pool) {
            VkQueryPoolCreateInfo info{
                VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO
            };
            info.queryType = VK_QUERY_TYPE_TIMESTAMP;
            info.queryCount = kPreviewSlotCount * 2;
            const auto result =
                api.CreateQueryPool(device, &info, nullptr, &timestamp_pool);
            if (result != VK_SUCCESS) {
                timing.status = PreviewDrawTimingStatus::Failed;
                timing.message =
                    "Preview Vulkan timestamp pool allocation failed (" +
                    std::to_string(result) + ")";
                return false;
            }
        }
        timing.status = PreviewDrawTimingStatus::Pending;
        query.pending = true;
        query.issued_ns = SDL_GetTicksNS();
        return true;
    }
    VkRenderPass pass = VK_NULL_HANDLE;
    std::array<VkPipeline, kPipelineVariantCount> pipelines{};
    VkPipeline reference_pipeline = VK_NULL_HANDLE;
    VkShaderModule selected_modules[2]{};
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    bool unsupported_depth = false;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memory{};
    struct Buffer {
        VkBuffer handle = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void *mapped = nullptr;
    } vertices, uniform, upload, readback;
    struct Image {
        VkImage handle = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    } target, depth;
    std::array<Image, 4> textures{};
    std::array<VkSampler, 4> captured_samplers{};
    std::array<uint32_t, 4> texture_widths{ 8, 8, 8, 8 };
    std::array<uint32_t, 4> texture_texel_bytes{ 4, 4, 4, 4 };
    std::array<uint32_t, 4> texture_heights{ 8, 8, 8, 8 };
    std::array<size_t, 4> texture_offsets{};
    PreviewDigest material_digest{};
    bool material_active = false;
    bool material_uploaded = false;
    std::array<Buffer, 16> raw_vertices{};
    Buffer raw_indices;
    Buffer seed_upload;
    PreviewCapturedRaster native_raster;
    VkPrimitiveTopology native_topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    std::vector<VkVertexInputBindingDescription> raw_bindings;
    std::vector<VkVertexInputAttributeDescription> raw_attributes;
    std::array<uint32_t, 32> uniform_binding_offset{}, uniform_binding_size{};
    std::vector<uint8_t> push_data;
    uint32_t uniform_alignment = 256, max_push_bytes = 0;
    bool original_pipeline = false, geometry_supported = false;
    bool depth_clamp_supported = false;
    VkShaderModule geometry_module = VK_NULL_HANDLE;
    PreviewDigest uploaded_pipeline_digest{};
    bool pipeline_uploaded = false;
    std::array<bool, 4> cubes{};
    std::array<unsigned, 32> binding_stage{};
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    std::vector<Uniform> uniforms;
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    uint32_t uniform_size = 0;
    PreviewCompileKey key;
    bool prepared = false;
    bool compiler = false;
    bool initialized = false;
    bool in_flight = false;
    bool linear = false, repeat = false;
#ifdef XEMU_PREVIEW_VK_TESTING
    bool fail_next_sampler_creation = false;
    bool disable_depth_clamp = false;
    uint32_t pipeline_creation_count = 0;
#endif
    std::string *error = nullptr;

    bool Check(VkResult result, const char *operation)
    {
        if (result == VK_SUCCESS)
            return true;
        *error = std::string("Preview Vulkan ") + operation + " failed (" +
                 std::to_string(result) + ")";
        return false;
    }
    bool Init()
    {
        if (initialized)
            return true;
#ifdef _WIN32
        loader = SDL_LoadObject("vulkan-1.dll");
#else
        loader = SDL_LoadObject("libvulkan.so.1");
#endif
        if (!loader) {
            *error = "Preview Vulkan loader unavailable";
            return false;
        }
        auto get = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
            SDL_LoadFunction(loader, "vkGetInstanceProcAddr"));
        if (!get) {
            *error = "Preview Vulkan entry point unavailable";
            return false;
        }
        auto create = reinterpret_cast<PFN_vkCreateInstance>(
            get(VK_NULL_HANDLE, "vkCreateInstance"));
        VkApplicationInfo app{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
        app.pApplicationName = "xemu synthetic shader preview";
        app.apiVersion = VK_API_VERSION_1_0;
        VkInstanceCreateInfo ci{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
        ci.pApplicationInfo = &app;
        if (!create || !Check(create(&ci, nullptr, &instance), "instance"))
            return false;
#define LOAD_INSTANCE(name)                                               \
    api.name = reinterpret_cast<PFN_vk##name>(get(instance, "vk" #name)); \
    if (!api.name) {                                                      \
        *error = "Missing Vulkan " #name;                                 \
        return false;                                                     \
    }
        PREVIEW_VK_INSTANCE_FUNCTIONS(LOAD_INSTANCE)
#undef LOAD_INSTANCE
        uint32_t count = 0;
        if (!Check(api.EnumeratePhysicalDevices(instance, &count, nullptr),
                   "devices"))
            return false;
        if (!count) {
            *error = "Preview Vulkan physical device unavailable";
            return false;
        }
        std::vector<VkPhysicalDevice> devices(count);
        if (!Check(
                api.EnumeratePhysicalDevices(instance, &count, devices.data()),
                "devices"))
            return false;
        uint32_t family = 0;
        for (auto candidate : devices) {
            uint32_t n = 0;
            api.GetPhysicalDeviceQueueFamilyProperties(candidate, &n, nullptr);
            std::vector<VkQueueFamilyProperties> families(n);
            api.GetPhysicalDeviceQueueFamilyProperties(candidate, &n,
                                                       families.data());
            for (uint32_t i = 0; i < n; ++i) {
                if (families[i].queueCount &&
                    (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                    physical = candidate;
                    family = i;
                    timestamp_valid_bits = families[i].timestampValidBits;
                    break;
                }
            }
            if (physical)
                break;
        }
        if (!physical) {
            *error = "Preview Vulkan graphics queue unavailable";
            return false;
        }
        VkFormatProperties format{};
        api.GetPhysicalDeviceFormatProperties(
            physical, VK_FORMAT_R8G8B8A8_UNORM, &format);
        if (!(format.optimalTilingFeatures &
              VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) ||
            !(format.optimalTilingFeatures &
              VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
            *error = "Preview Vulkan RGBA8 format unavailable";
            return false;
        }
        for (VkFormat candidate : { VK_FORMAT_D32_SFLOAT,
                                    VK_FORMAT_D24_UNORM_S8_UINT }) {
            api.GetPhysicalDeviceFormatProperties(physical, candidate,
                                                  &format);
            if (format.optimalTilingFeatures &
                VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
                depth_format = candidate;
                break;
            }
        }
        if (depth_format == VK_FORMAT_UNDEFINED) {
            unsupported_depth = true;
            *error = "Unsupported preview Vulkan depth attachment format";
            return false;
        }
        float priority = 0.0f;
        VkPhysicalDeviceFeatures features{};
        api.GetPhysicalDeviceFeatures(physical, &features);
        VkPhysicalDeviceFeatures enabled_features{};
        enabled_features.geometryShader = features.geometryShader;
        enabled_features.depthClamp = features.depthClamp;
#ifdef XEMU_PREVIEW_VK_TESTING
        if (disable_depth_clamp)
            enabled_features.depthClamp = VK_FALSE;
#endif
        depth_clamp_supported = enabled_features.depthClamp;
        enabled_features.shaderTessellationAndGeometryPointSize =
            features.shaderTessellationAndGeometryPointSize;
        geometry_supported = features.geometryShader;
        VkPhysicalDeviceProperties properties{};
        api.GetPhysicalDeviceProperties(physical, &properties);
        timestamp_period_ns = properties.limits.timestampPeriod;
        uniform_alignment = std::max<uint64_t>(
            1, properties.limits.minUniformBufferOffsetAlignment);
        max_push_bytes = properties.limits.maxPushConstantsSize;
        VkDeviceQueueCreateInfo qi{
            VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO
        };
        qi.queueFamilyIndex = family;
        qi.queueCount = 1;
        qi.pQueuePriorities = &priority;
        VkDeviceCreateInfo di{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        di.queueCreateInfoCount = 1;
        di.pQueueCreateInfos = &qi;
        di.pEnabledFeatures = &enabled_features;
        if (!Check(api.CreateDevice(physical, &di, nullptr, &device), "device"))
            return false;
#define LOAD_DEVICE(name)                           \
    api.name = reinterpret_cast<PFN_vk##name>(      \
        api.GetDeviceProcAddr(device, "vk" #name)); \
    if (!api.name) {                                \
        *error = "Missing Vulkan " #name;           \
        return false;                               \
    }
        PREVIEW_VK_DEVICE_FUNCTIONS(LOAD_DEVICE)
#undef LOAD_DEVICE
        api.GetDeviceQueue(device, family, 0, &queue);
        api.GetPhysicalDeviceMemoryProperties(physical, &memory);
        VkCommandPoolCreateInfo pi{
            VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO
        };
        pi.queueFamilyIndex = family;
        pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        if (!Check(api.CreateCommandPool(device, &pi, nullptr, &pool),
                   "command pool"))
            return false;
        VkCommandBufferAllocateInfo ai{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO
        };
        ai.commandPool = pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        if (!Check(api.AllocateCommandBuffers(device, &ai, &cmd),
                   "command buffer"))
            return false;
        VkFenceCreateInfo fi{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        if (!Check(api.CreateFence(device, &fi, nullptr, &fence), "fence"))
            return false;
        compiler = glslang_initialize_process();
        if (!compiler) {
            *error = "Preview Vulkan compiler initialization failed";
            return false;
        }
        initialized = true;
        return true;
    }
    bool Allocate(const VkMemoryRequirements &req, VkMemoryPropertyFlags flags,
                  VkDeviceMemory *out)
    {
        for (uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
            if ((req.memoryTypeBits & (1U << i)) &&
                (memory.memoryTypes[i].propertyFlags & flags) == flags) {
                VkMemoryAllocateInfo ai{
                    VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO
                };
                ai.allocationSize = req.size;
                ai.memoryTypeIndex = i;
                return Check(api.AllocateMemory(device, &ai, nullptr, out),
                             "memory");
            }
        }
        *error = "Preview Vulkan compatible memory unavailable";
        return false;
    }
    bool MakeBuffer(Buffer &buffer, size_t size, VkBufferUsageFlags usage)
    {
        VkBufferCreateInfo ci{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        ci.size = size;
        ci.usage = usage;
        if (!Check(api.CreateBuffer(device, &ci, nullptr, &buffer.handle),
                   "buffer"))
            return false;
        VkMemoryRequirements req{};
        api.GetBufferMemoryRequirements(device, buffer.handle, &req);
        return Allocate(req,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                        &buffer.memory) &&
               Check(api.BindBufferMemory(device, buffer.handle, buffer.memory,
                                          0),
                     "buffer binding") &&
               Check(api.MapMemory(device, buffer.memory, 0, VK_WHOLE_SIZE, 0,
                                   &buffer.mapped),
                     "mapping");
    }
    bool MakeImage(Image &image, uint32_t width, uint32_t height,
                   VkImageUsageFlags usage, bool cube = false,
                   VkFormat format = VK_FORMAT_R8G8B8A8_UNORM,
                   VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                   VkComponentMapping components = {})
    {
        VkImageCreateInfo ci{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.format = format;
        ci.extent = { width, height, 1 };
        ci.mipLevels = 1;
        ci.arrayLayers = cube ? 6 : 1;
        ci.flags = cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.usage = usage;
        if (!Check(api.CreateImage(device, &ci, nullptr, &image.handle),
                   "image"))
            return false;
        VkMemoryRequirements req{};
        api.GetImageMemoryRequirements(device, image.handle, &req);
        if (!Allocate(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                      &image.memory) ||
            !Check(api.BindImageMemory(device, image.handle, image.memory, 0),
                   "image binding"))
            return false;
        VkImageViewCreateInfo vi{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        vi.image = image.handle;
        vi.viewType = cube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
        vi.format = ci.format;
        vi.components = components;
        vi.subresourceRange = { aspect, 0, 1, 0,
                                cube ? 6U : 1U };
        return Check(api.CreateImageView(device, &vi, nullptr, &image.view),
                     "image view");
    }
    void Destroy(Buffer &b)
    {
        if (b.mapped)
            api.UnmapMemory(device, b.memory);
        if (b.handle)
            api.DestroyBuffer(device, b.handle, nullptr);
        if (b.memory)
            api.FreeMemory(device, b.memory, nullptr);
        b = {};
    }
    void Destroy(Image &i)
    {
        if (i.view)
            api.DestroyImageView(device, i.view, nullptr);
        if (i.handle)
            api.DestroyImage(device, i.handle, nullptr);
        if (i.memory)
            api.FreeMemory(device, i.memory, nullptr);
        i = {};
    }
    void ClearProgram()
    {
        prepared = false;
        if (!device)
            return;
        for (VkPipeline &pipeline : pipelines) {
            if (pipeline) api.DestroyPipeline(device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }
        if (reference_pipeline)
            api.DestroyPipeline(device, reference_pipeline, nullptr);
        for (VkShaderModule &module : selected_modules) {
            if (module) api.DestroyShaderModule(device, module, nullptr);
            module = VK_NULL_HANDLE;
        }
        if (geometry_module)
            api.DestroyShaderModule(device, geometry_module, nullptr);
        geometry_module = VK_NULL_HANDLE;
        if (layout)
            api.DestroyPipelineLayout(device, layout, nullptr);
        if (descriptor_pool)
            api.DestroyDescriptorPool(device, descriptor_pool, nullptr);
        if (set_layout)
            api.DestroyDescriptorSetLayout(device, set_layout, nullptr);
        if (framebuffer)
            api.DestroyFramebuffer(device, framebuffer, nullptr);
        if (pass)
            api.DestroyRenderPass(device, pass, nullptr);
        if (sampler)
            api.DestroySampler(device, sampler, nullptr);
        reference_pipeline = VK_NULL_HANDLE;
        layout = VK_NULL_HANDLE;
        descriptor_pool = VK_NULL_HANDLE;
        set = VK_NULL_HANDLE;
        set_layout = VK_NULL_HANDLE;
        framebuffer = VK_NULL_HANDLE;
        pass = VK_NULL_HANDLE;
        sampler = VK_NULL_HANDLE;
        Destroy(vertices);
        for (auto &buffer : raw_vertices)
            Destroy(buffer);
        Destroy(raw_indices);
        Destroy(seed_upload);
        Destroy(uniform);
        Destroy(upload);
        Destroy(readback);
        Destroy(target);
        Destroy(depth);
        for (auto &texture : textures)
            Destroy(texture);
        for (auto &captured_sampler : captured_samplers) {
            if (captured_sampler)
                api.DestroySampler(device, captured_sampler, nullptr);
            captured_sampler = VK_NULL_HANDLE;
        }
        material_active = material_uploaded = false;
        texture_widths.fill(8);
        texture_texel_bytes.fill(4);
        texture_heights.fill(8);
        cubes.fill(false);
        uniforms.clear();
        bindings.clear();
        uniform_size = 0;
        uniform_binding_offset.fill(0);
        uniform_binding_size.fill(0);
        raw_bindings.clear();
        raw_attributes.clear();
        push_data.clear();
        original_pipeline = pipeline_uploaded = false;
    }
    ~Impl()
    {
        // Only this private worker waits at teardown; never a game queue.
        if (device && api.DeviceWaitIdle)
            api.DeviceWaitIdle(device);
        ClearProgram();
        if (timestamp_pool)
            api.DestroyQueryPool(device, timestamp_pool, nullptr);
        if (fence)
            api.DestroyFence(device, fence, nullptr);
        if (pool)
            api.DestroyCommandPool(device, pool, nullptr);
        if (device && api.DestroyDevice)
            api.DestroyDevice(device, nullptr);
        if (instance && api.DestroyInstance)
            api.DestroyInstance(instance, nullptr);
        if (loader)
            SDL_UnloadObject(loader);
        if (compiler)
            glslang_finalize_process();
    }
    bool Reflect(const std::vector<uint32_t> &vert,
                 const std::vector<uint32_t> &frag)
    {
        SpvReflectShaderModule v{}, f{};
        if (spvReflectCreateShaderModule(vert.size() * 4, vert.data(), &v) !=
            SPV_REFLECT_RESULT_SUCCESS)
            return false;
        if (spvReflectCreateShaderModule(frag.size() * 4, frag.data(), &f) !=
            SPV_REFLECT_RESULT_SUCCESS) {
            spvReflectDestroyShaderModule(&v);
            return false;
        }
        bool ok = [&] {
            if (v.descriptor_binding_count || v.push_constant_block_count ||
                f.push_constant_block_count || f.descriptor_binding_count > 5)
                return false;
            // Fixed partner inputs and matching fragment varyings only.
            for (uint32_t i = 0; i < f.input_variable_count; ++i) {
                const auto &input = *f.input_variables[i];
                if (input.decoration_flags & SPV_REFLECT_DECORATION_BUILT_IN) {
                    if (input.built_in != SpvBuiltInFragCoord &&
                        input.built_in != SpvBuiltInFrontFacing)
                        return false;
                    continue;
                }
                // SPIRV-Reflect uses UINT32_MAX for an absent Component
                // decoration, whose Vulkan interface value is zero.
                if (input.component != 0 && input.component != UINT32_MAX)
                    return false;
                bool match = false;
                for (uint32_t j = 0; j < v.output_variable_count; ++j) {
                    const auto &output = *v.output_variables[j];
                    if (input.location == output.location &&
                        (output.component == 0 ||
                         output.component == UINT32_MAX) &&
                        input.format == output.format &&
                        input.array.dims_count == 0 &&
                        output.array.dims_count == 0 &&
                        (input.decoration_flags &
                         SPV_REFLECT_DECORATION_FLAT) ==
                            (output.decoration_flags &
                             SPV_REFLECT_DECORATION_FLAT))
                        match = true;
                }
                if (!match)
                    return false;
            }
            for (uint32_t i = 0; i < f.output_variable_count; ++i) {
                const auto &out = *f.output_variables[i];
                if (out.decoration_flags & SPV_REFLECT_DECORATION_BUILT_IN) {
                    if (out.built_in != SpvBuiltInFragDepth)
                        return false;
                    continue;
                }
                if (out.location != 0 ||
                    (out.component != 0 && out.component != UINT32_MAX) ||
                    out.format != SPV_REFLECT_FORMAT_R32G32B32A32_SFLOAT ||
                    out.array.dims_count)
                    return false;
            }
            for (uint32_t i = 0; i < f.descriptor_binding_count; ++i) {
                const auto &b = f.descriptor_bindings[i];
                if (b.set != 0 || b.count != 1 || b.array.dims_count ||
                    b.binding > 31)
                    return false;
                VkDescriptorSetLayoutBinding binding{};
                binding.binding = b.binding;
                binding.descriptorCount = 1;
                binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
                if (b.descriptor_type ==
                    SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
                    if (!b.type_description ||
                        !(b.type_description->type_flags &
                          SPV_REFLECT_TYPE_FLAG_FLOAT) ||
                        (b.image.dim != SpvDim2D &&
                         b.image.dim != SpvDimCube) ||
                        b.image.arrayed || b.image.ms || b.image.depth ||
                        !b.name ||
                        (std::strcmp(b.name, "texSamp0") &&
                         std::strcmp(b.name, "texSamp1") &&
                         std::strcmp(b.name, "texSamp2") &&
                         std::strcmp(b.name, "texSamp3")))
                        return false;
                    const unsigned stage = b.name[7] - '0';
                    cubes[stage] = b.image.dim == SpvDimCube;
                    binding_stage[b.binding] = stage;
                    binding.descriptorType =
                        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                } else if (b.descriptor_type ==
                           SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
                    if (uniform_size || b.block.size == 0 ||
                        b.block.size > 4096 || b.block.member_count > 14)
                        return false;
                    uniform_size = b.block.padded_size;
                    if (uniform_size < b.block.size || uniform_size > 4096)
                        return false;
                    for (uint32_t m = 0; m < b.block.member_count; ++m) {
                        const auto &u = b.block.members[m];
                        if (!u.name || u.member_count ||
                            u.numeric.scalar.width != 32 ||
                            u.array.dims_count > 1)
                            return false;
                        const std::string name(u.name);
                        uint32_t count =
                            u.array.dims_count ? u.array.dims[0] : 1;
                        uint32_t components =
                            std::max(1U, u.numeric.vector.component_count);
                        uint32_t expected_count = 1, expected_components = 1;
                        bool integer = false, unsigned_integer = false,
                             matrix = false;
                        if (name == "alphaRef")
                            integer = true;
                        else if (name == "clipRange" || name == "fogColor")
                            expected_components = 4;
                        else if (name == "clipRegion") {
                            integer = true;
                            expected_count = 8;
                            expected_components = 4;
                        } else if (name == "surfaceScale") {
                            integer = true;
                            expected_components = 2;
                        } else if (name == "consts") {
                            expected_count = 18;
                            expected_components = 4;
                        } else if (name == "texScale" || name == "bumpOffset" ||
                                   name == "bumpScale")
                            expected_count = 4;
                        else if (name == "colorKey" || name == "colorKeyMask") {
                            integer = true;
                            unsigned_integer = true;
                            expected_count = 4;
                        } else if (name == "bumpMat") {
                            matrix = true;
                            expected_count = 4;
                            expected_components = 2;
                        } else if (name != "depthFactor" &&
                                   name != "depthOffset")
                            return false;
                        const auto flags = u.type_description ?
                                               u.type_description->type_flags :
                                               0;
                        if (count != expected_count ||
                            components != expected_components ||
                            (integer ?
                                 !(flags & SPV_REFLECT_TYPE_FLAG_INT) :
                                 !(flags & SPV_REFLECT_TYPE_FLAG_FLOAT)) ||
                            (integer &&
                             u.numeric.scalar.signedness == unsigned_integer) ||
                            (matrix ? (u.numeric.matrix.column_count != 2 ||
                                       u.numeric.matrix.row_count != 2) :
                                      u.numeric.matrix.column_count != 0))
                            return false;
                        const uint32_t stride = count > 1 ? u.array.stride : 0;
                        uint32_t matrix_stride = 0;
                        if (matrix && (!b.block.type_description ||
                                       !MatrixColumnStride(
                                           frag, b.block.type_description->id,
                                           m, &matrix_stride)))
                            return false;
                        const uint32_t element =
                            matrix ? matrix_stride * 2 : components * 4;
                        if ((count > 1 && stride < element) ||
                            u.offset > uniform_size ||
                            (count - 1) * stride + element >
                                uniform_size - u.offset)
                            return false;
                        uniforms.push_back(
                            { name, u.offset, count, stride,
                              matrix ? 4U : components,
                              matrix           ? XEMU_SHADER_DRAW_UNIFORM_MAT2 :
                              unsigned_integer ? XEMU_SHADER_DRAW_UNIFORM_UINT :
                              integer          ? XEMU_SHADER_DRAW_UNIFORM_INT :
                                                 XEMU_SHADER_DRAW_UNIFORM_FLOAT,
                              matrix_stride });
                    }
                    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                } else
                    return false;
                bindings.push_back(binding);
            }
            return true;
        }();
        spvReflectDestroyShaderModule(&v);
        spvReflectDestroyShaderModule(&f);
        if (!ok)
            *error = "Unsupported Vulkan synthetic interface (varyings, "
                     "output, descriptor, or uniform block)";
        return ok;
    }
    bool ReflectOriginal(const PreviewCapturedPipeline &pipeline,
                         const std::vector<uint32_t> &vert,
                         const std::vector<uint32_t> &frag,
                         const std::vector<uint32_t> &geom)
    {
        std::array<SpvReflectShaderModule, 3> modules{};
        const std::vector<uint32_t> *words[] = { &vert, &frag, &geom };
        auto cleanup = [&] {
            for (size_t i = 0; i < 3; ++i)
                if (modules[i]._internal)
                    spvReflectDestroyShaderModule(&modules[i]);
        };
        for (size_t i = 0; i < 3; ++i) {
            if (words[i]->empty())
                continue;
            if (spvReflectCreateShaderModule(words[i]->size() * 4,
                                             words[i]->data(), &modules[i]) !=
                SPV_REFLECT_RESULT_SUCCESS) {
                cleanup();
                return false;
            }
        }
        bool ok = [&] {
            if (!geom.empty()) {
                const bool adjacency =
                    pipeline.host_topology ==
                        VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY ||
                    pipeline.host_topology ==
                        VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY;
                const SpvExecutionMode required =
                    adjacency ? SpvExecutionModeInputLinesAdjacency :
                                SpvExecutionModeTriangles;
                const auto *geometry =
                    spvReflectGetEntryPoint(&modules[2], "main");
                if (!geometry || !geometry->execution_modes ||
                    !std::any_of(geometry->execution_modes,
                                 geometry->execution_modes +
                                     geometry->execution_mode_count,
                                 [&](SpvExecutionMode mode) {
                                     return mode == required;
                                 })) {
                    *error = "Captured host topology and geometry input stage "
                             "disagree";
                    return false;
                }
            }
            for (uint32_t i = 0; i < modules[0].input_variable_count; ++i) {
                const auto &input = *modules[0].input_variables[i];
                if (input.decoration_flags & SPV_REFLECT_DECORATION_BUILT_IN)
                    continue;
                if (input.location >= 16 || input.array.dims_count ||
                    input.numeric.scalar.width != 32 ||
                    input.numeric.matrix.column_count ||
                    (input.component != 0 && input.component != UINT32_MAX))
                    return false;
                const auto &attribute = pipeline.attributes[input.location];
                if (!attribute.enabled || attribute.stream.name.empty())
                    return false;
                const uint32_t format = attribute.stream.format;
                const uint32_t numeric_class =
                    format >= 98 && format <= 109 ? (format - 98) % 3 : 2;
                if (!input.type_description ||
                    (numeric_class == 2 ?
                         !(input.type_description->type_flags &
                           SPV_REFLECT_TYPE_FLAG_FLOAT) :
                         !(input.type_description->type_flags &
                           SPV_REFLECT_TYPE_FLAG_INT) ||
                             bool(input.numeric.scalar.signedness) !=
                                 (numeric_class == 1)))
                    return false;
                VkFormatProperties properties{};
                api.GetPhysicalDeviceFormatProperties(
                    physical, VkFormat(attribute.stream.format), &properties);
                if (!(properties.bufferFeatures &
                      VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT))
                    return false;
                raw_bindings.push_back({ input.location,
                                         attribute.stream.stride,
                                         VK_VERTEX_INPUT_RATE_VERTEX });
                raw_attributes.push_back({ input.location, input.location,
                                           VkFormat(attribute.stream.format),
                                           0 });
            }
            const auto &producer = geom.empty() ? modules[0] : modules[2];
            for (uint32_t i = 0; i < modules[1].input_variable_count; ++i) {
                const auto &input = *modules[1].input_variables[i];
                if (input.decoration_flags & SPV_REFLECT_DECORATION_BUILT_IN)
                    continue;
                bool match = false;
                for (uint32_t j = 0; j < producer.output_variable_count; ++j) {
                    const auto &output = *producer.output_variables[j];
                    if (input.location == output.location &&
                        input.format == output.format &&
                        !input.array.dims_count && !output.array.dims_count &&
                        (input.component == 0 ||
                         input.component == UINT32_MAX) &&
                        (output.component == 0 ||
                         output.component == UINT32_MAX) &&
                        (input.decoration_flags &
                         SPV_REFLECT_DECORATION_FLAT) ==
                            (output.decoration_flags &
                             SPV_REFLECT_DECORATION_FLAT))
                        match = true;
                }
                if (!match)
                    return false;
            }
            for (uint32_t i = 0; i < modules[1].output_variable_count; ++i) {
                const auto &output = *modules[1].output_variables[i];
                if (output.decoration_flags & SPV_REFLECT_DECORATION_BUILT_IN) {
                    if (output.built_in != SpvBuiltInFragDepth)
                        return false;
                } else if (output.location ||
                           output.format !=
                               SPV_REFLECT_FORMAT_R32G32B32A32_SFLOAT ||
                           output.array.dims_count)
                    return false;
            }
            for (size_t stage = 0; stage < 3; ++stage) {
                const auto &module = modules[stage];
                const uint32_t capture_stage = stage + 1;
                const VkShaderStageFlags vk_stage =
                    stage == 0 ? VK_SHADER_STAGE_VERTEX_BIT :
                    stage == 1 ? VK_SHADER_STAGE_FRAGMENT_BIT :
                                 VK_SHADER_STAGE_GEOMETRY_BIT;
                if (module.descriptor_binding_count > 8 ||
                    module.push_constant_block_count > 1)
                    return false;
                for (uint32_t i = 0; i < module.descriptor_binding_count; ++i) {
                    const auto &b = module.descriptor_bindings[i];
                    if (b.set || b.count != 1 || b.array.dims_count ||
                        b.binding >= 32 ||
                        std::any_of(bindings.begin(), bindings.end(),
                                    [&](const auto &existing) {
                                        return existing.binding == b.binding;
                                    }))
                        return false;
                    VkDescriptorSetLayoutBinding binding{
                        b.binding, VkDescriptorType(b.descriptor_type), 1,
                        vk_stage, nullptr
                    };
                    if (b.descriptor_type ==
                        SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
                        if (stage != 1 || !b.type_description ||
                            !(b.type_description->type_flags &
                              SPV_REFLECT_TYPE_FLAG_FLOAT) ||
                            !b.name || std::strlen(b.name) != 8 ||
                            std::strncmp(b.name, "texSamp", 7) ||
                            b.name[7] < '0' || b.name[7] > '3' ||
                            b.image.arrayed || b.image.ms || b.image.depth ||
                            (b.image.dim != SpvDim2D &&
                             b.image.dim != SpvDimCube))
                            return false;
                        const uint32_t slot = b.name[7] - '0';
                        cubes[slot] = b.image.dim == SpvDimCube;
                        binding_stage[b.binding] = slot;
                    } else if (b.descriptor_type ==
                               SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
                        if (std::count_if(
                                bindings.begin(), bindings.end(),
                                [](const auto &existing) {
                                    return existing.descriptorType ==
                                           VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                                }) >= 4)
                            return false;
                        if (!b.block.size ||
                            b.block.padded_size < b.block.size ||
                            b.block.padded_size > 65536 ||
                            b.block.member_count > 256 ||
                            !b.block.type_description || uniform_size > 262144)
                            return false;
                        const uint32_t base =
                            (uniform_size + uniform_alignment - 1) /
                            uniform_alignment * uniform_alignment;
                        uniform_binding_offset[b.binding] = base;
                        uniform_binding_size[b.binding] = b.block.padded_size;
                        uniform_size = base + b.block.padded_size;
                        for (uint32_t member = 0; member < b.block.member_count;
                             ++member) {
                            const auto &u = b.block.members[member];
                            if (!u.name || u.member_count ||
                                u.array.dims_count > 1 ||
                                u.numeric.scalar.width != 32 ||
                                !u.type_description)
                                return false;
                            if (!(u.type_description->type_flags &
                                  (SPV_REFLECT_TYPE_FLAG_FLOAT |
                                   SPV_REFLECT_TYPE_FLAG_INT)))
                                return false;
                            const uint32_t count =
                                u.array.dims_count ? u.array.dims[0] : 1;
                            uint32_t components = std::max(
                                         1U, u.numeric.vector.component_count),
                                     matrix_stride = 0;
                            uint32_t type =
                                u.type_description->type_flags &
                                        SPV_REFLECT_TYPE_FLAG_INT ?
                                    (u.numeric.scalar.signedness ?
                                         XEMU_SHADER_DRAW_UNIFORM_INT :
                                         XEMU_SHADER_DRAW_UNIFORM_UINT) :
                                    XEMU_SHADER_DRAW_UNIFORM_FLOAT;
                            if (u.numeric.matrix.column_count) {
                                if ((u.numeric.matrix.column_count != 2 &&
                                     u.numeric.matrix.column_count != 4) ||
                                    u.numeric.matrix.row_count !=
                                        u.numeric.matrix.column_count ||
                                    !MatrixColumnStride(
                                        *words[stage],
                                        b.block.type_description->id, member,
                                        &matrix_stride))
                                    return false;
                                if (matrix_stride <
                                    u.numeric.matrix.row_count * 4)
                                    return false;
                                components = u.numeric.matrix.column_count *
                                             u.numeric.matrix.row_count;
                                type = components == 4 ?
                                           XEMU_SHADER_DRAW_UNIFORM_MAT2 :
                                           XEMU_SHADER_DRAW_UNIFORM_MAT4;
                            }
                            const uint32_t element =
                                matrix_stride ?
                                    matrix_stride *
                                        u.numeric.matrix.column_count :
                                    components * 4;
                            const uint32_t stride =
                                count > 1 ? u.array.stride : 0;
                            if (!count || count > 4096 ||
                                (count > 1 && stride < element) ||
                                u.offset > b.block.padded_size ||
                                (count - 1) * stride + element >
                                    b.block.padded_size - u.offset)
                                return false;
                            auto source = std::find_if(
                                pipeline.uniforms.begin(),
                                pipeline.uniforms.end(),
                                [&](const auto &captured) {
                                    return (captured.stage == 0 ||
                                            captured.stage == capture_stage) &&
                                           PreviewCapturedUniformName(
                                               captured) == u.name &&
                                           captured.type == type &&
                                           captured.components == components &&
                                           captured.count >= count;
                                });
                            if (source == pipeline.uniforms.end()) {
                                *error = std::string("Original camera uniform "
                                                     "unavailable: ") +
                                         u.name;
                                return false;
                            }
                            uniforms.push_back({ u.name, base + u.offset, count,
                                                 stride, components, type,
                                                 matrix_stride,
                                                 capture_stage });
                        }
                    } else
                        return false;
                    bindings.push_back(binding);
                }
                if (module.push_constant_block_count) {
                    const auto &block = module.push_constant_blocks[0];
                    if (stage != 0 || block.offset || !block.size ||
                        block.size > max_push_bytes || block.size > 256 ||
                        block.member_count != 1 || !block.members[0].name ||
                        std::strcmp(block.members[0].name, "inlineValue"))
                        return false;
                    const auto &u = block.members[0];
                    if (u.offset || u.numeric.scalar.width != 32 ||
                        u.numeric.vector.component_count != 4 ||
                        !u.type_description ||
                        !(u.type_description->type_flags &
                          SPV_REFLECT_TYPE_FLAG_FLOAT) ||
                        u.array.dims_count != 1 || u.array.stride != 16 ||
                        u.array.dims[0] > 16 ||
                        u.array.dims[0] * 16 != block.size ||
                        uint32_t(__builtin_popcount(
                            pipeline.uniform_attribute_mask)) !=
                            u.array.dims[0])
                        return false;
                    for (size_t slot = 0; slot < 16; ++slot)
                        if ((pipeline.uniform_attribute_mask & (1U << slot)) &&
                            pipeline.attributes[slot].stream.bytes.size() != 16)
                            return false;
                    push_data.resize(block.size);
                }
            }
            return true;
        }();
        cleanup();
        if (!ok && error->empty())
            *error = "Unsupported original camera Vulkan vertex, geometry or "
                     "descriptor interface";
        return ok;
    }
    bool CheckOriginalMaterial(const PreviewPacket &packet)
    {
        if (!packet.captured_pipeline)
            return true;
        for (const auto &uniform : uniforms) {
            const auto &captured = packet.captured_pipeline->uniforms;
            const auto source = std::find_if(
                captured.begin(), captured.end(), [&](const auto &value) {
                    return (value.stage == 0 || value.stage == uniform.stage) &&
                           PreviewCapturedUniformName(value) == uniform.name &&
                           value.type == uniform.type &&
                           value.components == uniform.components &&
                           value.count >= uniform.count;
                });
            if (source == captured.end()) {
                *error = "Original camera uniform unavailable: " + uniform.name;
                return false;
            }
        }
        for (const auto &binding : bindings) {
            if (binding.descriptorType !=
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                continue;
            const uint32_t slot = binding_stage[binding.binding];
            if (!PreviewCapturedTexture(packet, slot, cubes[slot])) {
                *error = "Original camera texture base image is unavailable: "
                         "texSamp" +
                         std::to_string(slot);
                return false;
            }
        }
        return true;
    }
    bool MakeSampler(bool use_linear, bool use_repeat)
    {
        VkSampler next = VK_NULL_HANDLE;
        VkSamplerCreateInfo si{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        si.magFilter = si.minFilter =
            use_linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.addressModeU = si.addressModeV = si.addressModeW =
            use_repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT :
                         VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        VkResult result;
#ifdef XEMU_PREVIEW_VK_TESTING
        if (fail_next_sampler_creation) {
            fail_next_sampler_creation = false;
            result = VK_ERROR_OUT_OF_HOST_MEMORY;
        } else
#endif
        {
            result = api.CreateSampler(device, &si, nullptr, &next);
        }
        if (!Check(result, "sampler"))
            return false;
        // Prepare has no submitted work; Render enters only after the previous
        // fence completed. Keep the live descriptor intact until creation
        // succeeds, then publish the replacement before retiring its sampler.
        VkSampler previous = sampler;
        sampler = next;
        if (set)
            WriteDescriptors();
        linear = use_linear;
        repeat = use_repeat;
        if (previous)
            api.DestroySampler(device, previous, nullptr);
        return true;
    }
    void WriteDescriptors()
    {
        for (const auto &b : bindings) {
            VkDescriptorBufferInfo buffer{ uniform.handle,
                                           uniform_binding_offset[b.binding],
                                           uniform_binding_size[b.binding] ?
                                               uniform_binding_size[b.binding] :
                                               uniform_size };
            VkDescriptorImageInfo image{
                material_active ? captured_samplers[binding_stage[b.binding]] :
                                  sampler,
                textures[binding_stage[b.binding]].view,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            };
            VkWriteDescriptorSet write{
                VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET
            };
            write.dstSet = set;
            write.dstBinding = b.binding;
            write.descriptorCount = 1;
            write.descriptorType = b.descriptorType;
            if (b.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                write.pBufferInfo = &buffer;
            else
                write.pImageInfo = &image;
            api.UpdateDescriptorSets(device, 1, &write, 0, nullptr);
        }
    }
    static VkComponentMapping
    CapturedTextureComponents(const PreviewPacket &packet, size_t slot)
    {
        const auto &mapping =
            packet.captured_material->texture_storage_swizzle[slot];
        auto component = [](uint32_t value) {
            switch (value) {
            case 0:
                return VK_COMPONENT_SWIZZLE_ZERO;
            case 1:
                return VK_COMPONENT_SWIZZLE_ONE;
            case 0x1903:
                return VK_COMPONENT_SWIZZLE_R;
            case 0x1904:
                return VK_COMPONENT_SWIZZLE_G;
            case 0x1905:
                return VK_COMPONENT_SWIZZLE_B;
            default:
                return VK_COMPONENT_SWIZZLE_A;
            }
        };
        return { component(mapping[0]), component(mapping[1]),
                 component(mapping[2]), component(mapping[3]) };
    }
    bool ConfigureCapturedMaterial(const PreviewPacket &packet)
    {
        const bool captured = packet.packet_kind == PreviewPacketKind::Replay &&
                              packet.captured_material;
        if (captured == material_active &&
            (!captured || material_digest == packet.material_digest))
            return true;
        std::array<Image, 4> next_images{};
        std::array<VkSampler, 4> next_samplers{};
        std::array<uint32_t, 4> next_widths{}, next_heights{},
            next_texel_bytes{};
        std::array<size_t, 4> next_offsets{};
        Buffer next_upload;
        auto cleanup = [&] {
            for (auto &image : next_images)
                Destroy(image);
            for (auto handle : next_samplers)
                if (handle)
                    api.DestroySampler(device, handle, nullptr);
            Destroy(next_upload);
        };
        size_t bytes = 0;
        for (size_t slot = 0; slot < 4; ++slot) {
            const auto *texture =
                PreviewCapturedTexture(packet, slot, cubes[slot]);
            next_widths[slot] = texture ? texture->metadata.width : 8;
            next_heights[slot] = texture ? texture->metadata.height : 8;
            const auto *storage = PreviewCapturedTextureStorage(packet, slot);
            next_texel_bytes[slot] = storage ? 2 : 4;
            bytes = (bytes + 3) & ~size_t(3); // VkBufferImageCopy alignment
            next_offsets[slot] = bytes;
            bytes += size_t(next_widths[slot]) * next_heights[slot] *
                     next_texel_bytes[slot] *
                     (captured ? (cubes[slot] ? 6 : 1) : 6);
            if (!MakeImage(
                    next_images[slot], next_widths[slot], next_heights[slot],
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                        VK_IMAGE_USAGE_SAMPLED_BIT,
                    cubes[slot],
                    storage ? VK_FORMAT_R16_UNORM : VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    storage ? CapturedTextureComponents(packet, slot) :
                              VkComponentMapping{})) {
                cleanup();
                return false;
            }
            if (captured) {
                VkSamplerCreateInfo info{
                    VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO
                };
                info.magFilter = VK_FILTER_NEAREST;
                info.minFilter = VK_FILTER_NEAREST;
                info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
                info.addressModeU = info.addressModeV = info.addressModeW =
                    VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                if (texture) {
                    const auto &meta = texture->metadata;
                    // These capture fields use the common GL filter/wrap
                    // values.
                    info.magFilter = meta.mag_filter == 0x2601 ?
                                         VK_FILTER_LINEAR :
                                         VK_FILTER_NEAREST;
                    info.minFilter = meta.min_filter == 0x2601 ||
                                             meta.min_filter == 0x2701 ||
                                             meta.min_filter == 0x2703 ?
                                         VK_FILTER_LINEAR :
                                         VK_FILTER_NEAREST;
                    auto wrap = [](uint32_t value) {
                        return value == 0x2901 ?
                                   VK_SAMPLER_ADDRESS_MODE_REPEAT :
                               value == 0x8370 ?
                                   VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT :
                                   VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                    };
                    info.addressModeU = wrap(meta.wrap_s);
                    info.addressModeV = wrap(meta.wrap_t);
                    info.addressModeW = wrap(meta.wrap_r);
                }
                if (!Check(api.CreateSampler(device, &info, nullptr,
                                             &next_samplers[slot]),
                           "captured sampler")) {
                    cleanup();
                    return false;
                }
            }
        }
        if (!MakeBuffer(next_upload, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT)) {
            cleanup();
            return false;
        }
        auto previous_images = textures;
        auto previous_samplers = captured_samplers;
        Buffer previous_upload = upload;
        textures = next_images;
        captured_samplers = next_samplers;
        upload = next_upload;
        texture_widths = next_widths;
        texture_texel_bytes = next_texel_bytes;
        texture_heights = next_heights;
        texture_offsets = next_offsets;
        material_active = captured;
        material_uploaded = false;
        material_digest = packet.material_digest;
        WriteDescriptors();
        for (auto &image : previous_images)
            Destroy(image);
        for (auto handle : previous_samplers)
            if (handle)
                api.DestroySampler(device, handle, nullptr);
        Destroy(previous_upload);
        return true;
    }
    bool MakePipeline(const VkShaderModule modules[2],
                      const PreviewRenderState &requested, bool reference,
                      VkPipeline *out)
    {
        const PreviewRenderState state = ClampPreviewRenderState(requested);
        VkPipelineShaderStageCreateInfo stages[3]{};
        for (size_t i = 0; i < 2; ++i) {
            stages[i].sType =
                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[i].stage =
                i ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
            stages[i].module = modules[i];
            stages[i].pName = "main";
        }
        uint32_t stage_count = 2;
        if (!reference && geometry_module) {
            stages[2].sType =
                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[2].stage = VK_SHADER_STAGE_GEOMETRY_BIT;
            stages[2].module = geometry_module;
            stages[2].pName = "main";
            stage_count = 3;
        }
        VkVertexInputBindingDescription vb{ 0, sizeof(Vertex),
                                            VK_VERTEX_INPUT_RATE_VERTEX };
        VkVertexInputAttributeDescription attrs[] = {
            { 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0 },
            { 1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, color) },
            { 2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv) },
            { 3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, colors) },
            { 4, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
              offsetof(Vertex, colors) + 16 },
            { 5, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
              offsetof(Vertex, colors) + 32 },
            { 6, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
              offsetof(Vertex, colors) + 48 },
            { 7, 0, VK_FORMAT_R32_SFLOAT, offsetof(Vertex, fog) },
            { 8, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, direction) },
            { 9, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
              offsetof(Vertex, cube_stages) }
        };
        VkPipelineVertexInputStateCreateInfo vi{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
        };
        vi.vertexBindingDescriptionCount = 1;
        vi.pVertexBindingDescriptions = &vb;
        vi.vertexAttributeDescriptionCount = 10;
        vi.pVertexAttributeDescriptions = attrs;
        if (!reference && original_pipeline) {
            vi.vertexBindingDescriptionCount = raw_bindings.size();
            vi.pVertexBindingDescriptions = raw_bindings.data();
            vi.vertexAttributeDescriptionCount = raw_attributes.size();
            vi.pVertexAttributeDescriptions = raw_attributes.data();
        }
        VkPipelineInputAssemblyStateCreateInfo ia{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO
        };
        ia.topology = !reference && original_pipeline ? native_topology :
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO
        };
        viewport.viewportCount = viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO
        };
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.lineWidth = 1;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.cullMode = reference || state.cull == PreviewCullMode::None ?
            VK_CULL_MODE_NONE : state.cull == PreviewCullMode::Back ?
                VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_FRONT_BIT;
        VkPipelineMultisampleStateCreateInfo ms{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO
        };
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth_state{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO
        };
        depth_state.depthTestEnable = reference || state.depth_test;
        depth_state.depthWriteEnable = reference || state.depth_write;
        depth_state.depthCompareOp = VK_COMPARE_OP_LESS;
        VkPipelineColorBlendAttachmentState blend_attachment{};
        blend_attachment.colorWriteMask = 15;
        if (!reference && state.blend == PreviewBlendMode::Alpha) {
            blend_attachment.blendEnable = VK_TRUE;
            blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            blend_attachment.dstColorBlendFactor =
                VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
            blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blend_attachment.dstAlphaBlendFactor =
                VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
        }
        VkPipelineColorBlendStateCreateInfo blend{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO
        };
        blend.attachmentCount = 1;
        blend.pAttachments = &blend_attachment;
        if (!reference && original_pipeline) {
            const auto &r = native_raster;
            raster.depthClampEnable = r.depth_clamp;
            raster.cullMode = r.cull_mode;
            raster.frontFace = r.front_ccw ? VK_FRONT_FACE_COUNTER_CLOCKWISE :
                                             VK_FRONT_FACE_CLOCKWISE;
            raster.depthBiasEnable = r.depth_bias;
            raster.depthBiasConstantFactor = r.bias_constant;
            raster.depthBiasSlopeFactor = r.bias_slope;
            depth_state.depthTestEnable = r.depth_test;
            depth_state.depthWriteEnable = r.depth_write;
            depth_state.depthCompareOp = VkCompareOp(r.depth_compare);
            depth_state.stencilTestEnable = r.stencil_test;
            auto stencil = [](const PreviewCapturedStencil &s) {
                return VkStencilOpState{ VkStencilOp(s.fail),
                                         VkStencilOp(s.pass),
                                         VkStencilOp(s.depth_fail),
                                         VkCompareOp(s.compare),
                                         s.read_mask,
                                         s.write_mask,
                                         s.reference };
            };
            depth_state.front = stencil(r.front_stencil);
            depth_state.back = stencil(r.back_stencil);
            blend_attachment = {
                VkBool32(r.blend_enabled),  VkBlendFactor(r.src_rgb),
                VkBlendFactor(r.dst_rgb),   VkBlendOp(r.blend_rgb),
                VkBlendFactor(r.src_alpha), VkBlendFactor(r.dst_alpha),
                VkBlendOp(r.blend_alpha),   r.color_write
            };
            std::copy(r.blend_color.begin(), r.blend_color.end(),
                      blend.blendConstants);
        }
        VkDynamicState dynamics[] = { VK_DYNAMIC_STATE_VIEWPORT,
                                      VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamic{
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO
        };
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamics;
        VkGraphicsPipelineCreateInfo pi{
            VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO
        };
        pi.stageCount = stage_count;
        pi.pStages = stages;
        pi.pVertexInputState = &vi;
        pi.pInputAssemblyState = &ia;
        pi.pViewportState = &viewport;
        pi.pRasterizationState = &raster;
        pi.pMultisampleState = &ms;
        pi.pDepthStencilState = &depth_state;
        pi.pColorBlendState = &blend;
        pi.pDynamicState = &dynamic;
        pi.layout = layout;
        pi.renderPass = pass;
#ifdef XEMU_PREVIEW_VK_TESTING
        ++pipeline_creation_count;
#endif
        return Check(api.CreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                                 &pi, nullptr, out),
                     reference ? "reference pipeline" : "target pipeline");
    }
    bool Prepare(const PreviewWorkItem &work, bool *unsupported,
                 bool *cancelled,
                 const std::function<bool()> &may_continue)
    {
        *unsupported = false;
        if (cancelled) *cancelled = false;
        const auto allowed = [&] {
            if (!may_continue || may_continue()) return true;
            if (cancelled) *cancelled = true;
            *error = "Preview Vulkan preparation cancelled because the guest "
                     "resumed";
            return false;
        };
        if (!work.packet ||
            work.packet->selection.backend != PreviewBackend::Vulkan ||
            (work.packet->packet_kind != PreviewPacketKind::Synthetic &&
             work.packet->packet_kind != PreviewPacketKind::Replay) ||
            (!work.packet->captured_pipeline &&
             work.packet->partner_source !=
                 BuildPreviewSyntheticVertexSource(work.packet->source,
                                                   PreviewBackend::Vulkan))) {
            *unsupported = true;
            *error = "Unsupported Vulkan packet or synthetic partner";
            return false;
        }
        if (!PreviewExtentWithinLimits(work.packet->width, work.packet->height,
                                       bool(work.packet->captured_pipeline))) {
            *unsupported = true;
            *error = "Preview Vulkan packet extent exceeds the supported "
                     "captured or synthetic bounds";
            return false;
        }
        if (in_flight) {
            *error = "Preview Vulkan execution owner requires shutdown after "
                     "incomplete work";
            return false;
        }
        if (prepared && key == work.compile_key)
            return true;
        if (!allowed()) return false;
        if (!Init()) {
            if (unsupported_depth) *unsupported = true;
            return false;
        }
        ClearProgram();
        original_pipeline = bool(work.packet->captured_pipeline);
        if (original_pipeline) {
            if (!ValidatePreviewCapturedPipeline(
                    *work.packet->captured_pipeline, error)) {
                *unsupported = true;
                return false;
            }
            native_raster = work.packet->captured_pipeline->raster;
            native_topology = VkPrimitiveTopology(work.packet->captured_pipeline->host_topology);
            if (native_raster.depth_clamp && !depth_clamp_supported) {
                *unsupported = true;
                *error = "Captured raster state requires Vulkan depthClamp "
                         "support";
                return false;
            }
            if (native_raster.stencil_test &&
                depth_format != VK_FORMAT_D24_UNORM_S8_UINT) {
                *unsupported = true;
                *error = "Captured stencil state requires a preview stencil "
                         "attachment";
                return false;
            }
        }
        std::vector<uint32_t> vert, frag, geom;
        if (!Compile(work.packet->partner_source, GLSLANG_STAGE_VERTEX, &vert,
                     error) ||
            !Compile(work.packet->source, GLSLANG_STAGE_FRAGMENT, &frag, error))
            return false;
        if (original_pipeline &&
            !work.packet->captured_pipeline->geometry_source.empty()) {
            if (!geometry_supported) {
                *unsupported = true;
                *error =
                    "Original camera requires Vulkan geometryShader support";
                return false;
            }
            if (!Compile(work.packet->captured_pipeline->geometry_source,
                         GLSLANG_STAGE_GEOMETRY, &geom, error))
                return false;
        }
        if (original_pipeline ?
                !ReflectOriginal(*work.packet->captured_pipeline, vert, frag,
                                 geom) :
                !Reflect(vert, frag)) {
            *unsupported = true;
            return false;
        }
        if (!CheckOriginalMaterial(*work.packet)) {
            *unsupported = true;
            return false;
        }
        VkDescriptorSetLayoutCreateInfo dli{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO
        };
        dli.bindingCount = bindings.size();
        dli.pBindings = bindings.data();
        if (!Check(api.CreateDescriptorSetLayout(device, &dli, nullptr,
                                                 &set_layout),
                   "descriptor layout"))
            return false;
        VkPipelineLayoutCreateInfo li{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO
        };
        li.setLayoutCount = 1;
        li.pSetLayouts = &set_layout;
        VkPushConstantRange push_range{ VK_SHADER_STAGE_VERTEX_BIT, 0,
                                        uint32_t(push_data.size()) };
        if (!push_data.empty()) {
            li.pushConstantRangeCount = 1;
            li.pPushConstantRanges = &push_range;
        }
        if (!Check(api.CreatePipelineLayout(device, &li, nullptr, &layout),
                   "pipeline layout"))
            return false;
        VkAttachmentDescription attachment{};
        attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = original_pipeline ? VK_ATTACHMENT_LOAD_OP_LOAD :
                                                VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout =
            original_pipeline ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL :
                                VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        VkAttachmentDescription depth_attachment{};
        depth_attachment.format = depth_format;
        depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth_attachment.finalLayout =
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkAttachmentDescription attachments[] = { attachment, depth_attachment };
        VkAttachmentReference reference{
            0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        };
        VkAttachmentReference depth_reference{
            1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
        };
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &reference;
        subpass.pDepthStencilAttachment = &depth_reference;
        VkSubpassDependency dependencies[2]{};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dependencies[0].dstStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependencies[0].dstAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        VkRenderPassCreateInfo ri{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
        ri.attachmentCount = 2;
        ri.pAttachments = attachments;
        ri.subpassCount = 1;
        ri.pSubpasses = &subpass;
        ri.dependencyCount = 2;
        ri.pDependencies = dependencies;
        if (!Check(api.CreateRenderPass(device, &ri, nullptr, &pass),
                   "render pass"))
            return false;
        for (size_t i = 0; i < 2; ++i) {
            const auto &words = i ? frag : vert;
            VkShaderModuleCreateInfo mi{
                VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO
            };
            mi.codeSize = words.size() * 4;
            mi.pCode = words.data();
            if (!Check(api.CreateShaderModule(device, &mi, nullptr,
                                              &selected_modules[i]),
                       "selected shader module"))
                return false;
        }
        if (!geom.empty()) {
            VkShaderModuleCreateInfo mi{
                VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO
            };
            mi.codeSize = geom.size() * 4;
            mi.pCode = geom.data();
            if (!Check(api.CreateShaderModule(device, &mi, nullptr,
                                              &geometry_module),
                       "geometry shader module"))
                return false;
        }
        constexpr const char *reference_vertex =
            "#version 450\nlayout(location=0) in vec4 position;\n"
            "layout(location=1) in vec4 vertexColor;\n"
            "layout(location=0) out vec4 referenceColor;\n"
            "void main(){gl_Position=position;referenceColor=vertexColor;}\n";
        constexpr const char *reference_fragment =
            "#version 450\nlayout(location=0) in vec4 referenceColor;\n"
            "layout(location=0) out vec4 color;\n"
            "void main(){color=referenceColor;}\n";
        std::vector<uint32_t> reference_vert, reference_frag;
        if (!Compile(reference_vertex, GLSLANG_STAGE_VERTEX, &reference_vert,
                     error) ||
            !Compile(reference_fragment, GLSLANG_STAGE_FRAGMENT,
                     &reference_frag, error))
            return false;
        VkShaderModule reference_modules[2]{};
        for (size_t i = 0; i < 2; ++i) {
            const auto &words = i ? reference_frag : reference_vert;
            VkShaderModuleCreateInfo mi{
                VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO
            };
            mi.codeSize = words.size() * 4;
            mi.pCode = words.data();
            if (!Check(api.CreateShaderModule(device, &mi, nullptr,
                                              &reference_modules[i]),
                       "reference shader module")) {
                if (reference_modules[0])
                    api.DestroyShaderModule(device, reference_modules[0],
                                            nullptr);
                return false;
            }
        }
        if (!allowed()) {
            for (VkShaderModule module : reference_modules)
                if (module) api.DestroyShaderModule(device, module, nullptr);
            return false;
        }
        const bool pipelines_ok =
            MakePipeline(reference_modules, {}, true, &reference_pipeline);
        for (VkShaderModule module : reference_modules)
            api.DestroyShaderModule(device, module, nullptr);
        if (!pipelines_ok)
            return false;
        // The finite blend/depth/cull state space is built while the guest is
        // paused. Render only selects a handle; no driver pipeline compile can
        // occur when the user changes these controls during gameplay.
        for (unsigned blend = 0; blend < 2; ++blend)
            for (unsigned depth_test = 0; depth_test < 2; ++depth_test)
                for (unsigned depth_write = 0; depth_write < 2; ++depth_write)
                    for (unsigned cull = 0; cull < 3; ++cull) {
                        PreviewRenderState variant{};
                        variant.blend = static_cast<PreviewBlendMode>(blend);
                        variant.depth_test = depth_test;
                        variant.depth_write = depth_write;
                        variant.cull = static_cast<PreviewCullMode>(cull);
                        if (!allowed()) return false;
                        if (!MakePipeline(selected_modules, variant, false,
                                          &pipelines[PipelineVariantIndex(variant)]))
                            return false;
                    }
        if (!allowed()) return false;
        // Extent is a result input, so a cached program must also handle a
        // larger render without recreating its framebuffer or transfer buffers.
        const uint32_t target_width =
            original_pipeline ? kPreviewMaxCapturedWidth : kPreviewMaxWidth;
        const uint32_t target_height =
            original_pipeline ? kPreviewMaxCapturedHeight : kPreviewMaxHeight;
        const VkDeviceSize target_bytes =
            VkDeviceSize(target_width) * target_height * 4;
        if (!MakeBuffer(vertices, kPreviewMaxSceneVertices * sizeof(Vertex),
                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) ||
            !MakeBuffer(upload, kPreviewFixtureTextureBytes,
                        VK_BUFFER_USAGE_TRANSFER_SRC_BIT) ||
            !MakeBuffer(readback, target_bytes,
                        VK_BUFFER_USAGE_TRANSFER_DST_BIT) ||
            (uniform_size && !MakeBuffer(uniform, uniform_size,
                                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)) ||
            !MakeImage(target, target_width, target_height,
                       VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                           VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                           VK_IMAGE_USAGE_TRANSFER_DST_BIT) ||
            !MakeImage(depth, target_width, target_height,
                       VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, false,
                       depth_format,
                       depth_format == VK_FORMAT_D24_UNORM_S8_UINT ?
                           VK_IMAGE_ASPECT_DEPTH_BIT |
                               VK_IMAGE_ASPECT_STENCIL_BIT :
                           VK_IMAGE_ASPECT_DEPTH_BIT) ||
            !MakeSampler(false, false))
            return false;
        for (unsigned i = 0; i < 4; ++i)
            if (!MakeImage(textures[i], 8, 8,
                           VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                               VK_IMAGE_USAGE_SAMPLED_BIT,
                           cubes[i]))
                return false;
        if (original_pipeline) {
            const auto &pipeline = *work.packet->captured_pipeline;
            if (!MakeBuffer(seed_upload, target_bytes,
                            VK_BUFFER_USAGE_TRANSFER_SRC_BIT))
                return false;
            for (const auto &attribute : raw_attributes) {
                const auto &stream =
                    pipeline.attributes[attribute.location].stream;
                const size_t bytes =
                    size_t(pipeline.first_vertex) * stream.stride +
                    stream.bytes.size();
                if (!MakeBuffer(raw_vertices[attribute.location], bytes,
                                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT))
                    return false;
            }
            if (!pipeline.indices.empty() &&
                !MakeBuffer(raw_indices, pipeline.indices.size() * 4,
                            VK_BUFFER_USAGE_INDEX_BUFFER_BIT))
                return false;
        }
        VkFramebufferCreateInfo fi{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        fi.renderPass = pass;
        VkImageView views[] = { target.view, depth.view };
        fi.attachmentCount = 2;
        fi.pAttachments = views;
        fi.width = target_width;
        fi.height = target_height;
        fi.layers = 1;
        if (!Check(api.CreateFramebuffer(device, &fi, nullptr, &framebuffer),
                   "framebuffer"))
            return false;
        VkDescriptorPoolSize sizes[] = {
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, original_pipeline ? 4U : 1U },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 }
        };
        VkDescriptorPoolCreateInfo dpi{
            VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO
        };
        dpi.maxSets = 1;
        dpi.poolSizeCount = 2;
        dpi.pPoolSizes = sizes;
        if (!Check(api.CreateDescriptorPool(device, &dpi, nullptr,
                                            &descriptor_pool),
                   "descriptor pool"))
            return false;
        VkDescriptorSetAllocateInfo dai{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO
        };
        dai.descriptorPool = descriptor_pool;
        dai.descriptorSetCount = 1;
        dai.pSetLayouts = &set_layout;
        if (!Check(api.AllocateDescriptorSets(device, &dai, &set),
                   "descriptor set"))
            return false;
        WriteDescriptors();
        if (!allowed()) return false;
        key = work.compile_key;
        prepared = true;
        return true;
    }
    void Barrier(VkImage image, VkImageLayout old_layout,
                 VkImageLayout new_layout, VkAccessFlags src, VkAccessFlags dst,
                 VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage,
                 uint32_t layers = 1)
    {
        VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        barrier.srcAccessMask = src;
        barrier.dstAccessMask = dst;
        barrier.oldLayout = old_layout;
        barrier.newLayout = new_layout;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex =
            VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0,
                                     layers };
        api.CmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0,
                               nullptr, 1, &barrier);
    }
    bool Render(const PreviewWorkItem &work, const std::atomic<bool> &stop,
                std::vector<uint8_t> *rgba)
    {
        if (!prepared || !work.packet || work.slot >= kPreviewSlotCount ||
            key != work.compile_key || in_flight ||
            !PreviewExtentWithinLimits(work.packet->width, work.packet->height,
                                       bool(work.packet->captured_pipeline))) {
            *error = "Preview Vulkan preview is not prepared for these inputs";
            return false;
        }
        const auto &packet = *work.packet;
        auto &initial_timing = draw_queries[work.slot];
        initial_timing.pending = false;
        initial_timing.timing = {};
        initial_timing.timing.result = work.result_key;
        initial_timing.timing.backend = PreviewBackend::Vulkan;
        if (packet.profile_draw) {
            initial_timing.timing.status = PreviewDrawTimingStatus::Unsupported;
            initial_timing.timing.provenance =
                packet.packet_kind == PreviewPacketKind::Replay ?
                    PreviewDrawTimingProvenance::ReplayInstrumented :
                    PreviewDrawTimingProvenance::SelectedPreviewInstrumented;
            initial_timing.timing.message =
                "This channel emits no GPU draw interval";
        }
        if (packet.captured_pipeline &&
            !ValidatePreviewCapturedPipeline(*packet.captured_pipeline, error))
            return false;
        if (packet.captured_pipeline &&
            !packet.captured_pipeline->color_before.rgba.empty() &&
            (packet.captured_pipeline->color_before.width != packet.width ||
             packet.captured_pipeline->color_before.height != packet.height)) {
            *error =
                "Owned before destination does not match native preview extent";
            return false;
        }
        if (!CheckOriginalMaterial(packet))
            return false;
        PreviewSyntheticFixture fixture{};
        if (!DecodePreviewSyntheticFixture(packet.fixture_bytes, &fixture,
                                           error))
            return false;
        ApplyPreviewDeclaredBindings(&fixture, packet,
                                     work.result_key.time_seconds);
        if (!PreviewChannelAvailable(work.result_key.channel)) {
            *error = PreviewChannelProvenance(work.result_key.channel);
            return false;
        }
        if (PreviewChannelIsDiagnostic(work.result_key.channel)) {
            if (packet.captured_pipeline) {
                *error = "Native captured pipeline cannot use synthetic "
                         "fixture diagnostic channels";
                return false;
            }
            return RenderPreviewDiagnostic(work.result_key.channel, fixture,
                                           packet.width, packet.height, rgba,
                                           error);
        }
        const PreviewRenderState state =
            ClampPreviewRenderState(packet.render_state);
        if (!original_pipeline && !AdmitPreviewBakedAlphaTest(packet, error))
            return false;
        if (!ConfigureCapturedMaterial(packet))
            return false;
        std::string material_status;
        const bool captured = material_active;
        if (captured)
            material_status =
                DescribePreviewCapturedMaterial(*packet.captured_material);
        if (!captured && (linear != bool(fixture.linear_filter) ||
                          repeat != bool(fixture.repeat_wrap))) {
            if (!MakeSampler(fixture.linear_filter, fixture.repeat_wrap))
                return false;
        }
        for (unsigned i = 0; i < 4; ++i) {
            if (!captured) {
                texture_offsets[i] = i * 6 * kPreviewTextureFaceBytes;
                const auto pixels = GeneratePreviewTexture(fixture, i);
                std::memcpy(static_cast<uint8_t *>(upload.mapped) +
                                texture_offsets[i],
                            pixels.data(), pixels.size());
            } else if (!material_uploaded) {
                auto *destination =
                    static_cast<uint8_t *>(upload.mapped) + texture_offsets[i];
                const size_t face_bytes = size_t(texture_widths[i]) *
                                          texture_heights[i] *
                                          texture_texel_bytes[i];
                std::memset(destination, 0, face_bytes * (cubes[i] ? 6 : 1));
                const auto *texture =
                    PreviewCapturedTexture(packet, i, cubes[i]);
                if (const auto *storage =
                        PreviewCapturedTextureStorage(packet, i)) {
                    CopyPreviewCapturedStorageRows(*storage, texture_widths[i],
                                                   texture_heights[i],
                                                   destination, false);
                } else if (texture) {
                    for (const auto &image : texture->images)
                        CopyPreviewCapturedTextureRows(
                            image.image, destination + face_bytes * image.face,
                            false);
                }
            }
        }
        auto frame = original_pipeline ?
                         PreviewSceneFrame{} :
                     packet.packet_kind == PreviewPacketKind::Replay ?
                         BuildPreviewCapturedFrame(
                             work.result_key.scene, packet.captured_mesh,
                             float(packet.width) / packet.height, true) :
                         BuildPreviewSceneFrame(
                             work.result_key.scene,
                             float(packet.width) / packet.height, true);
        if (original_pipeline)
            frame.draw_count = 1;
        if ((!original_pipeline && frame.draw_count != kPreviewMaxSceneDraws) ||
            frame.vertices.size() > kPreviewMaxSceneVertices) {
            *error = "Preview Vulkan scene geometry is invalid";
            return false;
        }
        const auto &target_draw = frame.draws[frame.draw_count - 1];
        std::vector<Vertex> target_vertices(
            frame.vertices.begin() + target_draw.first_vertex,
            frame.vertices.begin() + target_draw.first_vertex +
                target_draw.vertex_count);
        ApplyPreviewSyntheticFixture(fixture, target_vertices, cubes);
        std::copy(target_vertices.begin(), target_vertices.end(),
                  frame.vertices.begin() + target_draw.first_vertex);
        if (!frame.vertices.empty())
            std::memcpy(vertices.mapped, frame.vertices.data(),
                        frame.vertices.size() * sizeof(Vertex));
        if (uniform_size)
            std::memset(uniform.mapped, 0, uniform_size);
        if (!original_pipeline)
            for (const auto &u : uniforms) {
                for (uint32_t n = 0; n < u.count; ++n) {
                    auto *destination = static_cast<uint8_t *>(uniform.mapped) +
                                        u.offset + n * u.stride;
                    float value[4]{};
                    int32_t integer[4]{};
                    if (u.name == "consts")
                        std::copy(fixture.constant_color.begin(),
                                  fixture.constant_color.end(), value);
                    else if (u.name == "fogColor")
                        std::copy(fixture.fog_color.begin(),
                                  fixture.fog_color.end(), value);
                    else if (u.name == "clipRange") {
                        value[1] = 1;
                        value[2] = -1.0e9f;
                        value[3] = 1.0e9f;
                    } else if (u.name == "texScale")
                        value[0] = 8;
                    else if (u.name == "alphaRef")
                        integer[0] = state.alpha_reference;
                    else if (u.name == "surfaceScale")
                        integer[0] = integer[1] = 1;
                    else if (u.name == "clipRegion") {
                        integer[2] = packet.width;
                        integer[3] = packet.height;
                    }
                    if (u.name == "bumpMat")
                        continue; // Explicit synthetic zero matrix.
                    bool is_integer =
                        u.name == "alphaRef" || u.name == "surfaceScale" ||
                        u.name == "clipRegion" || u.name == "colorKey" ||
                        u.name == "colorKeyMask";
                    std::memcpy(destination,
                                is_integer ? static_cast<void *>(integer) :
                                             static_cast<void *>(value),
                                u.components * 4);
                }
            }
        if (captured) {
            size_t unapplied = 0;
            for (const auto &source : packet.captured_material->uniforms) {
                if (!PreviewCapturedUniformAllowed(source))
                    continue;
                const std::string name = PreviewCapturedUniformName(source);
                auto found = std::find_if(
                    uniforms.begin(), uniforms.end(),
                    [&](const Uniform &u) { return u.name == name; });
                if (found == uniforms.end() || found->type != source.type ||
                    found->components != source.components) {
                    ++unapplied;
                    continue;
                }
                if (source.count > found->count)
                    ++unapplied;
                for (uint32_t element = 0;
                     element < std::min(found->count, source.count);
                     ++element) {
                    auto *destination = static_cast<uint8_t *>(uniform.mapped) +
                                        found->offset + element * found->stride;
                    const auto *data = source.data.data() +
                                       size_t(element) * source.components * 4;
                    if (source.type == XEMU_SHADER_DRAW_UNIFORM_MAT2) {
                        for (size_t column = 0; column < 2; ++column)
                            std::memcpy(destination +
                                            column * found->matrix_stride,
                                        data + column * 8, 8);
                    } else {
                        std::memcpy(destination, data, source.components * 4);
                    }
                }
            }
            if (unapplied)
                material_status += "; " + std::to_string(unapplied) +
                                   " reflected uniforms unapplied";
            size_t zeros = 0;
            for (const auto &binding : bindings) {
                if (binding.descriptorType ==
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
                    const auto slot = binding_stage[binding.binding];
                    zeros += PreviewCapturedTexture(packet, slot,
                                                    cubes[slot]) == nullptr;
                }
            }
            if (zeros)
                material_status +=
                    "; " + std::to_string(zeros) + " texture slots use zero";
        }
        if (original_pipeline) {
            const auto &pipeline = *packet.captured_pipeline;
            material_status =
                DescribePreviewCapturedRaster(*packet.captured_pipeline) +
                "; " + material_status;
            for (const auto &source : pipeline.uniforms) {
                const auto name = PreviewCapturedUniformName(source);
                for (const auto &destination_uniform : uniforms) {
                    if (destination_uniform.name != name ||
                        (source.stage &&
                         source.stage != destination_uniform.stage) ||
                        destination_uniform.type != source.type ||
                        destination_uniform.components != source.components)
                        continue;
                    for (uint32_t element = 0;
                         element <
                         std::min(source.count, destination_uniform.count);
                         ++element) {
                        auto *destination =
                            static_cast<uint8_t *>(uniform.mapped) +
                            destination_uniform.offset +
                            element * destination_uniform.stride;
                        const auto *data =
                            source.data.data() +
                            size_t(element) * source.components * 4;
                        if (source.type == XEMU_SHADER_DRAW_UNIFORM_MAT2 ||
                            source.type == XEMU_SHADER_DRAW_UNIFORM_MAT4) {
                            const size_t columns =
                                source.type == XEMU_SHADER_DRAW_UNIFORM_MAT2 ?
                                    2 :
                                    4;
                            for (size_t column = 0; column < columns; ++column)
                                std::memcpy(
                                    destination +
                                        column *
                                            destination_uniform.matrix_stride,
                                    data + column * columns * 4, columns * 4);
                        } else
                            std::memcpy(destination, data,
                                        source.components * 4);
                    }
                }
            }
            size_t push_offset = 0;
            for (size_t slot = 0; slot < 16 && !push_data.empty(); ++slot)
                if (pipeline.uniform_attribute_mask & (1U << slot)) {
                    std::memcpy(push_data.data() + push_offset,
                                pipeline.attributes[slot].stream.bytes.data(),
                                16);
                    push_offset += 16;
                }
            if (!pipeline_uploaded ||
                uploaded_pipeline_digest != packet.pipeline_digest) {
                for (const auto &attribute : raw_attributes) {
                    const auto &stream =
                        pipeline.attributes[attribute.location].stream;
                    const size_t prefix =
                        size_t(pipeline.first_vertex) * stream.stride;
                    std::memset(raw_vertices[attribute.location].mapped, 0,
                                prefix);
                    std::memcpy(static_cast<uint8_t *>(
                                    raw_vertices[attribute.location].mapped) +
                                    prefix,
                                stream.bytes.data(), stream.bytes.size());
                }
                if (!pipeline.indices.empty())
                    std::memcpy(raw_indices.mapped, pipeline.indices.data(),
                                pipeline.indices.size() * 4);
                pipeline_uploaded = true;
                uploaded_pipeline_digest = packet.pipeline_digest;
            }
        }
        if (!Check(api.ResetFences(device, 1, &fence), "fence reset") ||
            !Check(api.ResetCommandBuffer(cmd, 0), "command reset"))
            return false;
        VkCommandBufferBeginInfo bi{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
        };
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (!Check(api.BeginCommandBuffer(cmd, &bi), "command begin"))
            return false;
        const bool instrumented = AdmitDrawQuery(work);
        if (instrumented)
            api.CmdResetQueryPool(cmd, timestamp_pool, work.slot * 2, 2);
        if (original_pipeline) {
            const auto &before = packet.captured_pipeline->color_before;
            Barrier(target.handle, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                    VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT);
            if (!before.rgba.empty()) {
                std::memcpy(seed_upload.mapped, before.rgba.data(),
                            before.rgba.size());
                VkBufferImageCopy copy{};
                copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
                copy.imageExtent = { packet.width, packet.height, 1 };
                api.CmdCopyBufferToImage(cmd, seed_upload.handle, target.handle,
                                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                         1, &copy);
            } else {
                const VkClearColorValue black{ { 0, 0, 0, 1 } };
                const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT,
                                                     0, 1, 0, 1 };
                api.CmdClearColorImage(cmd, target.handle,
                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                       &black, 1, &range);
            }
            Barrier(target.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        }
        for (unsigned i = 0; i < 4; ++i) {
            if (captured && material_uploaded)
                continue;
            const uint32_t layers = cubes[i] ? 6 : 1;
            Barrier(textures[i].handle, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                    VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, layers);
            VkBufferImageCopy copy{};
            copy.bufferOffset = texture_offsets[i];
            copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, layers };
            copy.imageExtent = { texture_widths[i], texture_heights[i], 1 };
            api.CmdCopyBufferToImage(cmd, upload.handle, textures[i].handle,
                                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                                     &copy);
            Barrier(textures[i].handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, layers);
        }
        VkClearValue clear[2]{};
        for (size_t i = 0; i < 4; ++i)
            clear[0].color.float32[i] = state.clear_color[i];
        clear[1].depthStencil.depth = 1.0f;
        VkRenderPassBeginInfo begin{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        begin.renderPass = pass;
        begin.framebuffer = framebuffer;
        begin.renderArea.extent = { packet.width, packet.height };
        begin.clearValueCount = 2;
        begin.pClearValues = clear;
        api.CmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
        api.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            reference_pipeline);
        VkDeviceSize offset = 0;
        api.CmdBindVertexBuffers(cmd, 0, 1, &vertices.handle, &offset);
        VkViewport viewport{ 0, 0, float(packet.width), float(packet.height),
                             0, 1 };
        VkRect2D scissor{ { 0, 0 }, { packet.width, packet.height } };
        if (original_pipeline) {
            const auto &r = packet.captured_pipeline->raster;
            viewport.minDepth = r.depth_min;
            viewport.maxDepth = r.depth_max;
            if (r.scissor_enabled) {
                const int64_t x = std::clamp<int64_t>(r.scissor_x, 0,
                                                      packet.width),
                              y = std::clamp<int64_t>(r.scissor_y, 0,
                                                      packet.height);
                const int64_t right = std::clamp<int64_t>(
                    int64_t(r.scissor_x) + r.scissor_width, x, packet.width);
                const int64_t bottom = std::clamp<int64_t>(
                    int64_t(r.scissor_y) + r.scissor_height, y, packet.height);
                scissor = { { int32_t(x), int32_t(y) },
                            { uint32_t(right - x), uint32_t(bottom - y) } };
            }
        }
        api.CmdSetViewport(cmd, 0, 1, &viewport);
        api.CmdSetScissor(cmd, 0, 1, &scissor);
        for (size_t i = 0; i < frame.draw_count - 1; ++i) {
            const auto &draw = frame.draws[i];
            if (draw.vertex_count)
                api.CmdDraw(cmd, draw.vertex_count, 1, draw.first_vertex, 0);
        }
        api.CmdBindPipeline(
            cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelines[original_pipeline ? 0 : PipelineVariantIndex(state)]);
        api.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
                                  0, 1, &set, 0, nullptr);
        if (original_pipeline) {
            for (const auto &binding : raw_bindings)
                api.CmdBindVertexBuffers(cmd, binding.binding, 1,
                                         &raw_vertices[binding.binding].handle,
                                         &offset);
            if (!push_data.empty())
                api.CmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                                     push_data.size(), push_data.data());
            const auto &pipeline = *packet.captured_pipeline;
            if (!pipeline.indices.empty()) {
                api.CmdBindIndexBuffer(cmd, raw_indices.handle, 0,
                                       VK_INDEX_TYPE_UINT32);
                if (instrumented)
                    api.CmdWriteTimestamp(cmd,
                                          VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                          timestamp_pool, work.slot * 2);
                api.CmdDrawIndexed(cmd, pipeline.indices.size(), 1, 0, 0, 0);
            } else {
                if (instrumented)
                    api.CmdWriteTimestamp(cmd,
                                          VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                          timestamp_pool, work.slot * 2);
                for (const auto &range : pipeline.ranges)
                    api.CmdDraw(cmd, range[1], 1, range[0], 0);
            }
        } else {
            if (instrumented)
                api.CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                      timestamp_pool, work.slot * 2);
            api.CmdDraw(cmd, target_draw.vertex_count, 1,
                        target_draw.first_vertex, 0);
        }
        if (instrumented)
            api.CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                  timestamp_pool, work.slot * 2 + 1);
        api.CmdEndRenderPass(cmd);
        VkBufferImageCopy copy{};
        copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        copy.imageExtent = { packet.width, packet.height, 1 };
        api.CmdCopyImageToBuffer(cmd, target.handle,
                                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                 readback.handle, 1, &copy);
        VkMemoryBarrier host{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        api.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                               VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0,
                               nullptr, 0, nullptr);
        if (!Check(api.EndCommandBuffer(cmd), "command end"))
            return false;
        VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        if (!Check(api.QueueSubmit(queue, 1, &submit, fence), "submit"))
            return false;
        in_flight = true;
        while (!stop.load(std::memory_order_acquire)) {
            VkResult status = api.GetFenceStatus(device, fence);
            if (status == VK_SUCCESS) {
                in_flight = false;
                break;
            }
            if (status != VK_NOT_READY) {
                Check(status, "completion");
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (in_flight) {
            *error = "Preview Vulkan preview stopped";
            return false;
        }
        CollectDrawQuery(work.slot);
        const size_t stride = packet.width * 4;
        rgba->resize(stride * packet.height);
        const auto *source = static_cast<const uint8_t *>(readback.mapped);
        for (uint32_t row = 0; row < packet.height; ++row) {
            std::memcpy(rgba->data() + row * stride,
                        source + (packet.height - row - 1) * stride, stride);
        }
        ApplyPreviewOutputChannel(work.result_key.channel, rgba);
        material_uploaded = captured;
        if (captured || original_pipeline)
            *error = material_status;
        return true;
    }
};

PreviewVkExecutor::PreviewVkExecutor() : impl_(new Impl)
{
}
PreviewVkExecutor::~PreviewVkExecutor()
{
    delete impl_;
}
bool PreviewVkExecutor::Prepare(const PreviewWorkItem &work, std::string *error,
                                bool *unsupported, bool *cancelled,
                                const std::function<bool()> &may_continue)
{
    impl_->error = error;
    bool ok = impl_->Prepare(work, unsupported, cancelled, may_continue);
    if (!ok && !impl_->prepared)
        impl_->ClearProgram();
    if (!ok && !impl_->initialized) {
        delete impl_;
        impl_ = new Impl;
    }
    return ok;
}
bool PreviewVkExecutor::Render(const PreviewWorkItem &work,
                               const std::atomic<bool> &stop,
                               std::vector<uint8_t> *rgba, std::string *error,
                               PreviewDrawTiming *draw_timing)
{
    impl_->error = error;
    const bool rendered = impl_->Render(work, stop, rgba);
    if (draw_timing && work.slot < kPreviewSlotCount)
        *draw_timing = impl_->draw_queries[work.slot].timing;
    return rendered;
}
bool PreviewVkExecutor::PollDrawTiming(PreviewWorkItem *work,
                                       PreviewDrawTiming *timing)
{
    if (!work || !timing || !impl_->initialized)
        return false;
    for (uint32_t slot = 0; slot < kPreviewSlotCount; ++slot)
        if (impl_->CollectDrawQuery(slot)) {
            *work = impl_->draw_queries[slot].work;
            *timing = impl_->draw_queries[slot].timing;
            return true;
        }
    return false;
}
#ifdef XEMU_PREVIEW_VK_TESTING
void PreviewVkExecutor::FailNextSamplerCreationForTest()
{
    impl_->fail_next_sampler_creation = true;
}
void PreviewVkExecutor::DisableDepthClampForTest()
{
    impl_->disable_depth_clamp = true;
}
bool PreviewVkExecutor::HasSamplerSettingsForTest(bool linear,
                                                  bool repeat) const
{
    return impl_->sampler != VK_NULL_HANDLE && impl_->linear == linear &&
           impl_->repeat == repeat;
}
uint32_t PreviewVkExecutor::PipelineCreationCountForTest() const
{
    return impl_->pipeline_creation_count;
}
#endif
} // namespace xemu::shader_browser
#else
namespace xemu::shader_browser {
struct PreviewVkExecutor::Impl {};
PreviewVkExecutor::PreviewVkExecutor() : impl_(new Impl)
{
}
PreviewVkExecutor::~PreviewVkExecutor()
{
    delete impl_;
}
bool PreviewVkExecutor::Prepare(const PreviewWorkItem &, std::string *error,
                                bool *unsupported, bool *cancelled,
                                const std::function<bool()> &)
{
    if (cancelled) *cancelled = false;
    *unsupported = true;
    *error = "This build has no preview Vulkan preview support";
    return false;
}
bool PreviewVkExecutor::Render(const PreviewWorkItem &,
                               const std::atomic<bool> &,
                               std::vector<uint8_t> *, std::string *error,
                               PreviewDrawTiming *)
{
    *error = "This build has no preview Vulkan preview support";
    return false;
}
bool PreviewVkExecutor::PollDrawTiming(PreviewWorkItem *, PreviewDrawTiming *)
{
    return false;
}
#ifdef XEMU_PREVIEW_VK_TESTING
void PreviewVkExecutor::FailNextSamplerCreationForTest()
{
}
void PreviewVkExecutor::DisableDepthClampForTest()
{
}
bool PreviewVkExecutor::HasSamplerSettingsForTest(bool, bool) const
{
    return false;
}
uint32_t PreviewVkExecutor::PipelineCreationCountForTest() const
{
    return 0;
}
#endif
} // namespace xemu::shader_browser
#endif

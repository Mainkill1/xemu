// SPDX-License-Identifier: GPL-2.0-or-later
#include "qemu/osdep.h"
#include "shader-browser-preview-vk.hh"
#include "shader-browser-preview-adapter.hh"

#ifdef CONFIG_VULKAN
#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>
#include <glslang/Include/glslang_c_interface.h>
#include <spirv_reflect.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <thread>

namespace xemu::shader_browser {
namespace {
// Resolve into this owner's table. Calling volkLoadInstance/volkLoadDevice
// here would replace the game renderer's process-global dispatch pointers.
#define PREVIEW_VK_INSTANCE_FUNCTIONS(X)      \
    X(DestroyInstance)                        \
    X(EnumeratePhysicalDevices)               \
    X(GetPhysicalDeviceQueueFamilyProperties) \
    X(GetPhysicalDeviceMemoryProperties)      \
    X(GetPhysicalDeviceFormatProperties) X(CreateDevice) X(GetDeviceProcAddr)
#define PREVIEW_VK_DEVICE_FUNCTIONS(X)                                            \
    X(DestroyDevice)                                                              \
    X(GetDeviceQueue)                                                             \
    X(CreateCommandPool)                                                          \
    X(DestroyCommandPool) X(AllocateCommandBuffers) X(ResetCommandBuffer) X(      \
        BeginCommandBuffer) X(EndCommandBuffer) X(CreateFence) X(DestroyFence)    \
        X(ResetFences) X(GetFenceStatus) X(QueueSubmit) X(DeviceWaitIdle) X(      \
            CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements)         \
            X(AllocateMemory) X(FreeMemory) X(BindBufferMemory) X(                \
                MapMemory) X(UnmapMemory) X(CreateImage) X(DestroyImage)          \
                X(GetImageMemoryRequirements) X(BindImageMemory) X(               \
                    CreateImageView) X(DestroyImageView) X(CreateSampler)         \
                    X(DestroySampler) X(CreateRenderPass) X(DestroyRenderPass) X( \
                        CreateFramebuffer) X(DestroyFramebuffer)                  \
                        X(CreateShaderModule) X(DestroyShaderModule) X(           \
                            CreatePipelineLayout) X(DestroyPipelineLayout)        \
                            X(CreateGraphicsPipelines) X(DestroyPipeline) X(      \
                                CreateDescriptorSetLayout)                        \
                                X(DestroyDescriptorSetLayout) X(                  \
                                    CreateDescriptorPool)                         \
                                    X(DestroyDescriptorPool) X(                   \
                                        AllocateDescriptorSets)                   \
                                        X(UpdateDescriptorSets) X(                \
                                            CmdPipelineBarrier)                   \
                                            X(CmdCopyBufferToImage) X(            \
                                                CmdCopyImageToBuffer)             \
                                                X(CmdBeginRenderPass) X(          \
                                                    CmdEndRenderPass)             \
                                                    X(CmdBindPipeline) X(         \
                                                        CmdBindDescriptorSets)    \
                                                        X(CmdBindVertexBuffers)   \
                                                            X(CmdSetViewport)     \
                                                                X(CmdSetScissor)  \
                                                                    X(CmdDraw)
struct Dispatch {
#define DECLARE(name) PFN_vk##name name = nullptr;
    PREVIEW_VK_INSTANCE_FUNCTIONS(DECLARE)
    PREVIEW_VK_DEVICE_FUNCTIONS(DECLARE)
#undef DECLARE
};
struct Vertex {
    float position[2], color[4], uv[2];
};
struct Uniform {
    std::string name;
    uint32_t offset, count, stride, components;
};

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
        *error = "Private Vulkan compiler allocation failed";
        return false;
    }
    bool ok = glslang_shader_preprocess(shader, &input) &&
              glslang_shader_parse(shader, &input);
    glslang_program_t *program = nullptr;
    if (!ok)
        *error = std::string("Private Vulkan GLSL: ") +
                 glslang_shader_get_info_log(shader);
    if (ok) {
        program = glslang_program_create();
        ok = program != nullptr;
        if (ok) {
            glslang_program_add_shader(program, shader);
            ok = glslang_program_link(program, input.messages);
            if (!ok)
                *error = std::string("Private Vulkan link: ") +
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
            *error = "Private Vulkan SPIR-V generation failed";
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
    VkRenderPass pass = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
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
    } target, texture;
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
#endif
    std::string *error = nullptr;

    bool Check(VkResult result, const char *operation)
    {
        if (result == VK_SUCCESS)
            return true;
        *error = std::string("Private Vulkan ") + operation + " failed (" +
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
            *error = "Private Vulkan loader unavailable";
            return false;
        }
        auto get = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
            SDL_LoadFunction(loader, "vkGetInstanceProcAddr"));
        if (!get) {
            *error = "Private Vulkan entry point unavailable";
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
            *error = "Private Vulkan physical device unavailable";
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
                    break;
                }
            }
            if (physical)
                break;
        }
        if (!physical) {
            *error = "Private Vulkan graphics queue unavailable";
            return false;
        }
        VkFormatProperties format{};
        api.GetPhysicalDeviceFormatProperties(
            physical, VK_FORMAT_R8G8B8A8_UNORM, &format);
        if (!(format.optimalTilingFeatures &
              VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) ||
            !(format.optimalTilingFeatures &
              VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
            *error = "Private Vulkan RGBA8 format unavailable";
            return false;
        }
        float priority = 0.0f;
        VkDeviceQueueCreateInfo qi{
            VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO
        };
        qi.queueFamilyIndex = family;
        qi.queueCount = 1;
        qi.pQueuePriorities = &priority;
        VkDeviceCreateInfo di{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        di.queueCreateInfoCount = 1;
        di.pQueueCreateInfos = &qi;
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
            *error = "Private Vulkan compiler initialization failed";
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
        *error = "Private Vulkan compatible memory unavailable";
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
                   VkImageUsageFlags usage)
    {
        VkImageCreateInfo ci{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.format = VK_FORMAT_R8G8B8A8_UNORM;
        ci.extent = { width, height, 1 };
        ci.mipLevels = 1;
        ci.arrayLayers = 1;
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
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = ci.format;
        vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
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
        if (pipeline)
            api.DestroyPipeline(device, pipeline, nullptr);
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
        pipeline = VK_NULL_HANDLE;
        layout = VK_NULL_HANDLE;
        descriptor_pool = VK_NULL_HANDLE;
        set = VK_NULL_HANDLE;
        set_layout = VK_NULL_HANDLE;
        framebuffer = VK_NULL_HANDLE;
        pass = VK_NULL_HANDLE;
        sampler = VK_NULL_HANDLE;
        Destroy(vertices);
        Destroy(uniform);
        Destroy(upload);
        Destroy(readback);
        Destroy(target);
        Destroy(texture);
        uniforms.clear();
        bindings.clear();
        uniform_size = 0;
    }
    ~Impl()
    {
        // Only this private worker waits at teardown; never a game queue.
        if (device && api.DeviceWaitIdle)
            api.DeviceWaitIdle(device);
        ClearProgram();
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
                        b.image.dim != SpvDim2D || b.image.arrayed ||
                        b.image.ms || b.image.depth || !b.name ||
                        (std::strcmp(b.name, "texSamp0") &&
                         std::strcmp(b.name, "texSamp1") &&
                         std::strcmp(b.name, "texSamp2") &&
                         std::strcmp(b.name, "texSamp3")))
                        return false;
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
                        const uint32_t element =
                            matrix ? u.numeric.matrix.stride * 2 :
                                     components * 4;
                        if ((count > 1 && stride < element) ||
                            u.offset > uniform_size ||
                            (count - 1) * stride + element >
                                uniform_size - u.offset)
                            return false;
                        uniforms.push_back(
                            { name, u.offset, count, stride, components });
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
        VkDescriptorImageInfo image{ sampler, texture.view,
                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorBufferInfo buffer{ uniform.handle, 0, uniform_size };
        for (const auto &b : bindings) {
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
    bool Prepare(const PreviewWorkItem &work, bool *unsupported)
    {
        *unsupported = false;
        if (!work.packet ||
            work.packet->selection.backend != PreviewBackend::Vulkan ||
            work.packet->packet_kind != PreviewPacketKind::Synthetic ||
            work.packet->partner_source !=
                BuildPreviewSyntheticVertexSource(work.packet->source,
                                                  PreviewBackend::Vulkan)) {
            *unsupported = true;
            *error = "Unsupported Vulkan packet or synthetic partner";
            return false;
        }
        if (in_flight) {
            *error = "Private Vulkan execution owner requires shutdown after "
                     "incomplete work";
            return false;
        }
        if (prepared && key == work.compile_key)
            return true;
        if (!Init())
            return false;
        ClearProgram();
        std::vector<uint32_t> vert, frag;
        if (!Compile(work.packet->partner_source, GLSLANG_STAGE_VERTEX, &vert,
                     error) ||
            !Compile(work.packet->source, GLSLANG_STAGE_FRAGMENT, &frag, error))
            return false;
        if (!Reflect(vert, frag)) {
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
        if (!Check(api.CreatePipelineLayout(device, &li, nullptr, &layout),
                   "pipeline layout"))
            return false;
        VkAttachmentDescription attachment{};
        attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        VkAttachmentReference reference{
            0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        };
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &reference;
        VkSubpassDependency dependency{};
        dependency.srcSubpass = 0;
        dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependency.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        VkRenderPassCreateInfo ri{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
        ri.attachmentCount = 1;
        ri.pAttachments = &attachment;
        ri.subpassCount = 1;
        ri.pSubpasses = &subpass;
        ri.dependencyCount = 1;
        ri.pDependencies = &dependency;
        if (!Check(api.CreateRenderPass(device, &ri, nullptr, &pass),
                   "render pass"))
            return false;
        VkShaderModule modules[2]{};
        for (size_t i = 0; i < 2; ++i) {
            const auto &words = i ? frag : vert;
            VkShaderModuleCreateInfo mi{
                VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO
            };
            mi.codeSize = words.size() * 4;
            mi.pCode = words.data();
            if (!Check(
                    api.CreateShaderModule(device, &mi, nullptr, &modules[i]),
                    "shader module")) {
                if (modules[0])
                    api.DestroyShaderModule(device, modules[0], nullptr);
                return false;
            }
        }
        VkPipelineShaderStageCreateInfo stages[2]{};
        for (size_t i = 0; i < 2; ++i) {
            stages[i].sType =
                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[i].stage =
                i ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
            stages[i].module = modules[i];
            stages[i].pName = "main";
        }
        VkVertexInputBindingDescription vb{ 0, sizeof(Vertex),
                                            VK_VERTEX_INPUT_RATE_VERTEX };
        VkVertexInputAttributeDescription attrs[] = {
            { 0, 0, VK_FORMAT_R32G32_SFLOAT, 0 },
            { 1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 8 },
            { 2, 0, VK_FORMAT_R32G32_SFLOAT, 24 }
        };
        VkPipelineVertexInputStateCreateInfo vi{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
        };
        vi.vertexBindingDescriptionCount = 1;
        vi.pVertexBindingDescriptions = &vb;
        vi.vertexAttributeDescriptionCount = 3;
        vi.pVertexAttributeDescriptions = attrs;
        VkPipelineInputAssemblyStateCreateInfo ia{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO
        };
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO
        };
        viewport.viewportCount = viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO
        };
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo ms{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO
        };
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState blend_attachment{};
        blend_attachment.colorWriteMask = 15;
        VkPipelineColorBlendStateCreateInfo blend{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO
        };
        blend.attachmentCount = 1;
        blend.pAttachments = &blend_attachment;
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
        pi.stageCount = 2;
        pi.pStages = stages;
        pi.pVertexInputState = &vi;
        pi.pInputAssemblyState = &ia;
        pi.pViewportState = &viewport;
        pi.pRasterizationState = &raster;
        pi.pMultisampleState = &ms;
        pi.pColorBlendState = &blend;
        pi.pDynamicState = &dynamic;
        pi.layout = layout;
        pi.renderPass = pass;
        bool ok = Check(api.CreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                                    &pi, nullptr, &pipeline),
                        "pipeline");
        for (auto module : modules)
            api.DestroyShaderModule(device, module, nullptr);
        if (!ok)
            return false;
        if (!MakeBuffer(vertices, 6 * sizeof(Vertex),
                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) ||
            !MakeBuffer(upload, 16, VK_BUFFER_USAGE_TRANSFER_SRC_BIT) ||
            !MakeBuffer(readback, 320 * 320 * 4,
                        VK_BUFFER_USAGE_TRANSFER_DST_BIT) ||
            (uniform_size && !MakeBuffer(uniform, uniform_size,
                                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)) ||
            !MakeImage(target, 320, 320,
                       VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                           VK_IMAGE_USAGE_TRANSFER_SRC_BIT) ||
            !MakeImage(texture, 2, 2,
                       VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                           VK_IMAGE_USAGE_SAMPLED_BIT) ||
            !MakeSampler(false, false))
            return false;
        VkFramebufferCreateInfo fi{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        fi.renderPass = pass;
        fi.attachmentCount = 1;
        fi.pAttachments = &target.view;
        fi.width = fi.height = 320;
        fi.layers = 1;
        if (!Check(api.CreateFramebuffer(device, &fi, nullptr, &framebuffer),
                   "framebuffer"))
            return false;
        VkDescriptorPoolSize sizes[] = {
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
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
        key = work.compile_key;
        prepared = true;
        return true;
    }
    void Barrier(VkImage image, VkImageLayout old_layout,
                 VkImageLayout new_layout, VkAccessFlags src, VkAccessFlags dst,
                 VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage)
    {
        VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        barrier.srcAccessMask = src;
        barrier.dstAccessMask = dst;
        barrier.oldLayout = old_layout;
        barrier.newLayout = new_layout;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex =
            VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        api.CmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0,
                               nullptr, 1, &barrier);
    }
    bool Render(const PreviewWorkItem &work, const std::atomic<bool> &stop,
                std::vector<uint8_t> *rgba)
    {
        if (!prepared || !work.packet || key != work.compile_key || in_flight ||
            !work.packet->width || !work.packet->height ||
            work.packet->width > 320 || work.packet->height > 320) {
            *error = "Private Vulkan preview is not prepared for these inputs";
            return false;
        }
        const auto &packet = *work.packet;
        PreviewSyntheticFixture fixture{};
        if (!DecodePreviewSyntheticFixture(packet.fixture_bytes, &fixture,
                                           error))
            return false;
        if (linear != bool(fixture.linear_filter) ||
            repeat != bool(fixture.repeat_wrap)) {
            if (!MakeSampler(fixture.linear_filter, fixture.repeat_wrap))
                return false;
        }
        std::memcpy(upload.mapped, fixture.texture_texels.data(), 16);
        auto vertex = [&](float x, float y, float u, float v, size_t corner) {
            Vertex result{};
            result.position[0] = x;
            result.position[1] = y;
            result.uv[0] = u * fixture.uv_scale[0] + fixture.uv_offset[0];
            result.uv[1] = v * fixture.uv_scale[1] + fixture.uv_offset[1];
            for (size_t c = 0; c < 4; ++c)
                result.color[c] = fixture.corner_colors[corner][c] / 255.0f;
            return result;
        };
        // Positive Vulkan viewport: the first image row has NDC y=-1.
        // This CPU transport is flipped to GL row order below.
        Vertex tl = vertex(-1, -1, 0, 1, 0), tr = vertex(1, -1, 1, 1, 1);
        Vertex bl = vertex(-1, 1, 0, 0, 2), br = vertex(1, 1, 1, 0, 3);
        Vertex quad[] = { tl, bl, tr, tr, bl, br };
        std::memcpy(vertices.mapped, quad, sizeof(quad));
        if (uniform_size)
            std::memset(uniform.mapped, 0, uniform_size);
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
                    value[0] = 2;
                else if (u.name == "alphaRef")
                    integer[0] = fixture.alpha_reference;
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
        if (!Check(api.ResetFences(device, 1, &fence), "fence reset") ||
            !Check(api.ResetCommandBuffer(cmd, 0), "command reset"))
            return false;
        VkCommandBufferBeginInfo bi{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
        };
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (!Check(api.BeginCommandBuffer(cmd, &bi), "command begin"))
            return false;
        Barrier(texture.handle, VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy copy{};
        copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        copy.imageExtent = { 2, 2, 1 };
        api.CmdCopyBufferToImage(cmd, upload.handle, texture.handle,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                                 &copy);
        Barrier(texture.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        VkClearValue clear{};
        clear.color.float32[3] = 1;
        VkRenderPassBeginInfo begin{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        begin.renderPass = pass;
        begin.framebuffer = framebuffer;
        begin.renderArea.extent = { packet.width, packet.height };
        begin.clearValueCount = 1;
        begin.pClearValues = &clear;
        api.CmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
        api.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        api.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
                                  0, 1, &set, 0, nullptr);
        VkDeviceSize offset = 0;
        api.CmdBindVertexBuffers(cmd, 0, 1, &vertices.handle, &offset);
        VkViewport viewport{ 0, 0, float(packet.width), float(packet.height),
                             0, 1 };
        VkRect2D scissor{ { 0, 0 }, { packet.width, packet.height } };
        api.CmdSetViewport(cmd, 0, 1, &viewport);
        api.CmdSetScissor(cmd, 0, 1, &scissor);
        api.CmdDraw(cmd, 6, 1, 0, 0);
        api.CmdEndRenderPass(cmd);
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
            *error = "Private Vulkan preview stopped";
            return false;
        }
        const size_t stride = packet.width * 4;
        rgba->resize(stride * packet.height);
        const auto *source = static_cast<const uint8_t *>(readback.mapped);
        for (uint32_t row = 0; row < packet.height; ++row) {
            std::memcpy(rgba->data() + row * stride,
                        source + (packet.height - row - 1) * stride, stride);
        }
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
                                bool *unsupported)
{
    impl_->error = error;
    bool ok = impl_->Prepare(work, unsupported);
    if (!ok && !impl_->initialized) {
        delete impl_;
        impl_ = new Impl;
    }
    return ok;
}
bool PreviewVkExecutor::Render(const PreviewWorkItem &work,
                               const std::atomic<bool> &stop,
                               std::vector<uint8_t> *rgba, std::string *error)
{
    impl_->error = error;
    return impl_->Render(work, stop, rgba);
}
#ifdef XEMU_PREVIEW_VK_TESTING
void PreviewVkExecutor::FailNextSamplerCreationForTest()
{
    impl_->fail_next_sampler_creation = true;
}
bool PreviewVkExecutor::HasSamplerSettingsForTest(bool linear,
                                                  bool repeat) const
{
    return impl_->sampler != VK_NULL_HANDLE && impl_->linear == linear &&
           impl_->repeat == repeat;
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
                                bool *unsupported)
{
    *unsupported = true;
    *error = "This build has no private Vulkan preview support";
    return false;
}
bool PreviewVkExecutor::Render(const PreviewWorkItem &,
                               const std::atomic<bool> &,
                               std::vector<uint8_t> *, std::string *error)
{
    *error = "This build has no private Vulkan preview support";
    return false;
}
#ifdef XEMU_PREVIEW_VK_TESTING
void PreviewVkExecutor::FailNextSamplerCreationForTest()
{
}
bool PreviewVkExecutor::HasSamplerSettingsForTest(bool, bool) const
{
    return false;
}
#endif
} // namespace xemu::shader_browser
#endif

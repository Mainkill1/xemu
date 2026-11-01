/*
 * Actual-driver regression for late texture consumption after capacity finish.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/shaders.c"
#include "hw/xbox/nv2a/pgraph/vk/texture.c"
#include "test-xbox-vk-texture-submit.h"

static unsigned validation_errors;

static VKAPI_ATTR VkBool32 VKAPI_CALL validation_message(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT *data, void *opaque)
{
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        validation_errors++;
        g_printerr("Vulkan validation: %s\n", data->pMessage);
    }
    return VK_FALSE;
}

typedef struct NativeFixture {
    NV2AState *d;
    PGRAPHVkState *r;
    VkDebugUtilsMessengerEXT messenger;
    VkFramebuffer framebuffer;
    VkRenderPass render_pass;
    VkPipeline pipeline;
    VkPipelineLayout layout;
    VkShaderModule shaders[2];
    TextureBinding target;
    TextureBinding old[NV2A_MAX_TEXTURES];
    TextureBinding replacement;
    StorageBuffer readback;
    ShaderModuleInfo modules[2];
    ShaderBinding shader;
    PipelineBinding pipeline_binding;
    uint8_t uniforms[2][64];
} NativeFixture;

/*
 * No UI, guest reports, compute work or profiling is scheduled by this fixture.
 * GPU ownership, uploads, commands, finish, fences and destruction stay real.
 */
NV2AStats g_nv2a_stats;
XemuTweakBits xemu_tweaks_active;
int nv2a_vk_dgroup_indent;
bool nv2a_vk_text_debug_enabled;

void pgraph_vk_text_debug_printf(const char *format, ...)
{
}

void pgraph_vk_begin_debug_marker(PGRAPHVkState *r, VkCommandBuffer cmd,
                                  float color[4], const char *format, ...)
{
}

void pgraph_vk_end_debug_marker(PGRAPHVkState *r, VkCommandBuffer cmd)
{
}

void nv2a_profile_log_event_once(NV2AProfileEvent event)
{
}

void pgraph_vk_process_pending_reports_internal(NV2AState *d)
{
    g_assert_cmpuint(d->pgraph.vk_renderer_state->num_queries_in_flight, ==, 0);
    g_assert_cmpuint(d->pgraph.vk_renderer_state->report_queue_depth, ==, 0);
}

void pgraph_vk_check_memory_budget(PGRAPHState *pg)
{
    g_assert_not_reached();
}

static void make_buffer(PGRAPHVkState *r, StorageBuffer *b, VkDeviceSize size,
                        VkBufferUsageFlags usage)
{
    VkBufferCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
    };
    VmaAllocationCreateInfo allocation = {
        .usage = VMA_MEMORY_USAGE_AUTO,
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                 VMA_ALLOCATION_CREATE_MAPPED_BIT,
    };
    VmaAllocationInfo result;
    VK_CHECK(vmaCreateBuffer(r->allocator, &info, &allocation, &b->buffer,
                             &b->allocation, &result));
    b->buffer_size = size;
    b->mapped = result.pMappedData;
    memset(b->mapped, 0, size);
}

static void make_image(PGRAPHVkState *r, TextureBinding *t,
                       VkImageUsageFlags usage)
{
    VkImageCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = { 1, 1, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
    };
    VmaAllocationCreateInfo allocation = { .usage = VMA_MEMORY_USAGE_AUTO };
    VK_CHECK(vmaCreateImage(r->allocator, &info, &allocation, &t->image,
                            &t->allocation, NULL));
    VkImageViewCreateInfo view = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = t->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = info.format,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    VK_CHECK(vkCreateImageView(r->device, &view, NULL, &t->image_view));
    if (usage & VK_IMAGE_USAGE_SAMPLED_BIT) {
        VkSamplerCreateInfo sampler = {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_NEAREST,
            .minFilter = VK_FILTER_NEAREST,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        };
        VK_CHECK(vkCreateSampler(r->device, &sampler, NULL, &t->sampler));
    }
}

static void image_barrier(VkCommandBuffer cmd, VkImage image,
                          VkImageLayout before, VkImageLayout after,
                          VkAccessFlags src_access, VkAccessFlags dst_access,
                          VkPipelineStageFlags src_stage,
                          VkPipelineStageFlags dst_stage)
{
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = src_access,
        .dstAccessMask = dst_access,
        .oldLayout = before,
        .newLayout = after,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, NULL, 0, NULL, 1,
                         &barrier);
}

static void make_device(NativeFixture *f)
{
    PGRAPHVkState *r = f->r;
    VK_CHECK(volkInitialize());
    uint32_t count = 0;
    VK_CHECK(vkEnumerateInstanceLayerProperties(&count, NULL));
    g_autofree VkLayerProperties *layers = g_new0(VkLayerProperties, count);
    VK_CHECK(vkEnumerateInstanceLayerProperties(&count, layers));
    const char *layer = "VK_LAYER_KHRONOS_validation";
    bool validation = false;
    for (uint32_t i = 0; i < count; i++) {
        validation |= !strcmp(layers[i].layerName, layer);
    }
    if (g_getenv("XEMU_VK_NATIVE_REQUIRE_VALIDATION")) {
        g_assert_true(validation);
    }
    const char *extension = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    r->vk_api_version = VK_API_VERSION_1_1;
    VkApplicationInfo app = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "xemu texture submission regression",
        .apiVersion = r->vk_api_version,
    };
    VkDebugUtilsMessengerCreateInfoEXT debug = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = validation_message,
    };
    VkInstanceCreateInfo instance = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = validation ? &debug : NULL,
        .pApplicationInfo = &app,
        .enabledLayerCount = validation,
        .ppEnabledLayerNames = &layer,
        .enabledExtensionCount = validation,
        .ppEnabledExtensionNames = &extension,
    };
    VK_CHECK(vkCreateInstance(&instance, NULL, &r->instance));
    volkLoadInstance(r->instance);
    if (validation) {
        VK_CHECK(vkCreateDebugUtilsMessengerEXT(r->instance, &debug, NULL,
                                                &f->messenger));
    }
    count = 1;
    VkResult result =
        vkEnumeratePhysicalDevices(r->instance, &count, &r->physical_device);
    g_assert_true(result == VK_SUCCESS || result == VK_INCOMPLETE);
    g_assert_cmpuint(count, >, 0);
    vkGetPhysicalDeviceProperties(r->physical_device, &r->device_props);
    g_test_message("device=%s driver=%u api=%u type=%u validation=%s",
                   r->device_props.deviceName, r->device_props.driverVersion,
                   r->device_props.apiVersion, r->device_props.deviceType,
                   validation ? "enabled" : "UNAVAILABLE");
    vkGetPhysicalDeviceQueueFamilyProperties(r->physical_device, &count, NULL);
    g_autofree VkQueueFamilyProperties *families =
        g_new0(VkQueueFamilyProperties, count);
    vkGetPhysicalDeviceQueueFamilyProperties(r->physical_device, &count,
                                             families);
    uint32_t family;
    for (family = 0; family < count; family++) {
        if (families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            break;
        }
    }
    g_assert_cmpuint(family, <, count);
    float priority = 1;
    VkDeviceQueueCreateInfo queue = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = family,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };
    VkDeviceCreateInfo device = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue,
    };
    VK_CHECK(vkCreateDevice(r->physical_device, &device, NULL, &r->device));
    volkLoadDevice(r->device);
    vkGetDeviceQueue(r->device, family, 0, &r->queue);
    VmaAllocatorCreateInfo allocator = {
        .vulkanApiVersion = r->vk_api_version,
        .instance = r->instance,
        .physicalDevice = r->physical_device,
        .device = r->device,
    };
    VmaVulkanFunctions functions;
    VK_CHECK(vmaImportVulkanFunctionsFromVolk(&allocator, &functions));
    allocator.pVulkanFunctions = &functions;
    VK_CHECK(vmaCreateAllocator(&allocator, &r->allocator));
    VkCommandPoolCreateInfo pool = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = family,
    };
    VK_CHECK(vkCreateCommandPool(r->device, &pool, NULL, &r->command_pool));
    VkCommandBufferAllocateInfo commands = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = r->command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 2,
    };
    VK_CHECK(
        vkAllocateCommandBuffers(r->device, &commands, r->command_buffers));
    r->command_buffer = r->command_buffers[0];
    r->aux_command_buffer = r->command_buffers[1];
    VkSemaphoreCreateInfo semaphore = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };
    VkFenceCreateInfo fence = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VK_CHECK(vkCreateSemaphore(r->device, &semaphore, NULL,
                               &r->command_buffer_semaphore));
    VK_CHECK(vkCreateFence(r->device, &fence, NULL, &r->command_buffer_fence));
}

static VkShaderModule make_shader(PGRAPHVkState *r, glslang_stage_t stage,
                                  const char *source)
{
    PGRAPHVkGlslCompileConfig config = { .api_version = r->vk_api_version };
    GByteArray *spirv =
        pgraph_vk_compile_glsl_to_spv_config(&config, stage, source);
    g_assert_nonnull(spirv);
    VkShaderModuleCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = spirv->len,
        .pCode = (const uint32_t *)spirv->data,
    };
    VkShaderModule module;
    VK_CHECK(vkCreateShaderModule(r->device, &info, NULL, &module));
    g_byte_array_unref(spirv);
    return module;
}

static void make_pipeline(NativeFixture *f)
{
    PGRAPHVkState *r = f->r;
    PGRAPHState *pg = &f->d->pgraph;
    create_descriptor_pool(pg);
    create_descriptor_set_layout(pg);
    create_descriptor_sets(pg);
    VkPipelineLayoutCreateInfo layout = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &r->descriptor_set_layout,
    };
    VK_CHECK(vkCreatePipelineLayout(r->device, &layout, NULL, &f->layout));
    VkAttachmentDescription attachment = {
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    VkAttachmentReference color = { 0,
                                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color,
    };
    VkRenderPassCreateInfo pass = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
    };
    VK_CHECK(vkCreateRenderPass(r->device, &pass, NULL, &f->render_pass));
    VkFramebufferCreateInfo framebuffer = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = f->render_pass,
        .attachmentCount = 1,
        .pAttachments = &f->target.image_view,
        .width = 1,
        .height = 1,
        .layers = 1,
    };
    VK_CHECK(
        vkCreateFramebuffer(r->device, &framebuffer, NULL, &f->framebuffer));
    f->shaders[0] = make_shader(
        r, GLSLANG_STAGE_VERTEX,
        "#version 450\n"
        "void main(){vec2 p[3]=vec2[](vec2(-1,-1),vec2(3,-1),vec2(-1,3));"
        "gl_Position=vec4(p[gl_VertexIndex],0,1);}\n");
    f->shaders[1] =
        make_shader(r, GLSLANG_STAGE_FRAGMENT,
                    "#version 450\n"
                    "layout(set=0,binding=2) uniform sampler2D a;\n"
                    "layout(set=0,binding=3) uniform sampler2D b;\n"
                    "layout(set=0,binding=4) uniform sampler2D c;\n"
                    "layout(set=0,binding=5) uniform sampler2D d;\n"
                    "layout(location=0) out vec4 color;\n"
                    "void main(){color=vec4(texelFetch(a,ivec2(0),0).r,"
                    "texelFetch(b,ivec2(0),0).r,texelFetch(c,ivec2(0),0).r,"
                    "texelFetch(d,ivec2(0),0).r);}\n");
    VkPipelineShaderStageCreateInfo stages[2];
    for (int i = 0; i < 2; i++) {
        stages[i] = (VkPipelineShaderStageCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage =
                i ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT,
            .module = f->shaders[i],
            .pName = "main",
        };
    }
    VkPipelineVertexInputStateCreateInfo vertex = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };
    VkPipelineInputAssemblyStateCreateInfo assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkViewport viewport = { 0, 0, 1, 1, 0, 1 };
    VkRect2D scissor = { { 0, 0 }, { 1, 1 } };
    VkPipelineViewportStateCreateInfo view = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor,
    };
    VkPipelineRasterizationStateCreateInfo raster = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .lineWidth = 1,
    };
    VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineColorBlendAttachmentState blend_attachment = {
        .colorWriteMask = 15,
    };
    VkPipelineColorBlendStateCreateInfo blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blend_attachment,
    };
    VkGraphicsPipelineCreateInfo pipeline = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vertex,
        .pInputAssemblyState = &assembly,
        .pViewportState = &view,
        .pRasterizationState = &raster,
        .pMultisampleState = &multisample,
        .pColorBlendState = &blend,
        .layout = f->layout,
        .renderPass = f->render_pass,
    };
    VK_CHECK(vkCreateGraphicsPipelines(r->device, VK_NULL_HANDLE, 1, &pipeline,
                                       NULL, &f->pipeline));
    f->pipeline_binding.layout = f->layout;
    r->pipeline_binding = &f->pipeline_binding;
}

static void run_case(gconstpointer uniform_capacity)
{
    if (!g_getenv("XEMU_VK_NATIVE_TEST")) {
        g_test_skip("Set XEMU_VK_NATIVE_TEST=1 to use an actual Vulkan driver");
        return;
    }
    g_autofree NativeFixture *f = g_new0(NativeFixture, 1);
    f->d = g_new0(NV2AState, 1);
    f->r = g_new0(PGRAPHVkState, 1);
    PGRAPHState *pg = &f->d->pgraph;
    PGRAPHVkState *r = f->r;
    pg->vk_renderer_state = r;
    validation_errors = 0;
    make_device(f);
    make_buffer(r, &r->storage_buffers[BUFFER_UNIFORM_STAGING], 65536,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    make_buffer(r, &r->storage_buffers[BUFFER_UNIFORM], 65536,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    make_buffer(r, &r->storage_buffers[BUFFER_VERTEX_RAM], 256,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    make_buffer(r, &f->readback, 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    make_image(r, &f->target,
               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    make_image(r, &f->replacement,
               VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    r->shader_binding = &f->shader;
    f->shader.vsh.module_info = &f->modules[0];
    f->shader.psh.module_info = &f->modules[1];
    for (int i = 0; i < 2; i++) {
        f->modules[i].uniforms.total_size = sizeof(f->uniforms[i]);
        f->modules[i].uniforms.allocation = f->uniforms[i];
    }
    make_pipeline(f);
    lru_init(&r->texture_cache);
    r->texture_cache.pre_node_evict = texture_cache_entry_pre_evict;
    r->texture_cache.post_node_evict = texture_cache_entry_post_evict;
    static const uint8_t expected[] = { 17, 37, 71, 131 };
    pgraph_vk_begin_command_buffer(pg);
    for (int i = 0; i < NV2A_MAX_TEXTURES; i++) {
        TextureBinding *t = &f->old[i];
        make_image(
            r, t, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        lru_add_free(&r->texture_cache, &t->node);
        g_assert_true(lru_lookup(&r->texture_cache, i + 1, NULL) == &t->node);
        r->texture_bindings[i] = t;
        image_barrier(r->command_buffer, t->image, VK_IMAGE_LAYOUT_UNDEFINED,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                      VK_ACCESS_TRANSFER_WRITE_BIT,
                      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                      VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkClearColorValue color = { .float32 = { expected[i] / 255.f, 0, 0,
                                                 1 } };
        VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0,
                                          1 };
        vkCmdClearColorImage(r->command_buffer, t->image,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1,
                             &range);
        image_barrier(r->command_buffer, t->image,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                      VK_PIPELINE_STAGE_TRANSFER_BIT,
                      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    }
    pgraph_vk_finish(pg, VK_FINISH_REASON_FLUSH);
    pgraph_vk_begin_command_buffer(pg);
    uint64_t preparation_submission = r->submit_count;
    pgraph_vk_pin_bound_textures(r);
    /* Force only the intended late capacity boundary, with real backing. */
    r->descriptor_set_index =
        uniform_capacity ? 1 : ARRAY_SIZE(r->descriptor_sets);
    r->storage_buffers[BUFFER_UNIFORM_STAGING].buffer_offset =
        uniform_capacity ? 65536 : 512;
    r->uniform_stage_dirty[0] = r->uniform_stage_dirty[1] = true;
    r->texture_descriptor_publication_pending = true;
    pgraph_vk_update_descriptor_sets(pg);
    g_assert_cmpuint(r->submit_count, ==, preparation_submission + 1);
    g_assert_false(r->in_command_buffer);
    g_assert_cmpint(vkGetFenceStatus(r->device, r->command_buffer_fence), ==,
                    VK_SUCCESS);
    pgraph_vk_begin_command_buffer(pg);
    VkRenderPassBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = f->render_pass,
        .framebuffer = f->framebuffer,
        .renderArea = { { 0, 0 }, { 1, 1 } },
    };
    vkCmdBeginRenderPass(r->command_buffer, &begin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(r->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      f->pipeline);
    test_texture_submit_bind(pg);
    vkCmdDraw(r->command_buffer, 3, 1, 0, 0);
    vkCmdEndRenderPass(r->command_buffer);
    for (int i = 0; i < NV2A_MAX_TEXTURES; i++) {
        r->texture_bindings[i] = &f->replacement;
    }
    /* Fail before destructive eviction in the private omitted-pin variant. */
    for (int i = 0; i < NV2A_MAX_TEXTURES; i++) {
        g_assert_false(
            texture_cache_entry_pre_evict(&r->texture_cache, &f->old[i].node));
    }
    g_assert_null(lru_try_evict_one(&r->texture_cache));
    image_barrier(r->command_buffer, f->target.image,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                  VK_ACCESS_TRANSFER_READ_BIT,
                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy copy = {
        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageExtent = { 1, 1, 1 },
    };
    vkCmdCopyImageToBuffer(r->command_buffer, f->target.image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           f->readback.buffer, 1, &copy);
    VkMemoryBarrier host = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
    };
    vkCmdPipelineBarrier(r->command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0, NULL, 0,
                         NULL);
    pgraph_vk_finish(pg, VK_FINISH_REASON_FLUSH);
    g_assert_cmpuint(r->submit_count, ==, preparation_submission + 2);
    g_assert_false(r->in_command_buffer);
    g_assert_cmpint(vkGetFenceStatus(r->device, r->command_buffer_fence), ==,
                    VK_SUCCESS);
    VK_CHECK(
        vmaInvalidateAllocation(r->allocator, f->readback.allocation, 0, 4));
    g_assert_cmpmem(f->readback.mapped, 4, expected, sizeof(expected));
    for (int i = 0; i < NV2A_MAX_TEXTURES; i++) {
        g_assert_nonnull(lru_try_evict_one(&r->texture_cache));
    }
    for (int i = 0; i < NV2A_MAX_TEXTURES; i++) {
        g_assert_true(f->old[i].image == VK_NULL_HANDLE);
        g_assert_true(f->old[i].image_view == VK_NULL_HANDLE);
        g_assert_true(f->old[i].sampler == VK_NULL_HANDLE);
    }
    g_assert_cmpint(r->texture_cache.num_used, ==, 0);
    destroy_descriptor_sets(pg);
    destroy_descriptor_pool(pg);
    vkDestroyPipeline(r->device, f->pipeline, NULL);
    vkDestroyPipelineLayout(r->device, f->layout, NULL);
    destroy_descriptor_set_layout(pg);
    for (int i = 0; i < 2; i++) {
        vkDestroyShaderModule(r->device, f->shaders[i], NULL);
    }
    vkDestroyFramebuffer(r->device, f->framebuffer, NULL);
    vkDestroyRenderPass(r->device, f->render_pass, NULL);
    texture_cache_release_node_resources(r, &f->replacement);
    texture_cache_release_node_resources(r, &f->target);
    for (int i = 0; i < ARRAY_SIZE(r->storage_buffers); i++) {
        StorageBuffer *b = &r->storage_buffers[i];
        if (b->buffer) {
            vmaDestroyBuffer(r->allocator, b->buffer, b->allocation);
        }
    }
    vmaDestroyBuffer(r->allocator, f->readback.buffer, f->readback.allocation);
    vkDestroyFence(r->device, r->command_buffer_fence, NULL);
    vkDestroySemaphore(r->device, r->command_buffer_semaphore, NULL);
    vkDestroyCommandPool(r->device, r->command_pool, NULL);
    vmaDestroyAllocator(r->allocator);
    vkDestroyDevice(r->device, NULL);
    if (f->messenger) {
        vkDestroyDebugUtilsMessengerEXT(r->instance, f->messenger, NULL);
    }
    vkDestroyInstance(r->instance, NULL);
    g_assert_cmpuint(validation_errors, ==, 0);
    g_free(r);
    g_free(f->d);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    pgraph_vk_init_glsl_compiler();
    g_test_add_data_func("/vk/texture-submit-native/descriptor-rollover", NULL,
                         run_case);
    g_test_add_data_func("/vk/texture-submit-native/uniform-rollover",
                         GINT_TO_POINTER(1), run_case);
    int result = g_test_run();
    pgraph_vk_finalize_glsl_compiler();
    return result;
}

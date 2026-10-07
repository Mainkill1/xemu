/* Scoped native qualification of the actual surface.c upload path and surface-compute.c adapter.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/fast-hash.h"
#include "hw/xbox/nv2a/pgraph/vk/renderer.h"
#ifndef _WIN32
#include <shaderc/shaderc.h>
#endif
#define XEMU_VK_DIAGNOSTIC_FAILURES 1
#include "hw/xbox/nv2a/pgraph/vk/failpoint.h"

NV2AState *g_nv2a;
static VkPhysicalDevice physical;
static VkQueue queue;
static VkCommandPool command_pool;
static unsigned shader_compiles;
static VkCommandBuffer pending_main;
static unsigned main_finishes;
static unsigned validation_errors;
static unsigned loader_manifest_errors;
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_message(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT *data, void *opaque)
{
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        if ((type & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) &&
            strstr(data->pMessage, "loader_get_json: Failed to open JSON file") &&
            strstr(data->pMessage, "EOSOverlayVkLayer-Win64.json")) {
            loader_manifest_errors++;
            fprintf(stderr, "HOST LOADER ERROR: %s\n", data->pMessage);
            return VK_FALSE;
        }
        validation_errors++;
        fprintf(stderr, "VALIDATION: %s\n", data->pMessage);
    }
    return VK_FALSE;
}


uint64_t fast_hash(const uint8_t *bytes, size_t size)
{
    uint64_t value = UINT64_C(1469598103934665603);
    for (size_t i = 0; i < size; i++) {
        value = (value ^ bytes[i]) * UINT64_C(1099511628211);
    }
    return value;
}

void pgraph_vk_begin_debug_marker(PGRAPHVkState *r, VkCommandBuffer cmd,
                                 float color[4], const char *format, ...)
{
}

void pgraph_vk_end_debug_marker(PGRAPHVkState *r, VkCommandBuffer cmd)
{
}

ShaderModuleInfo *pgraph_vk_create_shader_module_from_glsl(
    PGRAPHVkState *r, VkShaderStageFlagBits kind, const char *glsl)
{
    assert(kind == VK_SHADER_STAGE_COMPUTE_BIT);
    const char *dir = getenv("XEMU_NATIVE_SHADER_DIR");
    assert(dir);
    g_mkdir_with_parents(dir, 0700);
    uint64_t hash = fast_hash((const uint8_t *)glsl, strlen(glsl));
    g_autofree char *source_path = g_strdup_printf("%s/%016" PRIx64 ".glsl", dir, hash);
    g_autofree char *binary_path = g_strdup_printf("%s/%016" PRIx64 ".spv", dir, hash);
    char *binary = NULL;
    gsize bytes = 0;
#ifndef _WIN32
    shaderc_compiler_t compiler = shaderc_compiler_initialize();
    shaderc_compilation_result_t result = shaderc_compile_into_spv(
        compiler, glsl, strlen(glsl), shaderc_compute_shader,
        "production-surface-compute.glsl", "main", NULL);
    if (shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success) {
        fprintf(stderr, "%s", shaderc_result_get_error_message(result)); abort();
    }
    bytes = shaderc_result_get_length(result);
    binary = g_memdup2(shaderc_result_get_bytes(result), bytes);
    assert(g_file_set_contents(source_path, glsl, -1, NULL));
    assert(g_file_set_contents(binary_path, binary, bytes, NULL));
    shaderc_result_release(result); shaderc_compiler_release(compiler);
#else
    char *saved_source = NULL;
    assert(g_file_get_contents(source_path, &saved_source, NULL, NULL));
    assert(!strcmp(saved_source, glsl)); g_free(saved_source);
    assert(g_file_get_contents(binary_path, &binary, &bytes, NULL));
#endif
    ShaderModuleInfo *module = g_new0(ShaderModuleInfo, 1);
    VkShaderModuleCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = bytes, .pCode = (const uint32_t *)binary,
    };
    VK_CHECK(vkCreateShaderModule(r->device, &info, NULL, &module->module));
    g_free(binary);
    shader_compiles++;
    return module;
}

void pgraph_vk_destroy_shader_module(PGRAPHVkState *r, ShaderModuleInfo *module)
{
    vkDestroyShaderModule(r->device, module->module, NULL);
    g_free(module);
}

typedef struct ComputeFixtureBuffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
    uint8_t *bytes;
} ComputeFixtureBuffer;

static ComputeFixtureBuffer create_buffer(PGRAPHVkState *r, size_t bytes)
{
    ComputeFixtureBuffer buffer = { 0 };
    VkBufferCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = bytes,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    };
    VK_CHECK(vkCreateBuffer(r->device, &info, NULL, &buffer.buffer));
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(r->device, buffer.buffer, &requirements);
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    unsigned type;
    for (type = 0; type < properties.memoryTypeCount; type++) {
        VkMemoryPropertyFlags flags = properties.memoryTypes[type].propertyFlags;
        if ((requirements.memoryTypeBits & (1u << type)) &&
            (flags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            break;
        }
    }
    assert(type < properties.memoryTypeCount);
    VkMemoryAllocateInfo allocation = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = type,
    };
    VK_CHECK(vkAllocateMemory(r->device, &allocation, NULL, &buffer.memory));
    VK_CHECK(vkBindBufferMemory(r->device, buffer.buffer, buffer.memory, 0));
    VK_CHECK(vkMapMemory(r->device, buffer.memory, 0, bytes, 0,
                        (void **)&buffer.bytes));
    memset(buffer.bytes, 0, bytes);
    return buffer;
}

static void destroy_buffer(PGRAPHVkState *r, ComputeFixtureBuffer *buffer)
{
    vkUnmapMemory(r->device, buffer->memory);
    vkDestroyBuffer(r->device, buffer->buffer, NULL);
    vkFreeMemory(r->device, buffer->memory, NULL);
}

void pgraph_vk_ensure_buffer_capacity(PGRAPHState *pg, int index,
                                      VkDeviceSize bytes)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    assert(r->storage_buffers[index].buffer_size >= bytes);
}

VkCommandBuffer pgraph_vk_begin_nondraw_commands(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    VkCommandBuffer cmd;
    VkCommandBufferAllocateInfo allocation = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VK_CHECK(vkAllocateCommandBuffers(r->device, &allocation, &cmd));
    VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    VK_CHECK(vkBeginCommandBuffer(cmd, &begin));
    return cmd;
}

void pgraph_vk_end_nondraw_commands(PGRAPHState *pg, VkCommandBuffer cmd)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    VK_CHECK(vkEndCommandBuffer(cmd));
    VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
    };
    VK_CHECK(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(queue));
    vkFreeCommandBuffers(r->device, command_pool, 1, &cmd);
}

void pgraph_vk_finish(PGRAPHState *pg, FinishReason reason)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    if (pending_main) {
        VkCommandBuffer cmd = pending_main;
        pending_main = VK_NULL_HANDLE;
        pgraph_vk_end_nondraw_commands(pg, cmd);
        main_finishes++;
    } else {
        VK_CHECK(vkQueueWaitIdle(queue));
    }
    r->in_command_buffer = false;
    pgraph_vk_surface_retirements_complete(r);
    pgraph_vk_compute_finish_complete(r);
}


VkCommandBuffer pgraph_vk_begin_single_time_commands(PGRAPHState *pg)
{ VkCommandBuffer cmd=pgraph_vk_begin_nondraw_commands(pg); pg->vk_renderer_state->aux_command_buffer=cmd; return cmd; }
void pgraph_vk_end_single_time_commands(PGRAPHState *pg, VkCommandBuffer cmd,
                                        SingleTimeReason reason, uint64_t bytes)
{ pgraph_vk_end_nondraw_commands(pg, cmd); }
NV2AStats g_nv2a_stats;
void nv2a_profile_log_event_once(NV2AProfileEvent event) { }
int nv2a_vk_dgroup_indent;
bool nv2a_vk_text_debug_enabled;
void pgraph_vk_text_debug_printf(const char *format, ...) { (void)format; }
/* VMA allocation boundary backed by actual coherent Vulkan memory. */
VkResult vmaMapMemory(VmaAllocator a, VmaAllocation b, void **ptr)
{ *ptr = ((ComputeFixtureBuffer *)b)->bytes; return VK_SUCCESS; }
void vmaUnmapMemory(VmaAllocator a, VmaAllocation b) { }
VkResult vmaFlushAllocation(VmaAllocator a,VmaAllocation b,VkDeviceSize o,VkDeviceSize n)
{ return VK_SUCCESS; }
VkResult vmaInvalidateAllocation(VmaAllocator a,VmaAllocation b,VkDeviceSize o,VkDeviceSize n) { return VK_SUCCESS; }
static bool inject_guest_dirty;
static unsigned dirty_marks;
uint64_t memory_region_size(MemoryRegion *mr) { return 2*1024*1024; }
bool memory_region_take_dirty_pages(MemoryRegion *mr, hwaddr start, hwaddr size,
                                    unsigned client, unsigned long *pages, size_t capacity)
{ memset(pages,0,capacity*sizeof(*pages));if(inject_guest_dirty){pages[0]=1;inject_guest_dirty=false;return true;}return false; }
void memory_region_set_client_dirty(MemoryRegion *mr, hwaddr a, hwaddr n, unsigned client) { dirty_marks++; }
static PGRAPHVkFailpoint failpoint;
bool pgraph_vk_failpoint_should_fail(PGRAPHVkFailpoint point) { return point==failpoint; }
#include "hw/xbox/nv2a/pgraph/vk/surface.c"
XemuTweakBits xemu_tweaks_active;
bool tcg_allowed;
struct config g_config;
CPUState *qemu_get_cpu(int index) { abort(); }
void mem_access_callback_remove_by_ref(CPUState *cpu,MemAccessCallback *cb) { abort(); }
DMAObject nv_dma_load(NV2AState *d,hwaddr a) { return (DMAObject){.dma_class=NV_DMA_IN_MEMORY_CLASS,.address=0,.limit=1024*1024-1}; }
void pgraph_vk_ensure_not_in_render_pass(PGRAPHState *pg) { assert(!pg->vk_renderer_state->in_render_pass); }
MemAccessCallback *mem_access_callback_insert(CPUState *cpu,MemoryRegion *mr,hwaddr offset,hwaddr len,MemAccessCallbackFunc func,void *opaque) { abort(); }
void pfifo_kick(NV2AState *d) { pgraph_vk_process_pending_downloads(d); }
VkResult vmaCreateImage(VmaAllocator a,const VkImageCreateInfo *b,const VmaAllocationCreateInfo *c,VkImage *d,VmaAllocation *e,VmaAllocationInfo *f) { abort(); }
void vmaSetAllocationName(VmaAllocator a,VmaAllocation b,const char *name) { }
static VkDevice fixture_device;
static unsigned destroyed_images;
void vmaDestroyImage(VmaAllocator a,VkImage image,VmaAllocation allocation)
{ vkDestroyImage(fixture_device,image,NULL);vkFreeMemory(fixture_device,(VkDeviceMemory)(uintptr_t)allocation,NULL);if(image)destroyed_images++; }


static unsigned image_copies, buffer_copies, image_blits;
static PFN_vkCmdCopyImage real_image_copy;
static PFN_vkCmdCopyBufferToImage real_buffer_copy;
static PFN_vkCmdBlitImage real_blit;
static VKAPI_ATTR void VKAPI_CALL count_copy(VkCommandBuffer c,VkImage a,VkImageLayout al,VkImage b,VkImageLayout bl,uint32_t n,const VkImageCopy *r)
{ image_copies++;real_image_copy(c,a,al,b,bl,n,r); }
static VKAPI_ATTR void VKAPI_CALL count_buffer(VkCommandBuffer c,VkBuffer a,VkImage b,VkImageLayout bl,uint32_t n,const VkBufferImageCopy *r)
{ buffer_copies++;real_buffer_copy(c,a,b,bl,n,r); }
static VKAPI_ATTR void VKAPI_CALL count_blit(VkCommandBuffer c,VkImage a,VkImageLayout al,VkImage b,VkImageLayout bl,uint32_t n,const VkImageBlit *r,VkFilter f)
{ image_blits++;real_blit(c,a,al,b,bl,n,r,f); }
static VkDeviceMemory upload_image(PGRAPHVkState *r,SurfaceBinding *s,bool scratch,unsigned scale)
{
    VkImageCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,
        .format=s->host_fmt.vk_format,.extent={s->width*scale,s->height*scale,1},.mipLevels=1,.arrayLayers=1,
        .samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
        .usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|
            (s->color?VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT:VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)};
    VkImage *image=scratch?&s->image_scratch:&s->image;
    VK_CHECK(vkCreateImage(r->device,&ci,NULL,image));VkMemoryRequirements req;
    vkGetImageMemoryRequirements(r->device,*image,&req);
    VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=ctz32(req.memoryTypeBits)};
    VkDeviceMemory mem;VK_CHECK(vkAllocateMemory(r->device,&ai,NULL,&mem));VK_CHECK(vkBindImageMemory(r->device,*image,mem,0));return mem;
}
#include "retained.inc"
#include "runtime-alias.inc"
#include "retirement.inc"

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    VK_CHECK(volkInitialize());
    const char *layer="VK_LAYER_KHRONOS_validation", *ext=VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    uint32_t n=0; vkEnumerateInstanceLayerProperties(&n,NULL);
    VkLayerProperties *layers=g_new0(VkLayerProperties,n);vkEnumerateInstanceLayerProperties(&n,layers);
    bool validation=false;for(unsigned i=0;i<n;i++)validation|=!strcmp(layers[i].layerName,layer);g_free(layers);
    VkDebugUtilsMessengerCreateInfoEXT dbg={.sType=VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,.pfnUserCallback=debug_message};
    VkInstanceCreateInfo ii={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .enabledLayerCount=validation,.ppEnabledLayerNames=&layer,
        .enabledExtensionCount=validation,.ppEnabledExtensionNames=&ext,.pNext=validation?&dbg:NULL};
    VkInstance instance;VK_CHECK(vkCreateInstance(&ii,NULL,&instance));volkLoadInstance(instance);
    VkDebugUtilsMessengerEXT messenger=VK_NULL_HANDLE;
    if(validation)VK_CHECK(vkCreateDebugUtilsMessengerEXT(instance,&dbg,NULL,&messenger));
    uint32_t count = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &count, NULL)); assert(count);
    VkPhysicalDevice *devices = g_new(VkPhysicalDevice, count);
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &count, devices));
    physical = devices[0];
    const char *preferred = getenv("XEMU_NATIVE_DEVICE");
    if (preferred) {
        physical = VK_NULL_HANDLE;
        for (unsigned i = 0; i < count; i++) {
            VkPhysicalDeviceProperties properties;
            vkGetPhysicalDeviceProperties(devices[i], &properties);
            if (strstr(properties.deviceName, preferred)) { physical = devices[i]; break; }
        }
        assert(physical);
    }
    g_free(devices);
    NV2AState *d = g_new0(NV2AState, 1);
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = g_new0(PGRAPHVkState, 1);
    pg->vk_renderer_state = r;
    qemu_mutex_init(&d->pgraph.lock);qemu_mutex_init(&d->pfifo.lock);
    qemu_event_init(&r->downloads_complete,false);
    vkGetPhysicalDeviceProperties(physical, &r->device_props);
    VkPhysicalDeviceProperties actual = r->device_props;
    printf("device=%s actualX=%u actualInv=%u\n", actual.deviceName,
        actual.limits.maxComputeWorkGroupSize[0],
        actual.limits.maxComputeWorkGroupInvocations);
    printf("vendor=%04x device=%04x api=%u driver=%u type=%u\n",
        actual.vendorID, actual.deviceID, actual.apiVersion,
        actual.driverVersion, actual.deviceType);
    uint32_t family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count, NULL);
    VkQueueFamilyProperties *families = g_new0(VkQueueFamilyProperties, family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count, families);
    uint32_t family;
    for (family = 0; family < family_count; family++) {
        if (families[family].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            break;
        }
    }
    assert(family < family_count);
    g_free(families);
    float priority = 1;
    VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = family,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };
    VkDeviceCreateInfo device_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_info,
    };
    VK_CHECK(vkCreateDevice(physical, &device_info, NULL, &r->device));
    volkLoadDevice(r->device);
    vkGetDeviceQueue(r->device, family, 0, &queue);
    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = family,
    };
    VK_CHECK(vkCreateCommandPool(r->device, &pool_info, NULL, &command_pool));
    fixture_device=r->device;
    g_config.display.quality.surface_scale=1;
    pgraph_vk_init_compute(pg);
    real_image_copy=vkCmdCopyImage;vkCmdCopyImage=count_copy;
    real_buffer_copy=vkCmdCopyBufferToImage;vkCmdCopyBufferToImage=count_buffer;
    real_blit=vkCmdBlitImage;vkCmdBlitImage=count_blit;
    int indices[]={BUFFER_STAGING_SRC,BUFFER_COMPUTE_SRC,BUFFER_COMPUTE_DST,BUFFER_STAGING_DST};
    ComputeFixtureBuffer buffers[4];
    for(unsigned i=0;i<4;i++){buffers[i]=create_buffer(r,2*1024*1024);r->storage_buffers[indices[i]].buffer=buffers[i].buffer;r->storage_buffers[indices[i]].allocation=(VmaAllocation)&buffers[i];r->storage_buffers[indices[i]].buffer_size=2*1024*1024;}
    VkFormat formats[]={VK_FORMAT_R5G6B5_UNORM_PACK16,VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_D16_UNORM,VK_FORMAT_D24_UNORM_S8_UINT,VK_FORMAT_D32_SFLOAT_S8_UINT};
    for(unsigned f=0;f<5;f++){
        VkFormatProperties props;vkGetPhysicalDeviceFormatProperties(physical,formats[f],&props);
        if(f>=2 && !(props.optimalTilingFeatures&VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)){printf("SKIP format=%u unsupported\n",formats[f]);continue;}
        if(f>=2)run_retained_case(d,formats[f]);
    }
    run_native_alias_cases(pg);
    for(unsigned f=3;f<5;f++){VkFormatProperties props;vkGetPhysicalDeviceFormatProperties(physical,formats[f],&props);if(props.optimalTilingFeatures&VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)run_retirement_case(d,formats[f]);}
    pgraph_vk_finalize_surfaces(pg);
    pgraph_vk_finalize_compute(pg);for(unsigned i=0;i<4;i++)destroy_buffer(r,&buffers[i]);
    vkDestroyCommandPool(r->device,command_pool,NULL);vkDestroyDevice(r->device,NULL);if(validation)vkDestroyDebugUtilsMessengerEXT(instance,messenger,NULL);vkDestroyInstance(instance,NULL);printf("RESULT validation=%s errors=%u\n",validation?"enabled":"unavailable",validation_errors);printf("HOST loader_missing_EOS_manifest_errors=%u\n",loader_manifest_errors);assert(!validation_errors);g_free(r);g_free(d);return 0;
}


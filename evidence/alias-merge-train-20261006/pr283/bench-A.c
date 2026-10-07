/* Scoped native qualification of the actual surface.c upload path and surface-compute.c adapter.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/fast-hash.h"
#include "hw/xbox/nv2a/pgraph/vk/renderer.h"
#include <shaderc/shaderc.h>

static VkPhysicalDevice physical;
static VkQueue queue;
static VkCommandPool command_pool;
static unsigned shader_compiles;
static unsigned validation_errors;
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_message(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT *data, void *opaque)
{
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
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
    shaderc_compiler_t compiler = shaderc_compiler_initialize();
    shaderc_compilation_result_t result = shaderc_compile_into_spv(
        compiler, glsl, strlen(glsl), shaderc_compute_shader,
        "production-surface-compute.glsl", "main", NULL);
    if (shaderc_result_get_compilation_status(result) !=
        shaderc_compilation_status_success) {
        fprintf(stderr, "%s", shaderc_result_get_error_message(result));
        abort();
    }
    ShaderModuleInfo *module = g_new0(ShaderModuleInfo, 1);
    VkShaderModuleCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = shaderc_result_get_length(result),
        .pCode = (const uint32_t *)shaderc_result_get_bytes(result),
    };
    VK_CHECK(vkCreateShaderModule(r->device, &info, NULL, &module->module));
    shaderc_result_release(result);
    shaderc_compiler_release(compiler);
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
    VK_CHECK(vkQueueWaitIdle(queue));
    pgraph_vk_compute_finish_complete(pg->vk_renderer_state);
}


VkCommandBuffer pgraph_vk_begin_single_time_commands(PGRAPHState *pg)
{ return pgraph_vk_begin_nondraw_commands(pg); }
void pgraph_vk_end_single_time_commands(PGRAPHState *pg, VkCommandBuffer cmd,
                                        SingleTimeReason reason, uint64_t bytes)
{ pgraph_vk_end_nondraw_commands(pg, cmd); }
NV2AStats g_nv2a_stats;
int nv2a_vk_dgroup_indent;
bool nv2a_vk_text_debug_enabled;
void pgraph_vk_text_debug_printf(const char *format, ...) { (void)format; }
/* VMA allocation boundary backed by actual coherent Vulkan memory. */
VkResult vmaMapMemory(VmaAllocator a, VmaAllocation b, void **ptr)
{ *ptr = ((ComputeFixtureBuffer *)b)->bytes; return VK_SUCCESS; }
void vmaUnmapMemory(VmaAllocator a, VmaAllocation b) { }
VkResult vmaFlushAllocation(VmaAllocator a,VmaAllocation b,VkDeviceSize o,VkDeviceSize n)
{ return VK_SUCCESS; }
#include "/home/codex/xemu-shader-workbench-handoff/xemu-pr282/hw/xbox/nv2a/pgraph/vk/surface.c"
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
static void upload_case(NV2AState *d,VkFormat format,unsigned scale)
{
    PGRAPHState *pg=&d->pgraph;PGRAPHVkState *r=pg->vk_renderer_state;pg->surface_scale_factor=scale;
    bool color=format==VK_FORMAT_R8G8B8A8_UNORM || format==VK_FORMAT_R5G6B5_UNORM_PACK16, packed=format==VK_FORMAT_D24_UNORM_S8_UINT||format==VK_FORMAT_D32_SFLOAT_S8_UINT;
    SurfaceBinding s={.color=color,.width=32,.height=32,.pitch=32*((format==VK_FORMAT_D16_UNORM || format==VK_FORMAT_R5G6B5_UNORM_PACK16)?2:4)+16,.upload_pending=true};
    s.fmt.bytes_per_pixel=(format==VK_FORMAT_D16_UNORM || format==VK_FORMAT_R5G6B5_UNORM_PACK16)?2:4;s.host_fmt.vk_format=format;
    s.host_fmt.aspect=color?VK_IMAGE_ASPECT_COLOR_BIT:VK_IMAGE_ASPECT_DEPTH_BIT|(packed?VK_IMAGE_ASPECT_STENCIL_BIT:0);
    s.size=s.pitch*s.height;uint8_t *guest=g_malloc0(s.size);d->vram_ptr=guest;
    for(unsigned y=0;y<32;y++)for(unsigned x=0;x<32;x++){
        unsigned i=y*32+x;uint32_t value=color?(0xff003300u|((i*13)&255)):packed?(((i*1237)&0xffffff)<<8)|(i&255):i*31;
        memcpy(guest+y*s.pitch+x*s.fmt.bytes_per_pixel,&value,s.fmt.bytes_per_pixel);
    }
    VkDeviceMemory final=upload_image(r,&s,false,scale),scratch=upload_image(r,&s,true,packed?scale:1);
    s.image_scratch_current_layout=VK_IMAGE_LAYOUT_UNDEFINED;
    VkCommandBuffer cmd=pgraph_vk_begin_nondraw_commands(pg);
    VkImageLayout attachment=color?VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    pgraph_vk_transition_image_layout(pg,cmd,s.image,format,VK_IMAGE_LAYOUT_UNDEFINED,attachment);
    pgraph_vk_end_nondraw_commands(pg,cmd);
    for(unsigned generation=0;generation<32;generation++) {
    if(generation){for(unsigned y=0;y<32;y++)for(unsigned x=0;x<32;x++){uint32_t value=0;uint8_t *at=guest+y*s.pitch+x*s.fmt.bytes_per_pixel;memcpy(&value,at,s.fmt.bytes_per_pixel);value^=0x00003109;memcpy(at,&value,s.fmt.bytes_per_pixel);}s.upload_pending=true;}
    image_copies=buffer_copies=image_blits=0;
    int64_t start=g_get_monotonic_time();
    assert(pgraph_vk_upload_surface_data(d,&s,false));
    int64_t elapsed=g_get_monotonic_time()-start;
    assert(s.initialized&&!s.upload_pending);
    unsigned copies=image_copies,blits=image_blits,uploads=buffer_copies;
    ComputeFixtureBuffer read=create_buffer(r,32*32*scale*scale*8);
    cmd=pgraph_vk_begin_nondraw_commands(pg);
    pgraph_vk_transition_image_layout(pg,cmd,s.image,format,attachment,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    unsigned pixels=32*32*scale*scale,depthbytes=pixels*s.fmt.bytes_per_pixel;
    VkBufferImageCopy regions[2]={
        {.imageSubresource={color?VK_IMAGE_ASPECT_COLOR_BIT:VK_IMAGE_ASPECT_DEPTH_BIT,0,0,1},.imageExtent={32*scale,32*scale,1}},
        {.bufferOffset=depthbytes,.imageSubresource={VK_IMAGE_ASPECT_STENCIL_BIT,0,0,1},.imageExtent={32*scale,32*scale,1}}};
    vkCmdCopyImageToBuffer(cmd,s.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,read.buffer,packed?2:1,regions);
    VkMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,NULL,0,NULL);
    pgraph_vk_end_nondraw_commands(pg,cmd);
    unsigned zero_canonicalizations=0;
    for(unsigned y=0;y<32*scale;y++)for(unsigned x=0;x<32*scale;x++){
        unsigned i=y*32*scale+x;uint32_t input=0,actual=0;memcpy(&input,guest+(y/scale)*s.pitch+(x/scale)*s.fmt.bytes_per_pixel,s.fmt.bytes_per_pixel);
        memcpy(&actual,read.bytes+i*s.fmt.bytes_per_pixel,s.fmt.bytes_per_pixel);
        if(packed){uint32_t expected=input>>8;if(format==VK_FORMAT_D32_SFLOAT_S8_UINT){float f=(float)expected/16777216.f;memcpy(&expected,&f,4);expected++;}else actual&=0xffffff;if(format==VK_FORMAT_D32_SFLOAT_S8_UINT && expected==1 && actual==0){zero_canonicalizations++;expected=0;}if(actual!=expected){fprintf(stderr,"DEPTH MISMATCH format=%u scale=%u generation=%u pixel=%u input=%08x expected=%08x actual=%08x\n",format,scale,generation,i,input,expected,actual);}assert(actual==expected);assert(read.bytes[depthbytes+i]==(input&255));}
        else assert(actual==input);
    }
    printf("PASS upload format=%u scale=%u pixels=%u bufferToImage=%u imageCopy=%u blit=%u host_us=%" PRId64 "\n",format,scale,pixels,uploads,copies,blits,elapsed);
    printf("ZERO_DEPTH canonicalizations=%u generation=%u\n",zero_canonicalizations,generation);
    assert(uploads==1);
    /* Main reference is allowed its extra copy; candidate's expected topology
     * is asserted by the result analyzer across reference/candidate. */
    destroy_buffer(r,&read);
    cmd=pgraph_vk_begin_nondraw_commands(pg);
    pgraph_vk_transition_image_layout(pg,cmd,s.image,format,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,attachment);
    pgraph_vk_end_nondraw_commands(pg,cmd);
    }
    vkDestroyImage(r->device,s.image,NULL);vkDestroyImage(r->device,s.image_scratch,NULL);
    vkFreeMemory(r->device,final,NULL);vkFreeMemory(r->device,scratch,NULL);g_free(guest);d->vram_ptr=NULL;
}
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
    uint32_t count = 1;
    VkResult result = vkEnumeratePhysicalDevices(instance, &count, &physical);
    assert(result == VK_SUCCESS || result == VK_INCOMPLETE);
    assert(count);
    NV2AState *d = g_new0(NV2AState, 1);
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = g_new0(PGRAPHVkState, 1);
    pg->vk_renderer_state = r;
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
    pgraph_vk_init_compute(pg);
    real_image_copy=vkCmdCopyImage;vkCmdCopyImage=count_copy;
    real_buffer_copy=vkCmdCopyBufferToImage;vkCmdCopyBufferToImage=count_buffer;
    real_blit=vkCmdBlitImage;vkCmdBlitImage=count_blit;
    int indices[]={BUFFER_STAGING_SRC,BUFFER_COMPUTE_SRC,BUFFER_COMPUTE_DST};
    ComputeFixtureBuffer buffers[3];
    for(unsigned i=0;i<3;i++){buffers[i]=create_buffer(r,1024*1024);r->storage_buffers[indices[i]].buffer=buffers[i].buffer;r->storage_buffers[indices[i]].allocation=(VmaAllocation)&buffers[i];r->storage_buffers[indices[i]].buffer_size=1024*1024;}
    VkFormat formats[]={VK_FORMAT_R5G6B5_UNORM_PACK16,VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_D16_UNORM,VK_FORMAT_D24_UNORM_S8_UINT,VK_FORMAT_D32_SFLOAT_S8_UINT};
    for(unsigned f=0;f<5;f++){
        VkFormatProperties props;vkGetPhysicalDeviceFormatProperties(physical,formats[f],&props);
        if(f>=2 && !(props.optimalTilingFeatures&VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)){printf("SKIP format=%u unsupported\n",formats[f]);continue;}
        for(unsigned scale=1;scale<=2;scale++)upload_case(d,formats[f],scale);
    }
    pgraph_vk_finalize_compute(pg);for(unsigned i=0;i<3;i++)destroy_buffer(r,&buffers[i]);
    vkDestroyCommandPool(r->device,command_pool,NULL);vkDestroyDevice(r->device,NULL);if(validation)vkDestroyDebugUtilsMessengerEXT(instance,messenger,NULL);vkDestroyInstance(instance,NULL);printf("RESULT validation=%s errors=%u\n",validation?"enabled":"unavailable",validation_errors);assert(!validation_errors);g_free(r);g_free(d);return 0;
}


/* Native GPU oracle for the production graphics pipeline recipe.
 * Allocation/submission are fixture-owned; guest register decoding, pipeline
 * state and render pass creation are actual draw.c. No guest media needed.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/draw.c"
#include <shaderc/shaderc.h>

int nv2a_vk_dgroup_indent;
bool nv2a_vk_text_debug_enabled;
void pgraph_vk_text_debug_printf(const char *format, ...) { (void)format; }
static VkPhysicalDevice gpu;
static VkQueue queue;
static VkCommandPool pool;
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
static VkDeviceMemory memory(VkDevice dev, VkMemoryRequirements req,
                             VkMemoryPropertyFlags flags)
{
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(gpu, &props);
    unsigned i;
    for (i = 0; i < props.memoryTypeCount; i++) {
        if ((req.memoryTypeBits & (1u << i)) &&
            (props.memoryTypes[i].propertyFlags & flags) == flags) break;
    }
    assert(i < props.memoryTypeCount);
    VkMemoryAllocateInfo info = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size, .memoryTypeIndex = i };
    VkDeviceMemory mem;
    VK_CHECK(vkAllocateMemory(dev, &info, NULL, &mem));
    return mem;
}
typedef struct Image { VkImage image; VkImageView view; VkDeviceMemory mem; } Image;
static Image image(VkDevice dev, VkFormat fmt, VkImageAspectFlags aspect)
{
    Image result;
    VkImageCreateInfo info = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D, .format = fmt, .extent = {4,4,1},
        .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            (aspect == VK_IMAGE_ASPECT_COLOR_BIT ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT :
                                                   VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) };
    VK_CHECK(vkCreateImage(dev, &info, NULL, &result.image));
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(dev, result.image, &req);
    result.mem = memory(dev, req, 0);
    VK_CHECK(vkBindImageMemory(dev, result.image, result.mem, 0));
    VkImageViewCreateInfo view = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = result.image, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = fmt,
        .subresourceRange = {aspect,0,1,0,1} };
    VK_CHECK(vkCreateImageView(dev, &view, NULL, &result.view));
    return result;
}
static void image_barrier(VkCommandBuffer cmd, Image *img, VkImageAspectFlags aspect,
                    VkImageLayout old, VkImageLayout next)
{
    VkImageMemoryBarrier b = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = old == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_MEMORY_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
        .oldLayout = old, .newLayout = next,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = img->image, .subresourceRange = {aspect,0,1,0,1} };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0,NULL,0,NULL,1,&b);
}
static VkShaderModule shader(VkDevice dev, const char *source, shaderc_shader_kind kind)
{
    shaderc_compiler_t c = shaderc_compiler_initialize();
    shaderc_compilation_result_t b = shaderc_compile_into_spv(c, source, strlen(source),
        kind, "zeta-oracle.glsl", "main", NULL);
    assert(shaderc_result_get_compilation_status(b) == shaderc_compilation_status_success);
    VkShaderModuleCreateInfo info = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = shaderc_result_get_length(b), .pCode = (void *)shaderc_result_get_bytes(b)};
    VkShaderModule mod;
    VK_CHECK(vkCreateShaderModule(dev,&info,NULL,&mod));
    shaderc_result_release(b); shaderc_compiler_release(c);
    return mod;
}
static void run_case(PGRAPHState *pg, VkFormat format, unsigned mode)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    /* 0 readonly depth; 1 depth write; 2 stencil readonly; 3 stencil write;
     * 4 explicit clear with ordinary writes disabled; 5 rejected depth. */
    const char *names[] = {"depth-readonly","depth-write","stencil-readonly",
                          "stencil-write","explicit-clear","depth-reject"};
    ShaderModuleInfo vs = {0}, ps = {0}, gs = {0};
    vs.module = shader(r->device, "#version 450\nvoid main(){float x=-1.+float((gl_VertexIndex&1)<<2);float y=-1.+float((gl_VertexIndex&2)<<1);gl_Position=vec4(x,y,0.25,1);}", shaderc_vertex_shader);
    ps.module = shader(r->device, "#version 450\nlayout(location=0) out vec4 c;void main(){c=vec4(1,0,0,1);}", shaderc_fragment_shader);
    gs.module = shader(r->device, "#version 450\nlayout(triangles) in; layout(triangle_strip,max_vertices=3) out; void main(){for(int i=0;i<3;i++){gl_Position=gl_in[i].gl_Position;EmitVertex();}EndPrimitive();}", shaderc_geometry_shader);
    ShaderBinding binding = {0}; PipelineKey key = {0};
    binding.state.geom.primitive_mode = PRIM_TYPE_TRIANGLES;
    binding.state.geom.polygon_front_mode = binding.state.geom.polygon_back_mode = POLY_MODE_FILL;
    binding.geom.module_info = &gs; binding.vsh.module_info = &vs; binding.psh.module_info = &ps;
    binding.fragment_route = PGRAPH_VK_FRAGMENT_SPECIALIZED;
    key.fragment_route = binding.fragment_route; key.shader_state = binding.state;
    key.render_pass_state.color_format = VK_FORMAT_R8G8B8A8_UNORM;
    key.render_pass_state.zeta_format = format;
    key.regs[1] = NV_PGRAPH_CONTROL_0_ZENABLE | NV_PGRAPH_CONTROL_0_RED_WRITE_ENABLE |
        NV_PGRAPH_CONTROL_0_GREEN_WRITE_ENABLE | NV_PGRAPH_CONTROL_0_BLUE_WRITE_ENABLE |
        NV_PGRAPH_CONTROL_0_ALPHA_WRITE_ENABLE;
    SET_MASK(key.regs[1], NV_PGRAPH_CONTROL_0_ZFUNC, 1); /* LESS */
    if (mode == 1) key.regs[1] |= NV_PGRAPH_CONTROL_0_ZWRITEENABLE;
    if (mode == 2 || mode == 3) {
        SET_MASK(key.regs[2], NV_PGRAPH_CONTROL_1_STENCIL_TEST_ENABLE, 1);
        SET_MASK(key.regs[2], NV_PGRAPH_CONTROL_1_STENCIL_FUNC, 7); /* ALWAYS */
        SET_MASK(key.regs[2], NV_PGRAPH_CONTROL_1_STENCIL_MASK_READ, 255);
        SET_MASK(key.regs[2], NV_PGRAPH_CONTROL_1_STENCIL_MASK_WRITE, 255);
        SET_MASK(key.regs[2], NV_PGRAPH_CONTROL_1_STENCIL_REF, 0xa5);
        SET_MASK(key.regs[3], NV_PGRAPH_CONTROL_2_STENCIL_OP_ZPASS, 3); /* REPLACE */
        if (mode == 3) key.regs[1] |= NV_PGRAPH_CONTROL_0_STENCIL_WRITE_ENABLE;
    }
    PGRAPHVkGraphicsPipelineRecipe recipe;
    assert(prepare_graphics_pipeline_recipe(pg, &key, &binding, &recipe));
    VkPipeline pipeline;
    VK_CHECK(vkCreateGraphicsPipelines(r->device,VK_NULL_HANDLE,1,&recipe.info,NULL,&pipeline));
    VkImageAspectFlags ds = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    Image color = image(r->device,key.render_pass_state.color_format,VK_IMAGE_ASPECT_COLOR_BIT);
    Image depth = image(r->device,format,ds);
    VkImageView views[] = {color.view,depth.view};
    VkFramebufferCreateInfo fi = {.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass=recipe.render_pass,.attachmentCount=2,.pAttachments=views,
        .width=4,.height=4,.layers=1};
    VkFramebuffer framebuffer;
    VK_CHECK(vkCreateFramebuffer(r->device,&fi,NULL,&framebuffer));
    VkBuffer buf; VkDeviceMemory mem; void *mapped;
    VkBufferCreateInfo bi={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=256,
        .usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    VK_CHECK(vkCreateBuffer(r->device,&bi,NULL,&buf));
    VkMemoryRequirements req; vkGetBufferMemoryRequirements(r->device,buf,&req);
    mem=memory(r->device,req,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK_CHECK(vkBindBufferMemory(r->device,buf,mem,0));
    VK_CHECK(vkMapMemory(r->device,mem,0,VK_WHOLE_SIZE,0,&mapped));
    VkCommandBuffer cmd;
    VkCommandBufferAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
    VK_CHECK(vkAllocateCommandBuffers(r->device,&ai,&cmd));
    VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VK_CHECK(vkBeginCommandBuffer(cmd,&begin));
    image_barrier(cmd,&color,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    image_barrier(cmd,&depth,ds,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkClearColorValue green={.float32={0,1,0,1}};
    VkImageSubresourceRange cr={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}, dr={ds,0,1,0,1};
    VkClearDepthStencilValue initial={mode == 5 ? 0.0f : 1.0f,0x5a};
    vkCmdClearColorImage(cmd,color.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&green,1,&cr);
    vkCmdClearDepthStencilImage(cmd,depth.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&initial,1,&dr);
    image_barrier(cmd,&color,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    image_barrier(cmd,&depth,ds,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    VkRenderPassBeginInfo rp={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass=recipe.render_pass,.framebuffer=framebuffer,.renderArea={{0,0},{4,4}}};
    vkCmdBeginRenderPass(cmd,&rp,VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
    VkViewport vp={0,0,4,4,0,1}; VkRect2D sc={{0,0},{4,4}};
    vkCmdSetViewport(cmd,0,1,&vp); vkCmdSetScissor(cmd,0,1,&sc);
    if (mode == 4) {
        VkClearAttachment clear={.aspectMask=ds,.clearValue.depthStencil={0.5f,0x33}};
        VkClearRect rect={.rect=sc,.layerCount=1};
        vkCmdClearAttachments(cmd,1,&clear,1,&rect);
    } else vkCmdDraw(cmd,3,1,0,0);
    vkCmdEndRenderPass(cmd);
    image_barrier(cmd,&color,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    image_barrier(cmd,&depth,ds,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy copies[3]={
        {.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={4,4,1}},
        {.bufferOffset=64,.imageSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,0,0,1},.imageExtent={4,4,1}},
        {.bufferOffset=128,.imageSubresource={VK_IMAGE_ASPECT_STENCIL_BIT,0,0,1},.imageExtent={4,4,1}}};
    vkCmdCopyImageToBuffer(cmd,color.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buf,1,copies);
    vkCmdCopyImageToBuffer(cmd,depth.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buf,2,copies+1);
    VkMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,NULL,0,NULL);
    VK_CHECK(vkEndCommandBuffer(cmd));
    VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cmd};
    VK_CHECK(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE)); VK_CHECK(vkQueueWaitIdle(queue));
    for(unsigned i=0;i<16;i++) {
        uint8_t *bytes=mapped;
        bool red=mode!=4 && mode!=5;
        assert(bytes[4*i]==(red?255:0) && bytes[4*i+1]==(red?0:255) && bytes[4*i+2]==0 && bytes[4*i+3]==255);
        float expected=mode==1 ? 0.25f : mode==4 ? 0.5f : mode==5 ? 0.f : 1.f;
        if(format==VK_FORMAT_D32_SFLOAT_S8_UINT) {float value;memcpy(&value,bytes+64+4*i,4);assert(value==expected);}
        else {uint32_t value;memcpy(&value,bytes+64+4*i,4);value &= 0xffffff;assert(abs((int)value-(int)llround((double)expected*16777215.0))<=1);}
        assert(bytes[128+i]==(mode==3?0xa5:mode==4?0x33:0x5a));
    }
    printf("PASS format=%u case=%s pixels=16 color/depth/stencil oracle\n",format,names[mode]);
    vkFreeCommandBuffers(r->device,pool,1,&cmd); vkUnmapMemory(r->device,mem);
    vkDestroyBuffer(r->device,buf,NULL);vkFreeMemory(r->device,mem,NULL);
    vkDestroyFramebuffer(r->device,framebuffer,NULL);
    Image images[]={color,depth};for(unsigned i=0;i<2;i++){vkDestroyImageView(r->device,images[i].view,NULL);vkDestroyImage(r->device,images[i].image,NULL);vkFreeMemory(r->device,images[i].mem,NULL);}
    vkDestroyPipeline(r->device,pipeline,NULL);vkDestroyPipelineLayout(r->device,recipe.layout,NULL);
    vkDestroyShaderModule(r->device,gs.module,NULL);vkDestroyShaderModule(r->device,vs.module,NULL);vkDestroyShaderModule(r->device,ps.module,NULL);
}
int main(void)
{
    setvbuf(stdout,NULL,_IOLBF,0); VK_CHECK(volkInitialize());
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
    n=1;VkResult enumerated=vkEnumeratePhysicalDevices(instance,&n,&gpu);assert(n&&(enumerated==VK_SUCCESS||enumerated==VK_INCOMPLETE));
    PGRAPHState *pg=g_new0(PGRAPHState,1);PGRAPHVkState *r=g_new0(PGRAPHVkState,1);pg->vk_renderer_state=r;
    vkGetPhysicalDeviceProperties(gpu,&r->device_props);
    printf("device=%s vendor=%04x device=%04x driver=%u type=%u validation=%s\n",r->device_props.deviceName,r->device_props.vendorID,r->device_props.deviceID,r->device_props.driverVersion,r->device_props.deviceType,validation?"enabled":"unavailable");
    vkGetPhysicalDeviceQueueFamilyProperties(gpu,&n,NULL);VkQueueFamilyProperties *families=g_new0(VkQueueFamilyProperties,n);vkGetPhysicalDeviceQueueFamilyProperties(gpu,&n,families);
    unsigned family;for(family=0;family<n;family++)if(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT)break;assert(family<n);g_free(families);
    float priority=1;VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
    VkPhysicalDeviceFeatures features;vkGetPhysicalDeviceFeatures(gpu,&features);assert(features.depthClamp && features.geometryShader);features=(VkPhysicalDeviceFeatures){.depthClamp=VK_TRUE,.geometryShader=VK_TRUE};
    VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi,.pEnabledFeatures=&features};
    VK_CHECK(vkCreateDevice(gpu,&di,NULL,&r->device));volkLoadDevice(r->device);vkGetDeviceQueue(r->device,family,0,&queue);
    VkCommandPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=family};VK_CHECK(vkCreateCommandPool(r->device,&pi,NULL,&pool));
    VkDescriptorSetLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};VK_CHECK(vkCreateDescriptorSetLayout(r->device,&li,NULL,&r->descriptor_set_layout));
    init_render_passes(r);
    VkFormat formats[]={VK_FORMAT_D24_UNORM_S8_UINT,VK_FORMAT_D32_SFLOAT_S8_UINT};unsigned passed=0;
    for(unsigned f=0;f<2;f++) {
        VkFormatProperties props;vkGetPhysicalDeviceFormatProperties(gpu,formats[f],&props);
        if(!(props.optimalTilingFeatures&VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)){printf("SKIP unsupported format=%u\n",formats[f]);continue;}
        for(unsigned mode=0;mode<6;mode++){run_case(pg,formats[f],mode);passed++;}
    }
    finalize_render_passes(r);vkDestroyDescriptorSetLayout(r->device,r->descriptor_set_layout,NULL);
    vkDestroyCommandPool(r->device,pool,NULL);vkDestroyDevice(r->device,NULL);
    if(validation)vkDestroyDebugUtilsMessengerEXT(instance,messenger,NULL);vkDestroyInstance(instance,NULL);
    printf("RESULT cases=%u validation_errors=%u\n",passed,validation_errors);assert(passed>=6 && !validation_errors);g_free(r);g_free(pg);return 0;
}

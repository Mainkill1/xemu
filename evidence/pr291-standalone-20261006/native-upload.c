/* Actual ordered uploader, VMA flush and Vulkan image/readback qualification. */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/renderer.h"
#include "hw/xbox/nv2a/pgraph/vk/surface.c"
#include "hw/xbox/nv2a/pgraph/vk/image.c"

NV2AStats g_nv2a_stats;
static unsigned submits;

void pgraph_vk_ensure_buffer_capacity(PGRAPHState *pg, int index, VkDeviceSize size)
{
    assert(pg->vk_renderer_state->storage_buffers[index].buffer_size >= size);
}

bool pgraph_vk_buffer_has_space_for(PGRAPHState *pg, int index,
                                    VkDeviceSize size, VkDeviceAddress alignment)
{
    StorageBuffer *b = &pg->vk_renderer_state->storage_buffers[index];
    VkDeviceSize offset = ROUND_UP(b->buffer_offset, alignment);
    return offset <= b->buffer_size && size <= b->buffer_size - offset;
}

VkCommandBuffer pgraph_vk_begin_nondraw_commands(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    if (!r->in_command_buffer) {
        VkCommandBufferBeginInfo begin = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        VK_CHECK(vkBeginCommandBuffer(r->command_buffer, &begin));
        r->in_command_buffer = true;
    }
    return r->command_buffer;
}

void pgraph_vk_end_nondraw_commands(PGRAPHState *pg, VkCommandBuffer cmd)
{
    /* Keep uploads unsubmitted until ring pressure or explicit final drain. */
}

void pgraph_vk_finish(PGRAPHState *pg, FinishReason reason)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    if (!r->in_command_buffer) {
        return;
    }
    VK_CHECK(vkEndCommandBuffer(r->command_buffer));
    VkSubmitInfo submit = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1, .pCommandBuffers = &r->command_buffer };
    VK_CHECK(vkQueueSubmit(r->queue, 1, &submit, r->command_buffer_fence));
    VK_CHECK(vkWaitForFences(r->device, 1, &r->command_buffer_fence, true, UINT64_MAX));
    VK_CHECK(vkResetFences(r->device, 1, &r->command_buffer_fence));
    VK_CHECK(vkResetCommandBuffer(r->command_buffer, 0));
    r->in_command_buffer = false;
    r->storage_buffers[BUFFER_TEXTURE_STAGING].buffer_offset = 0;
    submits++;
}

static void make_buffer(PGRAPHVkState *r, StorageBuffer *b, size_t size)
{
    VkBufferCreateInfo info = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size, .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT };
    VmaAllocationCreateInfo alloc = { .usage = VMA_MEMORY_USAGE_AUTO,
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT };
    VmaAllocationInfo result;
    VK_CHECK(vmaCreateBuffer(r->allocator, &info, &alloc, &b->buffer, &b->allocation, &result));
    b->mapped = result.pMappedData;
    b->buffer_size = size;
}

static unsigned morton(unsigned x, unsigned y)
{
    unsigned address = 0;
    for (unsigned bit = 0; bit < 5; bit++) {
        address |= ((x >> bit) & 1) << (bit * 2);
        address |= ((y >> bit) & 1) << (bit * 2 + 1);
    }
    return address;
}

static uint8_t pixel(unsigned x, unsigned y, unsigned channel, unsigned generation)
{
    return x * 13 + y * 19 + channel * 29 + generation * 7;
}

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    VK_CHECK(volkInitialize());
    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_1 };
    VkInstanceCreateInfo ii = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
    NV2AState *d = g_new0(NV2AState, 1);
    PGRAPHVkState *r = g_new0(PGRAPHVkState, 1);
    d->pgraph.vk_renderer_state = r;
    d->vram_ptr = g_malloc0(4096);
    VK_CHECK(vkCreateInstance(&ii, NULL, &r->instance));
    volkLoadInstance(r->instance);
    uint32_t count = 1;
    VkResult result = vkEnumeratePhysicalDevices(r->instance, &count, &r->physical_device);
    assert(count && (result == VK_SUCCESS || result == VK_INCOMPLETE));
    vkGetPhysicalDeviceProperties(r->physical_device, &r->device_props);
    printf("device=%s vendor=%x deviceid=%x driver=%u\n", r->device_props.deviceName,
           r->device_props.vendorID, r->device_props.deviceID, r->device_props.driverVersion);
    uint32_t family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(r->physical_device, &family_count, NULL);
    VkQueueFamilyProperties *families = g_new0(VkQueueFamilyProperties, family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(r->physical_device, &family_count, families);
    uint32_t family = 0;
    while (family < family_count && !(families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT)) family++;
    assert(family < family_count);
    g_free(families);
    float priority = 1;
    VkDeviceQueueCreateInfo qi = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority };
    VkDeviceCreateInfo di = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &qi };
    VK_CHECK(vkCreateDevice(r->physical_device, &di, NULL, &r->device));
    volkLoadDevice(r->device);
    vkGetDeviceQueue(r->device, family, 0, &r->queue);
    VmaAllocatorCreateInfo ai = { .vulkanApiVersion = VK_API_VERSION_1_1,
        .instance = r->instance, .physicalDevice = r->physical_device, .device = r->device };
    VmaVulkanFunctions functions;
    VK_CHECK(vmaImportVulkanFunctionsFromVolk(&ai, &functions));
    ai.pVulkanFunctions = &functions;
    VK_CHECK(vmaCreateAllocator(&ai, &r->allocator));
    VkCommandPoolCreateInfo pi = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = family, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT };
    VK_CHECK(vkCreateCommandPool(r->device, &pi, NULL, &r->command_pool));
    VkCommandBufferAllocateInfo ci = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = r->command_pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    VK_CHECK(vkAllocateCommandBuffers(r->device, &ci, &r->command_buffer));
    VkFenceCreateInfo fi = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VK_CHECK(vkCreateFence(r->device, &fi, NULL, &r->command_buffer_fence));
    StorageBuffer *staging = &r->storage_buffers[BUFFER_TEXTURE_STAGING];
    StorageBuffer output = { 0 };
    make_buffer(r, staging, 16384);
    make_buffer(r, &output, 20 * 4096);
    SurfaceBinding surface = { .width = 32, .height = 32,
        .host_fmt.vk_format = VK_FORMAT_R8G8B8A8_UNORM };
    VkImageCreateInfo image = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D, .format = surface.host_fmt.vk_format,
        .extent = {32,32,1}, .mipLevels = 1, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT };
    VmaAllocationCreateInfo ia = { .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE };
    VK_CHECK(vmaCreateImage(r->allocator, &image, &ia, &surface.image, &surface.allocation, NULL));
    VkCommandBuffer cmd = pgraph_vk_begin_nondraw_commands(&d->pgraph);
    pgraph_vk_transition_image_layout(&d->pgraph, cmd, surface.image, surface.host_fmt.vk_format,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    r->perf.enabled = true;
    for (unsigned generation = 0; generation < 20; generation++) {
        for (unsigned y = 0; y < 32; y++) for (unsigned x = 0; x < 32; x++)
            for (unsigned c = 0; c < 4; c++) d->vram_ptr[morton(x,y)*4+c] = pixel(x,y,c,generation);
        assert(upload_small_swizzled_color(&d->pgraph, &surface));
        /* Destroy source immediately, before any queued GPU copy may execute. */
        memset(d->vram_ptr, 0xee, 4096);
        cmd = pgraph_vk_begin_nondraw_commands(&d->pgraph);
        pgraph_vk_transition_image_layout(&d->pgraph, cmd, surface.image, surface.host_fmt.vk_format,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkBufferImageCopy region = { .bufferOffset = generation * 4096,
            .imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .imageSubresource.layerCount = 1, .imageExtent = {32,32,1} };
        vkCmdCopyImageToBuffer(cmd, surface.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, output.buffer, 1, &region);
        VkMemoryBarrier barrier = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1,&barrier,0,NULL,0,NULL);
        pgraph_vk_transition_image_layout(&d->pgraph, cmd, surface.image, surface.host_fmt.vk_format,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    }
    pgraph_vk_finish(&d->pgraph, VK_FINISH_REASON_FLUSH);
    VK_CHECK(vmaInvalidateAllocation(r->allocator, output.allocation, 0, 20 * 4096));
    for (unsigned generation=0; generation<20; generation++) {
        for (unsigned y=0;y<32;y++) for(unsigned x=0;x<32;x++) for(unsigned c=0;c<4;c++)
            assert(output.mapped[generation*4096+(y*32+x)*4+c] == pixel(x,y,c,generation));
    }
    assert(submits == 5 && r->perf.small_color_upload_ring_finishes == 4);
    printf("PASS 20 immutable generations, 81920 byte checks, 4 ring turnovers, 5 real submissions\n");
    vmaDestroyImage(r->allocator,surface.image,surface.allocation);
    vmaDestroyBuffer(r->allocator,staging->buffer,staging->allocation);
    vmaDestroyBuffer(r->allocator,output.buffer,output.allocation);
    vkDestroyFence(r->device,r->command_buffer_fence,NULL);
    vkDestroyCommandPool(r->device,r->command_pool,NULL);
    vmaDestroyAllocator(r->allocator);
    vkDestroyDevice(r->device,NULL);
    vkDestroyInstance(r->instance,NULL);
    g_free(d->vram_ptr);g_free(r);g_free(d);
    return 0;
}

/* Actual ordered uploader, VMA flush and Vulkan image/readback qualification. */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/renderer.h"
#include "hw/xbox/nv2a/pgraph/vk/texture.c"
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

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    VK_CHECK(volkInitialize());
    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_1 };
    VkInstanceCreateInfo ii = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
    NV2AState *d = g_new0(NV2AState, 1);
    PGRAPHVkState *r = g_new0(PGRAPHVkState, 1);
    d->pgraph.vk_renderer_state = r;
    d->pgraph.surface_scale_factor = 1;
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

    PGRAPHState *pg = &d->pgraph;
    r->perf.enabled = true;
    unsigned long checked = 0;
    for (unsigned format_index = 0; format_index < 2; format_index++) {
        unsigned bpp = format_index ? 4 : 2;
        unsigned face_bytes = 128 * 128 * bpp;
        VkFormat format = format_index ? VK_FORMAT_B8G8R8A8_UNORM :
                                        VK_FORMAT_R5G6B5_UNORM_PACK16;
        StorageBuffer input = { 0 }, output = { 0 };
        make_buffer(r, &input, 7 * face_bytes);
        make_buffer(r, &output, 12 * face_bytes);
        for (unsigned face = 0; face < 7; face++) {
            for (unsigned y = 0; y < 128; y++) {
                for (unsigned x = 0; x < 128; x++) {
                    for (unsigned c = 0; c < bpp; c++) {
                        input.mapped[face * face_bytes + (y * 128 + x) * bpp + c]
                            = x * 13 + y * 29 + c * 53 + face * 71;
                    }
                }
            }
        }
        VK_CHECK(vmaFlushAllocation(r->allocator, input.allocation, 0,
                                    7 * face_bytes));
        SurfaceBinding sources[6] = { 0 };
        SurfaceBinding *faces[6];
        TextureBinding texture = { .key = {
            .texture_vram_offset = 0x100000,
            .texture_length = 6 * face_bytes,
            .state = { .cubemap = true, .dimensionality = 2,
                .width = 128, .height = 128, .depth = 1, .levels = 1,
                .storage_levels = 1,
                .color_format = format_index ?
                    NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8R8G8B8 :
                    NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R5G6B5 }
        }};
        VkImageCreateInfo image = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D, .format = format,
            .extent = { 128, 128, 1 }, .mipLevels = 1, .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                     VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                     VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                     VK_IMAGE_USAGE_SAMPLED_BIT
        };
        VmaAllocationCreateInfo ia = {
            .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
        };
        VkCommandBuffer cmd = pgraph_vk_begin_nondraw_commands(pg);
        for (unsigned i = 0; i < 6; i++) {
            faces[i] = &sources[i];
            sources[i] = (SurfaceBinding) {
                .vram_addr = 0x100000 + i * face_bytes,
                .width = 128, .height = 128, .color = true,
                .lifetime_id = i + 1,
                .host_fmt = { .vk_format = format,
                              .aspect = VK_IMAGE_ASPECT_COLOR_BIT }
            };
            VK_CHECK(vmaCreateImage(r->allocator, &image, &ia,
                                   &sources[i].image, &sources[i].allocation,
                                   NULL));
            pgraph_vk_transition_image_layout(pg, cmd, sources[i].image, format,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkBufferImageCopy upload = {
                .bufferOffset = i * face_bytes,
                .imageSubresource = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                      .layerCount = 1 },
                .imageExtent = { 128, 128, 1 }
            };
            vkCmdCopyBufferToImage(cmd, input.buffer, sources[i].image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &upload);
            pgraph_vk_transition_image_layout(pg, cmd, sources[i].image, format,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        }
        image.arrayLayers = 6;
        image.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        VK_CHECK(vmaCreateImage(r->allocator, &image, &ia, &texture.image,
                               &texture.allocation, NULL));
        for (unsigned generation = 0; generation < 2; generation++) {
            if (generation) {
                pgraph_vk_transition_image_layout(pg, cmd, sources[4].image,
                    format, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                VkBufferImageCopy upload = {
                    .bufferOffset = 6 * face_bytes,
                    .imageSubresource = {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1
                    }, .imageExtent = {128, 128, 1}
                };
                vkCmdCopyBufferToImage(cmd, input.buffer, sources[4].image,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &upload);
                pgraph_vk_transition_image_layout(pg, cmd, sources[4].image,
                    format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
                sources[4].draw_time++;
            }
            copy_cubemap_surfaces(pg, faces, &texture);
            copy_cubemap_surfaces(pg, faces, &texture);
            pgraph_vk_transition_image_layout(pg, cmd, texture.image, format,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            pgraph_vk_transition_image_layout(pg, cmd, texture.image, format,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            VkBufferImageCopy readback = {
                .bufferOffset = generation * 6 * face_bytes,
                .imageSubresource = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 6
                }, .imageExtent = {128, 128, 1}
            };
            vkCmdCopyImageToBuffer(cmd, texture.image,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, output.buffer, 1,
                &readback);
            pgraph_vk_transition_image_layout(pg, cmd, texture.image, format,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            pgraph_vk_transition_image_layout(pg, cmd, texture.image, format,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
        VkMemoryBarrier barrier = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_HOST_READ_BIT
        };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
        pgraph_vk_finish(pg, VK_FINISH_REASON_FLUSH);
        VK_CHECK(vmaInvalidateAllocation(r->allocator, output.allocation, 0,
                                         12 * face_bytes));
        for (unsigned generation = 0; generation < 2; generation++) {
            for (unsigned face = 0; face < 6; face++) {
                unsigned source_face = generation && face == 4 ? 6 : face;
                assert(memcmp(output.mapped + (generation * 6 + face) * face_bytes,
                              input.mapped + source_face * face_bytes,
                              face_bytes) == 0);
                checked += face_bytes;
            }
        }
        for (unsigned i = 0; i < 6; i++) {
            vmaDestroyImage(r->allocator, sources[i].image,
                            sources[i].allocation);
        }
        vmaDestroyImage(r->allocator, texture.image, texture.allocation);
        vmaDestroyBuffer(r->allocator, input.buffer, input.allocation);
        vmaDestroyBuffer(r->allocator, output.buffer, output.allocation);
    }
    assert(r->perf.cubemap_face_copies == 14);
    assert(r->perf.cubemap_face_reuses == 34);
    printf("PASS 2 formats, 24 face images, %lu byte checks, 14 copies, "
           "34 reuses, %u submissions\n", checked, submits);
    vkDestroyFence(r->device,r->command_buffer_fence,NULL);
    vkDestroyCommandPool(r->device,r->command_pool,NULL);
    vmaDestroyAllocator(r->allocator);
    vkDestroyDevice(r->device,NULL);
    vkDestroyInstance(r->instance,NULL);
    g_free(r);g_free(d);
    return 0;
}

void pgraph_vk_begin_debug_marker(PGRAPHVkState *r, VkCommandBuffer cmd,
                                  float color[4], const char *format, ...)
{
}
void pgraph_vk_end_debug_marker(PGRAPHVkState *r, VkCommandBuffer cmd)
{
}
bool pgraph_vk_compute_needs_finish(PGRAPHVkState *r)
{
    g_assert_not_reached();
}
void pgraph_vk_pack_depth_stencil(PGRAPHState *pg, SurfaceBinding *surface,
                                  VkCommandBuffer cmd, VkBuffer src,
                                  VkBuffer dst, bool downscale)
{
    g_assert_not_reached();
}

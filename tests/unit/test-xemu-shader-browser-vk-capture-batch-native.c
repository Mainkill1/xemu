/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hw/xbox/nv2a/pgraph/vk/renderer.h"
#include "hw/xbox/nv2a/pgraph/shader-browser-resource.h"

int xemu_test_vk_capture_batch_native(uint64_t tokens[4], int stop_before_aux);
void xemu_test_vk_capture_stop(void);
void xemu_test_vk_capture_stop_at_admission(void);
static unsigned stop_scope_countdown;

NV2AStats g_nv2a_stats;
uint64_t nv2a_profile_preview_renderer_epoch(void)
{
    return 3;
}
uint64_t xemu_shader_browser_scope_generation(void)
{
    if (stop_scope_countdown && !--stop_scope_countdown)
        xemu_test_vk_capture_stop();
    return 1;
}
void nv2a_profile_log_event_once(NV2AProfileEvent event)
{
    (void)event;
}
int64_t qemu_clock_get_ns(QEMUClockType type)
{
    (void)type;
    return 0;
}
void pgraph_vk_perf_record_single_time_submit(PGRAPHVkState *r,
                                              SingleTimeReason reason,
                                              uint64_t submit, uint64_t wait,
                                              uint64_t bytes)
{
    (void)r;
    (void)reason;
    (void)submit;
    (void)wait;
    (void)bytes;
}

#define CHECK_NATIVE(x)                                                 \
    do {                                                                \
        if (!(x)) {                                                     \
            fprintf(stderr, "native Vulkan batch fixture: %s:%d: %s\n", \
                    __FILE__, __LINE__, #x);                            \
            abort();                                                    \
        }                                                               \
    } while (0)

static VkDeviceMemory create_buffer(PGRAPHVkState *r, int index,
                                    uint32_t memory_type)
{
    StorageBuffer *buffer = &r->storage_buffers[index];
    VkBufferCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = 64,
        .usage =
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    };
    CHECK_NATIVE(vkCreateBuffer(r->device, &info, NULL, &buffer->buffer) ==
                 VK_SUCCESS);
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(r->device, buffer->buffer, &requirements);
    CHECK_NATIVE(requirements.memoryTypeBits & (1U << memory_type));
    VkMemoryAllocateInfo allocation = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memory_type,
    };
    VkDeviceMemory memory;
    CHECK_NATIVE(vkAllocateMemory(r->device, &allocation, NULL, &memory) ==
                 VK_SUCCESS);
    CHECK_NATIVE(vkBindBufferMemory(r->device, buffer->buffer, memory, 0) ==
                 VK_SUCCESS);
    CHECK_NATIVE(vkMapMemory(r->device, memory, 0, VK_WHOLE_SIZE, 0,
                             (void **)&buffer->mapped) == VK_SUCCESS);
    buffer->buffer_size = 64;
    buffer->capture_owner = xemu_shader_capture_resource_new_owner();
    memset(buffer->mapped, 0, 64);
    return memory;
}

static uint64_t record_copy(PGRAPHState *pg, VkCommandBuffer cmd, int source,
                            int destination, bool deferred)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    uint64_t token = pgraph_vk_capture_buffer_copy(pg, cmd, source, destination,
                                                   0, 0, 16, deferred);
    CHECK_NATIVE(token);
    VkBufferCopy copy = { .size = 16 };
    vkCmdCopyBuffer(cmd, r->storage_buffers[source].buffer,
                    r->storage_buffers[destination].buffer, 1, &copy);
    VkBufferMemoryBarrier host_read = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = r->storage_buffers[destination].buffer,
        .offset = 0,
        .size = 16,
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 1, &host_read,
                         0, NULL);
    pgraph_vk_capture_record(pg, cmd, token,
                             XEMU_SHADER_CAPTURE_COMMAND_UNKNOWN);
    pgraph_shader_resource_finish(token);
    return token;
}

/* Exercises the production capture helpers and real queue execution. It emits
 * buffer copies only; no synthetic draw is attributed to a native command. */
int xemu_test_vk_capture_batch_native(uint64_t tokens[4], int stop_before_aux)
{
    if (volkInitialize() != VK_SUCCESS)
        return 77;
    VkInstanceCreateInfo instance_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    };
    VkInstance instance;
    if (vkCreateInstance(&instance_info, NULL, &instance) != VK_SUCCESS)
        return 77;
    volkLoadInstance(instance);
    uint32_t count = 0;
    CHECK_NATIVE(vkEnumeratePhysicalDevices(instance, &count, NULL) ==
                 VK_SUCCESS);
    if (!count) {
        vkDestroyInstance(instance, NULL);
        return 77;
    }
    VkPhysicalDevice *devices = g_new(VkPhysicalDevice, count);
    CHECK_NATIVE(vkEnumeratePhysicalDevices(instance, &count, devices) ==
                 VK_SUCCESS);
    VkPhysicalDevice physical = devices[0];
    g_free(devices);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, NULL);
    VkQueueFamilyProperties *families = g_new(VkQueueFamilyProperties, count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families);
    uint32_t family = count;
    for (uint32_t i = 0; i < count; ++i)
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            family = i;
            break;
        }
    g_free(families);
    if (family == count) {
        vkDestroyInstance(instance, NULL);
        return 77;
    }
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
    PGRAPHState *pg = g_new0(PGRAPHState, 1);
    PGRAPHVkState *r = g_new0(PGRAPHVkState, 1);
    pg->vk_renderer_state = r;
    pg->frame_time = 7;
    CHECK_NATIVE(vkCreateDevice(physical, &device_info, NULL, &r->device) ==
                 VK_SUCCESS);
    volkLoadDevice(r->device);
    vkGetDeviceQueue(r->device, family, 0, &r->queue);
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    uint32_t memory_type = properties.memoryTypeCount;
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
        if ((properties.memoryTypes[i].propertyFlags &
             (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
            (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            memory_type = i;
            break;
        }
    CHECK_NATIVE(memory_type < properties.memoryTypeCount);
    const int indices[] = {
        BUFFER_INDEX_STAGING, BUFFER_INDEX,      BUFFER_VERTEX_INLINE_STAGING,
        BUFFER_VERTEX_INLINE, BUFFER_VERTEX_RAM, BUFFER_STAGING_SRC
    };
    VkDeviceMemory memory[G_N_ELEMENTS(indices)];
    for (size_t i = 0; i < G_N_ELEMENTS(indices); ++i)
        memory[i] = create_buffer(r, indices[i], memory_type);
    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = family,
    };
    CHECK_NATIVE(vkCreateCommandPool(r->device, &pool_info, NULL,
                                     &r->command_pool) == VK_SUCCESS);
    VkCommandBufferAllocateInfo allocate = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = r->command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 2,
    };
    CHECK_NATIVE(vkAllocateCommandBuffers(r->device, &allocate,
                                          r->command_buffers) == VK_SUCCESS);
    r->command_buffer = r->command_buffers[0];
    r->aux_command_buffer = r->command_buffers[1];
    uint32_t original[] = { 5, 9, 13, 17 },
             inline_values[] = { 21, 25, 29, 33 };
    void *data[] = { original }, *inline_data[] = { inline_values };
    VkDeviceSize size[] = { sizeof(original) };
    CHECK_NATIVE(pgraph_vk_append_to_buffer(pg, BUFFER_INDEX_STAGING, data,
                                            size, 1, 1) == 0);
    CHECK_NATIVE(pgraph_vk_append_to_buffer(pg, BUFFER_VERTEX_INLINE_STAGING,
                                            inline_data, size, 1, 1) == 0);
    VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    CHECK_NATIVE(vkBeginCommandBuffer(r->command_buffer, &begin) == VK_SUCCESS);
    tokens[2] = record_copy(pg, r->command_buffer, BUFFER_INDEX,
                            BUFFER_VERTEX_RAM, false);
    CHECK_NATIVE(vkEndCommandBuffer(r->command_buffer) == VK_SUCCESS);
    if (stop_before_aux)
        xemu_test_vk_capture_stop();
    r->capture_finish_auxiliary = true;
    VkCommandBuffer auxiliary = pgraph_vk_begin_single_time_commands(pg);
    tokens[0] =
        record_copy(pg, auxiliary, BUFFER_INDEX_STAGING, BUFFER_INDEX, true);
    tokens[1] = record_copy(pg, auxiliary, BUFFER_VERTEX_INLINE_STAGING,
                            BUFFER_VERTEX_INLINE, true);
    CHECK_NATIVE(vkEndCommandBuffer(auxiliary) == VK_SUCCESS);
    r->in_aux_command_buffer = false;
    r->capture_finish_auxiliary = false;
    VkSemaphoreCreateInfo semaphore_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
    };
    VkSemaphore semaphore;
    CHECK_NATIVE(vkCreateSemaphore(r->device, &semaphore_info, NULL,
                                   &semaphore) == VK_SUCCESS);
    VkFenceCreateInfo fence_info = { .sType =
                                         VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence;
    CHECK_NATIVE(vkCreateFence(r->device, &fence_info, NULL, &fence) ==
                 VK_SUCCESS);
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkSubmitInfo submits[] = {
        { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
          .commandBufferCount = 1,
          .pCommandBuffers = &auxiliary,
          .signalSemaphoreCount = 1,
          .pSignalSemaphores = &semaphore },
        { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
          .commandBufferCount = 1,
          .pCommandBuffers = &r->command_buffer,
          .waitSemaphoreCount = 1,
          .pWaitSemaphores = &semaphore,
          .pWaitDstStageMask = &wait_stage },
    };
    VkResult result = vkQueueSubmit(r->queue, 2, submits, fence);
    uint64_t batch = pgraph_vk_capture_submit(pg, r->command_buffer, result);
    CHECK_NATIVE(result == VK_SUCCESS);
    result = vkWaitForFences(r->device, 1, &fence, VK_TRUE, UINT64_MAX);
    pgraph_vk_capture_retire(batch, result);
    CHECK_NATIVE(result == VK_SUCCESS);
    CHECK_NATIVE(memcmp(r->storage_buffers[BUFFER_INDEX].mapped, original,
                        sizeof(original)) == 0);
    CHECK_NATIVE(memcmp(r->storage_buffers[BUFFER_VERTEX_RAM].mapped, original,
                        sizeof(original)) == 0);
    CHECK_NATIVE(memcmp(r->storage_buffers[BUFFER_VERTEX_INLINE].mapped,
                        inline_values, sizeof(inline_values)) == 0);
    memset(&r->capture_main_batch, 0, sizeof(r->capture_main_batch));
    r->storage_buffers[BUFFER_INDEX_STAGING].buffer_offset = 0;
    r->storage_buffers[BUFFER_VERTEX_INLINE_STAGING].buffer_offset = 0;
    memset(r->storage_buffers[BUFFER_INDEX_STAGING].mapped, 0xee, 64);
    memset(r->storage_buffers[BUFFER_VERTEX_INLINE_STAGING].mapped, 0xdd, 64);
    if (stop_before_aux)
        goto cleanup;
    memcpy(r->storage_buffers[BUFFER_STAGING_SRC].mapped, original,
           sizeof(original));
    pgraph_vk_capture_buffer_upload(pg, BUFFER_STAGING_SRC, 0, sizeof(original),
                                    original, false);
    auxiliary = pgraph_vk_begin_single_time_commands(pg);
    tokens[3] =
        record_copy(pg, auxiliary, BUFFER_STAGING_SRC, BUFFER_INDEX, false);
    pgraph_vk_end_single_time_commands(
        pg, auxiliary, VK_SINGLE_TIME_SURFACE_UPLOAD, sizeof(original));
    CHECK_NATIVE(memcmp(r->storage_buffers[BUFFER_INDEX].mapped, original,
                        sizeof(original)) == 0);
cleanup:
    vkDestroyFence(r->device, fence, NULL);
    vkDestroySemaphore(r->device, semaphore, NULL);
    vkDestroyCommandPool(r->device, r->command_pool, NULL);
    for (size_t i = 0; i < G_N_ELEMENTS(indices); ++i) {
        vkUnmapMemory(r->device, memory[i]);
        vkDestroyBuffer(r->device, r->storage_buffers[indices[i]].buffer, NULL);
        vkFreeMemory(r->device, memory[i], NULL);
    }
    vkDestroyDevice(r->device, NULL);
    vkDestroyInstance(instance, NULL);
    g_free(r);
    g_free(pg);
    return 0;
}

void xemu_test_vk_capture_stop_at_admission(void)
{
    PGRAPHState *pg = g_new0(PGRAPHState, 1);
    PGRAPHVkState *r = g_new0(PGRAPHVkState, 1);
    pg->vk_renderer_state = r;
    pg->frame_time = 7;
    uint32_t values[] = { 5, 9, 13, 17 };
    uint8_t storage[64] = { 0 };
    StorageBuffer *buffer = &r->storage_buffers[BUFFER_INDEX_STAGING];
    buffer->mapped = storage;
    buffer->buffer_size = sizeof(storage);
    buffer->capture_owner = xemu_shader_capture_resource_new_owner();
    void *data[] = { values };
    VkDeviceSize size[] = { sizeof(values) };
    // Stop after occurrence claim and before the first batch is admitted.
    stop_scope_countdown = 2;
    CHECK_NATIVE(pgraph_vk_append_to_buffer(pg, BUFFER_INDEX_STAGING, data,
                                            size, 1, 1) == 0);
    CHECK_NATIVE(stop_scope_countdown == 0);
    CHECK_NATIVE(memcmp(storage, values, sizeof(values)) == 0);
    g_free(r);
    g_free(pg);
}

uint64_t xemu_test_vk_last_event_token(void);
int xemu_test_vk_capture_image_copy(uint64_t tokens[3], uint64_t clears[3]);

static VkDeviceMemory create_capture_image(PGRAPHVkState *r, VkFormat format,
                                           uint32_t width, uint32_t height,
                                           VkImage *image, uint64_t *bytes)
{
    VkImageCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = { width, height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
    };
    CHECK_NATIVE(vkCreateImage(r->device, &info, NULL, image) == VK_SUCCESS);
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(r->device, *image, &requirements);
    uint32_t type = 0;
    while (!(requirements.memoryTypeBits & (1U << type)))
        ++type;
    VkMemoryAllocateInfo allocate = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = type,
    };
    VkDeviceMemory memory;
    CHECK_NATIVE(vkAllocateMemory(r->device, &allocate, NULL, &memory) ==
                 VK_SUCCESS);
    CHECK_NATIVE(vkBindImageMemory(r->device, *image, memory, 0) == VK_SUCCESS);
    *bytes = requirements.size;
    return memory;
}

static void capture_image_barrier(VkCommandBuffer cmd, VkImage image,
                                  VkImageLayout before, VkImageLayout after,
                                  VkAccessFlags source,
                                  VkAccessFlags destination)
{
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = source,
        .dstAccessMask = destination,
        .oldLayout = before,
        .newLayout = after,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    vkCmdPipelineBarrier(cmd,
                         before == VK_IMAGE_LAYOUT_UNDEFINED ?
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT :
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1,
                         &barrier);
}

int xemu_test_vk_capture_image_copy(uint64_t tokens[3], uint64_t clears[3])
{
    CHECK_NATIVE(volkInitialize() == VK_SUCCESS);
    VkInstanceCreateInfo instance_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO
    };
    VkInstance instance;
    CHECK_NATIVE(vkCreateInstance(&instance_info, NULL, &instance) ==
                 VK_SUCCESS);
    volkLoadInstance(instance);
    uint32_t count = 0;
    CHECK_NATIVE(vkEnumeratePhysicalDevices(instance, &count, NULL) ==
                 VK_SUCCESS);
    CHECK_NATIVE(count);
    VkPhysicalDevice *devices = g_new(VkPhysicalDevice, count);
    CHECK_NATIVE(vkEnumeratePhysicalDevices(instance, &count, devices) ==
                 VK_SUCCESS);
    VkPhysicalDevice physical = devices[0];
    g_free(devices);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, NULL);
    VkQueueFamilyProperties *families = g_new(VkQueueFamilyProperties, count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families);
    uint32_t family = 0;
    while (!(families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT))
        ++family;
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
    PGRAPHState *pg = g_new0(PGRAPHState, 1);
    PGRAPHVkState *r = g_new0(PGRAPHVkState, 1);
    pg->vk_renderer_state = r;
    pg->frame_time = 7;
    pg->surface_scale_factor = 1;
    CHECK_NATIVE(vkCreateDevice(physical, &device_info, NULL, &r->device) ==
                 VK_SUCCESS);
    volkLoadDevice(r->device);
    vkGetDeviceQueue(r->device, family, 0, &r->queue);
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    uint32_t memory_type = 0;
    while ((properties.memoryTypes[memory_type].propertyFlags &
            (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) !=
           (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
        ++memory_type;
    const int readback_indices[] = { BUFFER_STAGING_DST, BUFFER_VERTEX_RAM,
                                     BUFFER_INDEX };
    const int upload_indices[] = { BUFFER_TEXTURE_STAGING, BUFFER_INDEX_STAGING,
                                   BUFFER_VERTEX_INLINE_STAGING };
    VkDeviceMemory upload_memory[3];
    for (size_t i = 0; i < 3; ++i)
        upload_memory[i] = create_buffer(r, upload_indices[i], memory_type);
    VkDeviceMemory readback_memory[3];
    for (size_t i = 0; i < 3; ++i)
        readback_memory[i] = create_buffer(r, readback_indices[i], memory_type);
    const int clear_indices[] = { BUFFER_COMPUTE_SRC, BUFFER_COMPUTE_DST,
                                  BUFFER_VERTEX_INLINE };
    VkDeviceMemory clear_memory[3];
    for (size_t i = 0; i < 3; ++i)
        clear_memory[i] = create_buffer(r, clear_indices[i], memory_type);
    VkCommandPoolCreateInfo pool = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = family,
    };
    CHECK_NATIVE(vkCreateCommandPool(r->device, &pool, NULL,
                                     &r->command_pool) == VK_SUCCESS);
    VkCommandBufferAllocateInfo allocate = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = r->command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    CHECK_NATIVE(vkAllocateCommandBuffers(r->device, &allocate,
                                          &r->command_buffer) == VK_SUCCESS);
    VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
    };
    CHECK_NATIVE(vkBeginCommandBuffer(r->command_buffer, &begin) == VK_SUCCESS);
    SurfaceBinding source[3] = { 0 };
    TextureBinding destination[3] = { 0 };
    VkDeviceMemory source_memory[3], destination_memory[3];
    for (size_t i = 0; i < 3; ++i) {
        VkFormat source_format =
            i == 1 ? VK_FORMAT_B8G8R8A8_UNORM : VK_FORMAT_R8G8B8A8_UNORM;
        VkFormat destination_format =
            i == 0 ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_B8G8R8A8_UNORM;
        uint32_t destination_size = i == 1 ? 3 : 4;
        source[i].color = true;
        source[i].width = source[i].height = 3;
        source[i].host_fmt.vk_format = source_format;
        source[i].host_fmt.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        source[i].capture_owner = xemu_shader_capture_resource_new_owner();
        destination[i].key.vk_format = destination_format;
        destination[i].storage_extent =
            (VkExtent3D){ destination_size, destination_size, 1 };
        destination[i].storage_image_type = VK_IMAGE_TYPE_2D;
        destination[i].storage_mip_levels = destination[i].storage_layer_count =
            1;
        destination[i].capture_owner = xemu_shader_capture_resource_new_owner();
        source_memory[i] = create_capture_image(
            r, source_format, 3, 3, &source[i].image, &source[i].capture_bytes);
        destination_memory[i] = create_capture_image(
            r, destination_format, destination_size, destination_size,
            &destination[i].image, &destination[i].capture_bytes);
        VkImageViewCreateInfo view = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
            .image = source[i].image,
            .format = source_format,
        };
        CHECK_NATIVE(vkCreateImageView(r->device, &view, NULL,
                                       &source[i].image_view) == VK_SUCCESS);
        view.image = destination[i].image;
        view.format = destination_format;
        CHECK_NATIVE(vkCreateImageView(r->device, &view, NULL,
                                       &destination[i].image_view) ==
                     VK_SUCCESS);
        VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0,
                                          1 };
        VkClearColorValue green = { .float32 = { 0, 1, 0, 1 } };
        uint8_t *upload = r->storage_buffers[upload_indices[i]].mapped;
        for (uint32_t y = 0; y < 3; ++y)
            for (uint32_t x = 0; x < 3; ++x) {
                uint8_t rgba[] = { 20 + x * 40, 30 + y * 50, 80 + x + y, 255 };
                uint8_t *pixel = upload + (y * 3 + x) * 4;
                pixel[0] = rgba[i == 1 ? 2 : 0];
                pixel[1] = rgba[1];
                pixel[2] = rgba[i == 1 ? 0 : 2];
                pixel[3] = rgba[3];
            }
        capture_image_barrier(r->command_buffer, source[i].image,
                              VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                              VK_ACCESS_TRANSFER_WRITE_BIT);
        capture_image_barrier(r->command_buffer, destination[i].image,
                              VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                              VK_ACCESS_TRANSFER_WRITE_BIT);
        VkBufferImageCopy upload_region = {
            .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .imageExtent = { 3, 3, 1 },
        };
        vkCmdCopyBufferToImage(
            r->command_buffer, r->storage_buffers[upload_indices[i]].buffer,
            source[i].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
            &upload_region);
        vkCmdClearColorImage(r->command_buffer, destination[i].image,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &green, 1,
                             &range);
        capture_image_barrier(r->command_buffer, source[i].image,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                              VK_ACCESS_TRANSFER_WRITE_BIT,
                              VK_ACCESS_TRANSFER_READ_BIT);
        capture_image_barrier(r->command_buffer, destination[i].image,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              VK_ACCESS_TRANSFER_WRITE_BIT,
                              VK_ACCESS_TRANSFER_WRITE_BIT);
        VkImageCopy region = {
            .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .srcOffset = { i == 1 ? 0 : 1, i == 1 ? 0 : 1, 0 },
            .dstOffset = { 0, i == 1 ? 0 : 1, 0 },
            .extent = { i == 1 ? 3 : 2, i == 1 ? 3 : 2, 1 },
        };
        vkCmdCopyImage(r->command_buffer, source[i].image,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       destination[i].image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        tokens[i] = pgraph_vk_capture_color_image_copy(
            pg, r->command_buffer, &source[i], &destination[i], &region);
        CHECK_NATIVE(tokens[i]);
        capture_image_barrier(r->command_buffer, destination[i].image,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                              VK_ACCESS_TRANSFER_WRITE_BIT,
                              VK_ACCESS_TRANSFER_READ_BIT);
        VkBufferImageCopy read = {
            .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .imageExtent = { destination_size, destination_size, 1 },
        };
        StorageBuffer *buffer = &r->storage_buffers[readback_indices[i]];
        vkCmdCopyImageToBuffer(r->command_buffer, destination[i].image,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               buffer->buffer, 1, &read);
        VkBufferMemoryBarrier host = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = buffer->buffer,
            .size = 64,
        };
        vkCmdPipelineBarrier(r->command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 1, &host,
                             0, NULL);
    }
    VkRenderPass clear_passes[3];
    VkFramebuffer clear_frames[3];
    for (size_t i = 0; i < 3; ++i) {
        VkAttachmentDescription attachment = {
            .format = source[i].host_fmt.vk_format,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        };
        VkAttachmentReference reference = {
            0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        };
        VkSubpassDescription subpass = {
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount = 1,
            .pColorAttachments = &reference,
        };
        VkSubpassDependency dependencies[] = {
            { .srcSubpass = VK_SUBPASS_EXTERNAL,
              .dstSubpass = 0,
              .srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT,
              .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
              .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
              .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT },
            { .srcSubpass = 0,
              .dstSubpass = VK_SUBPASS_EXTERNAL,
              .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
              .dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT,
              .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
              .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT },
        };
        VkRenderPassCreateInfo pass = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .attachmentCount = 1,
            .pAttachments = &attachment,
            .subpassCount = 1,
            .pSubpasses = &subpass,
            .dependencyCount = 2,
            .pDependencies = dependencies,
        };
        CHECK_NATIVE(vkCreateRenderPass(r->device, &pass, NULL,
                                        &clear_passes[i]) == VK_SUCCESS);
        VkFramebufferCreateInfo frame = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = clear_passes[i],
            .attachmentCount = 1,
            .pAttachments = &source[i].image_view,
            .width = 3,
            .height = 3,
            .layers = 1,
        };
        CHECK_NATIVE(vkCreateFramebuffer(r->device, &frame, NULL,
                                         &clear_frames[i]) == VK_SUCCESS);
        VkImageMemoryBarrier color = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                             VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = source[i].image,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        };
        vkCmdPipelineBarrier(r->command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                             0, NULL, 0, NULL, 1, &color);
        VkRenderPassBeginInfo start_pass = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = clear_passes[i],
            .framebuffer = clear_frames[i],
            .renderArea = { { 0, 0 }, { 3, 3 } },
        };
        vkCmdBeginRenderPass(r->command_buffer, &start_pass,
                             VK_SUBPASS_CONTENTS_INLINE);
        VkClearAttachment clear = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .colorAttachment = 0,
            .clearValue.color.float32 = { i == 0 ? 1 : 0, i == 2 ? 1 : 0,
                                          i == 1 ? 1 : 0, i == 0 ? 1 : 0 },
        };
        VkClearRect rect = {
            .rect = { { i == 1 ? 1 : 0, 0 },
                      { i == 1 ? 1 : 3, i == 1 ? 2 : 3 } },
            .layerCount = 1,
        };
        // Dynamic scissor is deliberately different: attachment clear uses
        // its own exact rectangle and no graphics color-write mask.
        VkRect2D unrelated = { { 0, 2 }, { 1, 1 } };
        vkCmdSetScissor(r->command_buffer, 0, 1, &unrelated);
        vkCmdClearAttachments(r->command_buffer, 1, &clear, 1, &rect);
        r->color_binding = &source[i];
        pg->surface_shape.color_format =
            NV097_SET_SURFACE_FORMAT_COLOR_LE_A8R8G8B8;
        pg->surface_shape.anti_aliasing =
            i == 2 ? NV097_SET_SURFACE_FORMAT_ANTI_ALIASING_CENTER_CORNER_2 :
                     NV097_SET_SURFACE_FORMAT_ANTI_ALIASING_CENTER_1;
        // The third command clears a real single-sample host image while its
        // guest antialiasing state remains unsupported logical evidence.
        clears[i] = pgraph_vk_capture_clear(pg, r->command_buffer,
                                            NV097_CLEAR_SURFACE_COLOR, 1,
                                            &clear, &rect, false, NULL);
        CHECK_NATIVE(clears[i]);
        if (i == 0) {
            XemuShaderCaptureReplayDescription description;
            CHECK_NATIVE(pgraph_vk_capture_clear_description(
                pg, NV097_CLEAR_SURFACE_COLOR, 1, &clear, &rect, false,
                &description));
            CHECK_NATIVE(!pgraph_vk_capture_clear_description(
                pg, NV097_CLEAR_SURFACE_R, 1, &clear, &rect, true,
                &description));
            CHECK_NATIVE(!pgraph_vk_capture_clear_description(
                pg, NV097_CLEAR_SURFACE_COLOR | NV097_CLEAR_SURFACE_Z, 1,
                &clear, &rect, false, &description));
            CHECK_NATIVE(!pgraph_vk_capture_clear_description(
                pg, NV097_CLEAR_SURFACE_COLOR, 2, &clear, &rect, false,
                &description));
            VkClearRect invalid = rect;
            invalid.rect.offset.x = -1;
            CHECK_NATIVE(!pgraph_vk_capture_clear_description(
                pg, NV097_CLEAR_SURFACE_COLOR, 1, &clear, &invalid, false,
                &description));
            invalid = rect;
            invalid.rect.extent.width = UINT32_MAX;
            CHECK_NATIVE(!pgraph_vk_capture_clear_description(
                pg, NV097_CLEAR_SURFACE_COLOR, 1, &clear, &invalid, false,
                &description));
            invalid = rect;
            invalid.layerCount = 2;
            CHECK_NATIVE(!pgraph_vk_capture_clear_description(
                pg, NV097_CLEAR_SURFACE_COLOR, 1, &clear, &invalid, false,
                &description));
            VkClearAttachment invalid_color = clear;
            uint32_t nan = 0x7fc00000;
            memcpy(&invalid_color.clearValue.color.float32[0], &nan,
                   sizeof(nan));
            CHECK_NATIVE(!pgraph_vk_capture_clear_description(
                pg, NV097_CLEAR_SURFACE_COLOR, 1, &invalid_color, &rect, false,
                &description));
        }
        vkCmdEndRenderPass(r->command_buffer);
        VkBufferImageCopy read = {
            .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .imageExtent = { 3, 3, 1 },
        };
        StorageBuffer *buffer = &r->storage_buffers[clear_indices[i]];
        vkCmdCopyImageToBuffer(r->command_buffer, source[i].image,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               buffer->buffer, 1, &read);
        VkBufferMemoryBarrier host = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = buffer->buffer,
            .size = 64,
        };
        vkCmdPipelineBarrier(r->command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 1, &host,
                             0, NULL);
    }
    pg->surface_shape.anti_aliasing =
        NV097_SET_SURFACE_FORMAT_ANTI_ALIASING_CENTER_1;
    // Descriptor construction itself makes no recorded-draw claim.
    for (size_t attribute = 0; attribute < NV2A_VERTEXSHADER_ATTRIBUTES;
         ++attribute)
        r->vertex_attribute_to_description_location[attribute] = -1;
    r->color_binding = &source[0];
    r->texture_bindings[2] = &destination[0];
    XemuShaderCaptureReplayDescription draw;
    CHECK_NATIVE(pgraph_vk_capture_draw_description(pg, false, &draw));
    CHECK_NATIVE(draw.binding_count == 3 &&
                 draw.kind == XEMU_SHADER_CAPTURE_COMMAND_DRAW);
    CHECK_NATIVE(draw.bindings[0].resource.write == 0 &&
                 draw.bindings[1].resource.write == 1);
    CHECK_NATIVE(draw.bindings[0].role == XEMU_SHADER_CAPTURE_REPLAY_COLOR &&
                 draw.bindings[1].role == XEMU_SHADER_CAPTURE_REPLAY_COLOR);
    CHECK_NATIVE(draw.bindings[0].checkpoint && draw.bindings[1].checkpoint);
    CHECK_NATIVE(draw.bindings[0].image.width == 3 &&
                 draw.bindings[0].image.height == 3);
    CHECK_NATIVE(draw.bindings[2].role == XEMU_SHADER_CAPTURE_REPLAY_TEXTURE &&
                 draw.bindings[2].resource.slot == 2 &&
                 draw.bindings[2].slot == 2);
    CHECK_NATIVE(draw.bindings[2].image.format ==
                 XEMU_SHADER_CAPTURE_REPLAY_RGBA8_UNORM);
    CHECK_NATIVE(draw.bindings[2].checkpoint);
    vkDestroyImageView(r->device, destination[0].image_view, NULL);
    destination[0].component_mapping.a = VK_COMPONENT_SWIZZLE_ONE;
    VkImageViewCreateInfo remapped_view = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .image = destination[0].image,
        .format = destination[0].key.vk_format,
        .components = destination[0].component_mapping,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    CHECK_NATIVE(vkCreateImageView(r->device, &remapped_view, NULL,
                                   &destination[0].image_view) == VK_SUCCESS);
    CHECK_NATIVE(pgraph_vk_capture_draw_description(pg, false, &draw));
    CHECK_NATIVE(draw.bindings[2].image.sample_swizzle[3] == 5);
    CHECK_NATIVE(!draw.bindings[2].checkpoint);
    destination[0].storage_mip_levels = 2;
    CHECK_NATIVE(!pgraph_vk_capture_draw_description(pg, false, &draw));
    destination[0].storage_mip_levels = 1;
    destination[0].storage_image_type = VK_IMAGE_TYPE_3D;
    CHECK_NATIVE(!pgraph_vk_capture_draw_description(pg, false, &draw));
    destination[0].storage_image_type = VK_IMAGE_TYPE_2D;
    destination[0].key.vk_format = VK_FORMAT_R16_UNORM;
    CHECK_NATIVE(!pgraph_vk_capture_draw_description(pg, false, &draw));
    destination[0].key.vk_format = VK_FORMAT_R8G8B8A8_UNORM;
    r->vertex_attribute_to_description_location[3] = 0;
    r->num_active_vertex_attribute_descriptions = 1;
    CHECK_NATIVE(pgraph_vk_capture_draw_description(pg, true, &draw));
    CHECK_NATIVE(draw.binding_count == 5);
    CHECK_NATIVE(draw.bindings[3].role ==
                 XEMU_SHADER_CAPTURE_REPLAY_VERTEX_STREAM);
    CHECK_NATIVE(draw.bindings[3].resource.kind ==
                 XEMU_SHADER_CAPTURE_RESOURCE_BUFFER);
    CHECK_NATIVE(draw.bindings[3].resource.slot == 3 &&
                 draw.bindings[3].slot == 3);
    CHECK_NATIVE(strcmp(draw.bindings[3].blob_name, "vertex.attribute3") == 0);
    CHECK_NATIVE(draw.bindings[4].role == XEMU_SHADER_CAPTURE_REPLAY_INDICES);
    CHECK_NATIVE(draw.bindings[4].resource.slot ==
                 NV2A_VERTEXSHADER_ATTRIBUTES);
    CHECK_NATIVE(strcmp(draw.bindings[4].blob_name, "vertex.indices") == 0);

    CHECK_NATIVE(vkEndCommandBuffer(r->command_buffer) == VK_SUCCESS);
    VkFenceCreateInfo fence_info = { .sType =
                                         VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence;
    CHECK_NATIVE(vkCreateFence(r->device, &fence_info, NULL, &fence) ==
                 VK_SUCCESS);
    VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &r->command_buffer,
    };
    VkResult result = vkQueueSubmit(r->queue, 1, &submit, fence);
    uint64_t batch = pgraph_vk_capture_submit(pg, r->command_buffer, result);
    CHECK_NATIVE(result == VK_SUCCESS);
    result = vkWaitForFences(r->device, 1, &fence, VK_TRUE, UINT64_MAX);
    pgraph_vk_capture_retire(batch, result);
    CHECK_NATIVE(result == VK_SUCCESS);
    for (size_t i = 0; i < 3; ++i) {
        uint32_t width = i == 1 ? 3 : 4;
        const uint8_t *raw = r->storage_buffers[readback_indices[i]].mapped;
        for (uint32_t y = 0; y < width; ++y)
            for (uint32_t x = 0; x < width; ++x) {
                bool copied = i == 1 || (x < 2 && y >= 1 && y < 3);
                uint32_t source_x = x + (i == 1 ? 0 : 1);
                uint32_t source_y = y;
                uint8_t rgba[] = { 20 + source_x * 40, 30 + source_y * 50,
                                   80 + source_x + source_y, 255 };
                uint8_t expected[] = { copied ? rgba[i == 1 ? 2 : 0] : 0,
                                       copied ? rgba[1] : 255,
                                       copied ? rgba[i == 1 ? 0 : 2] : 0, 255 };
                CHECK_NATIVE(memcmp(raw + (y * width + x) * 4, expected, 4) ==
                             0);
            }
    }
    vkDestroyFence(r->device, fence, NULL);
    vkDestroyCommandPool(r->device, r->command_pool, NULL);
    for (size_t i = 0; i < 3; ++i) {
        const uint8_t *raw = r->storage_buffers[clear_indices[i]].mapped;
        for (uint32_t y = 0; y < 3; ++y)
            for (uint32_t x = 0; x < 3; ++x) {
                bool cleared = i != 1 || (x == 1 && y < 2);
                uint8_t rgba[] = { 20 + x * 40, 30 + y * 50, 80 + x + y, 255 };
                uint8_t expected[] = { cleared ? (i == 2 ? 0 : 255) :
                                                 rgba[i == 1 ? 2 : 0],
                                       cleared ? (i == 2 ? 255 : 0) : rgba[1],
                                       cleared ? 0 : rgba[i == 1 ? 0 : 2],
                                       cleared ? (i == 0 ? 255 : 0) : 255 };
                CHECK_NATIVE(memcmp(raw + (y * 3 + x) * 4, expected, 4) == 0);
            }
        vkDestroyFramebuffer(r->device, clear_frames[i], NULL);
        vkDestroyRenderPass(r->device, clear_passes[i], NULL);
        vkUnmapMemory(r->device, clear_memory[i]);
        vkDestroyBuffer(r->device, r->storage_buffers[clear_indices[i]].buffer,
                        NULL);
        vkFreeMemory(r->device, clear_memory[i], NULL);
    }
    for (size_t i = 0; i < 3; ++i) {
        vkDestroyImageView(r->device, source[i].image_view, NULL);
        vkDestroyImageView(r->device, destination[i].image_view, NULL);
        vkDestroyImage(r->device, source[i].image, NULL);
        vkDestroyImage(r->device, destination[i].image, NULL);
        vkFreeMemory(r->device, source_memory[i], NULL);
        vkFreeMemory(r->device, destination_memory[i], NULL);
        vkUnmapMemory(r->device, upload_memory[i]);
        vkDestroyBuffer(r->device, r->storage_buffers[upload_indices[i]].buffer,
                        NULL);
        vkFreeMemory(r->device, upload_memory[i], NULL);
        vkUnmapMemory(r->device, readback_memory[i]);
        vkDestroyBuffer(r->device,
                        r->storage_buffers[readback_indices[i]].buffer, NULL);
        vkFreeMemory(r->device, readback_memory[i], NULL);
    }
    vkDestroyDevice(r->device, NULL);
    vkDestroyInstance(instance, NULL);
    g_free(r);
    g_free(pg);
    return 0;
}

/*
 * Reference Vulkan neural-presentation adapter.
 *
 * This adapter performs an exact GPU copy round trip through a private image.
 * It exists to validate xemu's command-buffer insertion point and resource
 * lifecycle. It does not perform neural rendering.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vulkan/vulkan.h>

#include "hw/xbox/nv2a/pgraph/neural-present-plugin.h"

typedef struct VulkanCopyContext {
    const XemuNeuralPluginHostV1 *host;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkImage scratch_image;
    VkDeviceMemory scratch_memory;
    VkImageLayout scratch_layout;
    VkFormat scratch_format;
    uint32_t scratch_width;
    uint32_t scratch_height;
    uint64_t evaluated_frames;
    uint64_t failed_frames;
} VulkanCopyContext;

static void set_text(char *destination, size_t size, const char *text)
{
    if (size != 0) {
        snprintf(destination, size, "%s", text != NULL ? text : "");
    }
}

static void log_message(VulkanCopyContext *context,
                        XemuNeuralPluginLogLevel level,
                        const char *message)
{
    if (context != NULL && context->host != NULL &&
        context->host->log != NULL) {
        context->host->log(context->host->opaque, level, message);
    }
}

static XemuNeuralPluginResult copy_bootstrap(
    const XemuNeuralPluginHostV1 *host, void **bootstrap_context,
    XemuNeuralPluginVulkanRequirementsV1 *requirements,
    XemuNeuralPluginStatusV1 *status)
{
    (void)host;
    if (bootstrap_context == NULL || requirements == NULL || status == NULL) {
        return XEMU_NEURAL_PLUGIN_RESULT_FATAL_ERROR;
    }
    *bootstrap_context = NULL;
    *requirements = (XemuNeuralPluginVulkanRequirementsV1)
        XEMU_NEURAL_PLUGIN_VULKAN_REQUIREMENTS_V1_INIT;
    status->state = XEMU_NEURAL_PLUGIN_STATE_READY;
    status->active_feature = XEMU_NEURAL_PLUGIN_FEATURE_PASSTHROUGH;
    set_text(status->text, sizeof(status->text),
             "Vulkan copy adapter uses xemu's existing graphics queue");
    return XEMU_NEURAL_PLUGIN_RESULT_OK;
}

static void copy_shutdown(void *bootstrap_context)
{
    (void)bootstrap_context;
}

static void destroy_scratch(VulkanCopyContext *context)
{
    if (context == NULL || context->device == VK_NULL_HANDLE) {
        return;
    }
    if (context->scratch_image != VK_NULL_HANDLE) {
        vkDestroyImage(context->device, context->scratch_image, NULL);
    }
    if (context->scratch_memory != VK_NULL_HANDLE) {
        vkFreeMemory(context->device, context->scratch_memory, NULL);
    }
    context->scratch_image = VK_NULL_HANDLE;
    context->scratch_memory = VK_NULL_HANDLE;
    context->scratch_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    context->scratch_format = VK_FORMAT_UNDEFINED;
    context->scratch_width = 0;
    context->scratch_height = 0;
}

static bool find_memory_type(VulkanCopyContext *context,
                             uint32_t type_bits,
                             VkMemoryPropertyFlags required,
                             uint32_t *index_out)
{
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(context->physical_device,
                                        &properties);
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
        if ((type_bits & (1U << i)) != 0 &&
            (properties.memoryTypes[i].propertyFlags & required) == required) {
            *index_out = i;
            return true;
        }
    }
    return false;
}

static XemuNeuralPluginResult create_scratch(
    VulkanCopyContext *context, const XemuNeuralPluginFrameV1 *frame,
    XemuNeuralPluginStatusV1 *status)
{
    if (context->scratch_image != VK_NULL_HANDLE &&
        context->scratch_format == (VkFormat)frame->color_format &&
        context->scratch_width == frame->display_width &&
        context->scratch_height == frame->display_height) {
        return XEMU_NEURAL_PLUGIN_RESULT_OK;
    }

    destroy_scratch(context);

    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = (VkFormat)frame->color_format,
        .extent = {
            .width = frame->display_width,
            .height = frame->display_height,
            .depth = 1,
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkResult result = vkCreateImage(context->device, &image_info, NULL,
                                    &context->scratch_image);
    if (result != VK_SUCCESS) {
        snprintf(status->text, sizeof(status->text),
                 "vkCreateImage failed (%d)", result);
        context->failed_frames++;
        return XEMU_NEURAL_PLUGIN_RESULT_RETRYABLE_ERROR;
    }

    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(context->device, context->scratch_image,
                                 &requirements);
    uint32_t memory_type = 0;
    if (!find_memory_type(context, requirements.memoryTypeBits,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                          &memory_type)) {
        set_text(status->text, sizeof(status->text),
                 "No device-local memory type for copy image");
        destroy_scratch(context);
        context->failed_frames++;
        return XEMU_NEURAL_PLUGIN_RESULT_FATAL_ERROR;
    }

    VkMemoryAllocateInfo allocation_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memory_type,
    };
    result = vkAllocateMemory(context->device, &allocation_info, NULL,
                              &context->scratch_memory);
    if (result != VK_SUCCESS) {
        snprintf(status->text, sizeof(status->text),
                 "vkAllocateMemory failed (%d)", result);
        destroy_scratch(context);
        context->failed_frames++;
        return XEMU_NEURAL_PLUGIN_RESULT_RETRYABLE_ERROR;
    }

    result = vkBindImageMemory(context->device, context->scratch_image,
                               context->scratch_memory, 0);
    if (result != VK_SUCCESS) {
        snprintf(status->text, sizeof(status->text),
                 "vkBindImageMemory failed (%d)", result);
        destroy_scratch(context);
        context->failed_frames++;
        return XEMU_NEURAL_PLUGIN_RESULT_FATAL_ERROR;
    }

    context->scratch_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    context->scratch_format = (VkFormat)frame->color_format;
    context->scratch_width = frame->display_width;
    context->scratch_height = frame->display_height;
    set_text(status->text, sizeof(status->text),
             "Vulkan copy validation image created");
    return XEMU_NEURAL_PLUGIN_RESULT_OK;
}

static XemuNeuralPluginResult copy_create(
    const XemuNeuralPluginCreateInfoV1 *create_info, void **context_out,
    XemuNeuralPluginStatusV1 *status)
{
    if (create_info == NULL || context_out == NULL || status == NULL ||
        create_info->struct_size < sizeof(*create_info) ||
        create_info->vulkan.struct_size < sizeof(create_info->vulkan) ||
        create_info->vulkan.physical_device == 0 ||
        create_info->vulkan.device == 0 || create_info->vulkan.queue == 0) {
        return XEMU_NEURAL_PLUGIN_RESULT_FATAL_ERROR;
    }

    VulkanCopyContext *context = calloc(1, sizeof(*context));
    if (context == NULL) {
        return XEMU_NEURAL_PLUGIN_RESULT_FATAL_ERROR;
    }
    context->host = create_info->host;
    context->physical_device =
        (VkPhysicalDevice)(uintptr_t)create_info->vulkan.physical_device;
    context->device = (VkDevice)(uintptr_t)create_info->vulkan.device;
    *context_out = context;

    status->state = XEMU_NEURAL_PLUGIN_STATE_READY;
    status->active_feature = XEMU_NEURAL_PLUGIN_FEATURE_PASSTHROUGH;
    set_text(status->text, sizeof(status->text),
             "Vulkan copy validation adapter ready; no neural model loaded");
    log_message(context, XEMU_NEURAL_PLUGIN_LOG_INFO, status->text);
    return XEMU_NEURAL_PLUGIN_RESULT_OK;
}

static void copy_destroy(void *opaque)
{
    VulkanCopyContext *context = opaque;
    if (context == NULL) {
        return;
    }
    destroy_scratch(context);
    log_message(context, XEMU_NEURAL_PLUGIN_LOG_INFO,
                "Vulkan copy validation adapter unloaded");
    free(context);
}

static XemuNeuralPluginResult copy_prepare(
    void *opaque, const XemuNeuralPluginFrameV1 *frame,
    XemuNeuralPluginStatusV1 *status)
{
    VulkanCopyContext *context = opaque;
    if (context == NULL || frame == NULL || status == NULL ||
        frame->struct_size < sizeof(*frame) || frame->color_image == 0 ||
        frame->display_width == 0 || frame->display_height == 0 ||
        frame->color_sample_count != VK_SAMPLE_COUNT_1_BIT ||
        frame->color_layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL ||
        frame->required_return_layout !=
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        if (status != NULL) {
            set_text(status->text, sizeof(status->text),
                     "Unsupported or incomplete Vulkan frame contract");
            status->state = XEMU_NEURAL_PLUGIN_STATE_FAILED;
        }
        return XEMU_NEURAL_PLUGIN_RESULT_FATAL_ERROR;
    }

    XemuNeuralPluginResult result = create_scratch(context, frame, status);
    status->state = result == XEMU_NEURAL_PLUGIN_RESULT_OK
                        ? XEMU_NEURAL_PLUGIN_STATE_READY
                        : XEMU_NEURAL_PLUGIN_STATE_FAILED;
    status->active_feature = XEMU_NEURAL_PLUGIN_FEATURE_PASSTHROUGH;
    status->evaluated_frames = context->evaluated_frames;
    status->failed_frames = context->failed_frames;
    return result;
}

static void image_barrier(VkCommandBuffer command_buffer, VkImage image,
                          VkImageLayout old_layout, VkImageLayout new_layout,
                          VkAccessFlags src_access, VkAccessFlags dst_access,
                          VkPipelineStageFlags src_stage,
                          VkPipelineStageFlags dst_stage)
{
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = src_access,
        .dstAccessMask = dst_access,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    vkCmdPipelineBarrier(command_buffer, src_stage, dst_stage, 0,
                         0, NULL, 0, NULL, 1, &barrier);
}

static XemuNeuralPluginResult copy_record(
    void *opaque, const XemuNeuralPluginVulkanRecordV1 *record,
    XemuNeuralPluginFrameOutputV1 *output)
{
    VulkanCopyContext *context = opaque;
    if (context == NULL || record == NULL || output == NULL ||
        record->struct_size < sizeof(*record) ||
        record->frame.struct_size < sizeof(record->frame) ||
        record->command_buffer == 0 || context->scratch_image == VK_NULL_HANDLE) {
        return XEMU_NEURAL_PLUGIN_RESULT_FATAL_ERROR;
    }

    VkCommandBuffer command_buffer =
        (VkCommandBuffer)(uintptr_t)record->command_buffer;
    VkImage color_image =
        (VkImage)(uintptr_t)record->frame.color_image;

    image_barrier(command_buffer, color_image,
                  (VkImageLayout)record->frame.color_layout,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                  VK_ACCESS_TRANSFER_READ_BIT,
                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT);

    image_barrier(command_buffer, context->scratch_image,
                  context->scratch_layout,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  context->scratch_layout == VK_IMAGE_LAYOUT_UNDEFINED
                      ? 0
                      : VK_ACCESS_TRANSFER_READ_BIT,
                  VK_ACCESS_TRANSFER_WRITE_BIT,
                  context->scratch_layout == VK_IMAGE_LAYOUT_UNDEFINED
                      ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                      : VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkImageCopy copy = {
        .srcSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .dstSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .extent = {
            .width = record->frame.display_width,
            .height = record->frame.display_height,
            .depth = 1,
        },
    };
    vkCmdCopyImage(command_buffer, color_image,
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   context->scratch_image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    image_barrier(command_buffer, context->scratch_image,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_ACCESS_TRANSFER_READ_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT);
    context->scratch_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    image_barrier(command_buffer, color_image,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_ACCESS_TRANSFER_READ_BIT,
                  VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT);

    vkCmdCopyImage(command_buffer, context->scratch_image,
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   color_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &copy);

    image_barrier(command_buffer, color_image,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  (VkImageLayout)record->frame.required_return_layout,
                  VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

    context->evaluated_frames++;
    output->flags = XEMU_NEURAL_PLUGIN_OUTPUT_COLOR_WRITTEN;
    output->feature = XEMU_NEURAL_PLUGIN_FEATURE_PASSTHROUGH;
    set_text(output->text, sizeof(output->text),
             "Exact Vulkan copy round trip recorded; no neural rendering");
    return XEMU_NEURAL_PLUGIN_RESULT_OK;
}

static void copy_reset(void *opaque, uint32_t reason)
{
    VulkanCopyContext *context = opaque;
    (void)reason;
    if (context != NULL) {
        log_message(context, XEMU_NEURAL_PLUGIN_LOG_DEBUG,
                    "Copy adapter history reset (no temporal state)");
    }
}

static void copy_release(void *opaque)
{
    destroy_scratch((VulkanCopyContext *)opaque);
}

static void copy_status(void *opaque, XemuNeuralPluginStatusV1 *status)
{
    VulkanCopyContext *context = opaque;
    if (status == NULL) {
        return;
    }
    status->state = context != NULL ? XEMU_NEURAL_PLUGIN_STATE_READY
                                    : XEMU_NEURAL_PLUGIN_STATE_FAILED;
    status->active_feature = XEMU_NEURAL_PLUGIN_FEATURE_PASSTHROUGH;
    status->evaluated_frames = context != NULL ? context->evaluated_frames : 0;
    status->failed_frames = context != NULL ? context->failed_frames : 0;
    set_text(status->text, sizeof(status->text),
             "Vulkan copy validation adapter; no neural model");
}

static const XemuNeuralPluginApiV1 api = {
    .struct_size = sizeof(api),
    .abi_version = XEMU_NEURAL_PLUGIN_ABI_VERSION,
    .capabilities = XEMU_NEURAL_PLUGIN_CAP_VULKAN_RECORD |
                    XEMU_NEURAL_PLUGIN_CAP_TRANSACTIONAL_IN_PLACE |
                    XEMU_NEURAL_PLUGIN_CAP_COLOR_ONLY,
    .name = "xemu-vulkan-copy-validation",
    .version = "1.0.0",
    .bootstrap = copy_bootstrap,
    .shutdown = copy_shutdown,
    .create = copy_create,
    .destroy = copy_destroy,
    .prepare_frame = copy_prepare,
    .record_vulkan = copy_record,
    .reset_history = copy_reset,
    .release_resources = copy_release,
    .get_status = copy_status,
};

XEMU_NEURAL_PLUGIN_EXPORT const XemuNeuralPluginApiV1 *
xemu_neural_present_get_api(uint32_t requested_abi_version)
{
    return requested_abi_version == XEMU_NEURAL_PLUGIN_ABI_VERSION ? &api : NULL;
}

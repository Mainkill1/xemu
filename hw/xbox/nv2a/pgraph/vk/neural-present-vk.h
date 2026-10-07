/*
 * Vulkan bridge for optional host neural-presentation adapters.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_NEURAL_PRESENT_VK_H
#define HW_XBOX_NV2A_PGRAPH_VK_NEURAL_PRESENT_VK_H

#include "hw/xbox/nv2a/pgraph/neural-present-plugin.h"
#include "hw/xbox/nv2a/pgraph/neural-present-state.h"
#include "renderer.h"

typedef struct PGRAPHVkNeuralPresentFrame {
    bool initialized;
    bool should_record;
    bool completion_pending;
    XemuNeuralPresentFrameInfo state_frame;
    XemuNeuralPresentDecision decision;
    XemuNeuralPluginFrameV1 plugin_frame;
    XemuNeuralPluginFrameOutputV1 output;
    XemuNeuralPluginResult plugin_result;
} PGRAPHVkNeuralPresentFrame;

void pgraph_vk_neural_present_bootstrap(PGRAPHState *pg);
void pgraph_vk_neural_present_init(PGRAPHState *pg);
void pgraph_vk_neural_present_finalize(PGRAPHState *pg);
const XemuNeuralPluginVulkanRequirementsV1 *
pgraph_vk_neural_present_requirements(PGRAPHState *pg);
void pgraph_vk_neural_present_reject(PGRAPHState *pg, const char *reason);
VkResult pgraph_vk_neural_present_create_instance(
    PGRAPHState *pg, const VkInstanceCreateInfo *create_info,
    const VkAllocationCallbacks *allocation_callbacks,
    VkInstance *instance_out);
VkResult pgraph_vk_neural_present_create_device(
    PGRAPHState *pg, VkPhysicalDevice physical_device,
    const VkDeviceCreateInfo *create_info,
    const VkAllocationCallbacks *allocation_callbacks,
    VkDevice *device_out);
void pgraph_vk_neural_present_release_display(PGRAPHState *pg);
void pgraph_vk_neural_present_reset(PGRAPHState *pg,
                                    XemuNeuralPresentResetReason reason);
VkImageUsageFlags pgraph_vk_neural_present_required_image_usage(
    PGRAPHState *pg);
void pgraph_vk_neural_present_prepare(
    PGRAPHState *pg, SurfaceBinding *surface, hwaddr scanout_address,
    bool pvideo_enabled, bool interlaced,
    PGRAPHVkNeuralPresentFrame *frame);
void pgraph_vk_neural_present_record(
    PGRAPHState *pg, VkCommandBuffer command_buffer,
    PGRAPHVkNeuralPresentFrame *frame);
void pgraph_vk_neural_present_complete(
    PGRAPHState *pg, PGRAPHVkNeuralPresentFrame *frame);

#endif

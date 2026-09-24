/*
 * NV2A Vulkan compile-required pipeline probe
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "pipeline-probe.h"

bool pgraph_vk_pipeline_cache_control_is_core(uint32_t instance_api_version,
                                              uint32_t device_api_version)
{
    return instance_api_version >= VK_API_VERSION_1_3 &&
           device_api_version >= VK_API_VERSION_1_3;
}

bool pgraph_vk_pipeline_cache_control_api_available(
    uint32_t instance_api_version, uint32_t device_api_version,
    bool extension_enabled)
{
    return pgraph_vk_pipeline_cache_control_is_core(instance_api_version,
                                                    device_api_version) ||
           extension_enabled;
}

PGRAPHVkPipelineProbeOutcome pgraph_vk_probe_pipeline_without_compile(
    VkDevice device, VkPipelineCache cache,
    const VkGraphicsPipelineCreateInfo *create_info, VkPipeline *pipeline,
    PGRAPHVkPipelineProbeCreateFunc create, void *opaque)
{
    PGRAPHVkPipelineProbeOutcome outcome = {
        .status = PGRAPH_VK_PIPELINE_PROBE_ERROR,
        .vk_result = VK_ERROR_INITIALIZATION_FAILED,
    };

    if (pipeline) {
        *pipeline = VK_NULL_HANDLE;
    }
    if (!create_info || !pipeline || !create) {
        return outcome;
    }

    VkGraphicsPipelineCreateInfo probe_info = *create_info;
    probe_info.flags |=
        VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT;
    outcome.vk_result = create(opaque, device, cache, &probe_info, pipeline);
    if (outcome.vk_result == VK_SUCCESS && *pipeline != VK_NULL_HANDLE) {
        outcome.status = PGRAPH_VK_PIPELINE_PROBE_READY;
    } else if (outcome.vk_result == VK_PIPELINE_COMPILE_REQUIRED) {
        *pipeline = VK_NULL_HANDLE;
        outcome.status = PGRAPH_VK_PIPELINE_PROBE_COMPILE_REQUIRED;
    } else {
        *pipeline = VK_NULL_HANDLE;
    }
    return outcome;
}

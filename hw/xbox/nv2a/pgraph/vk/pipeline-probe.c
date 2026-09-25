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

bool pgraph_vk_pipeline_probe_should_run(bool cache_control_available,
                                        bool continue_policy,
                                        bool diagnostic_enabled)
{
    return cache_control_available &&
           (continue_policy || diagnostic_enabled);
}

PGRAPHVkPipelineProbeOutcome pgraph_vk_probe_pipeline_without_compile(
    VkDevice device, VkPipelineCache cache,
    const VkGraphicsPipelineCreateInfo *create_info, VkPipeline *pipeline,
    bool cache_access_available, PGRAPHVkPipelineProbeCreateFunc create,
    PGRAPHVkPipelineProbeDestroyFunc destroy, void *opaque)
{
    PGRAPHVkPipelineProbeOutcome outcome = {
        .status = PGRAPH_VK_PIPELINE_PROBE_ERROR,
        .vk_result = VK_ERROR_INITIALIZATION_FAILED,
    };

    if (pipeline) {
        *pipeline = VK_NULL_HANDLE;
    }
    if (!create_info || !pipeline || !create || !destroy) {
        return outcome;
    }
    if (!cache_access_available) {
        outcome.status = PGRAPH_VK_PIPELINE_PROBE_BUSY;
        outcome.vk_result = VK_SUCCESS;
        return outcome;
    }

    VkGraphicsPipelineCreateInfo probe_info = *create_info;
    probe_info.flags |=
        VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT;
    /* Never contend on the application's shared pipeline cache from the
     * foreground compile-free probe. The cache argument is retained so the
     * call site must make the ownership decision explicitly. */
    (void)cache;
    outcome.vk_result = create(opaque, device, VK_NULL_HANDLE, &probe_info,
                               pipeline);
    if (outcome.vk_result == VK_SUCCESS && *pipeline != VK_NULL_HANDLE) {
        outcome.status = PGRAPH_VK_PIPELINE_PROBE_READY;
    } else if (outcome.vk_result == VK_PIPELINE_COMPILE_REQUIRED) {
        outcome.status = PGRAPH_VK_PIPELINE_PROBE_COMPILE_REQUIRED;
    }
    if (outcome.status != PGRAPH_VK_PIPELINE_PROBE_READY &&
        *pipeline != VK_NULL_HANDLE) {
        destroy(opaque, device, *pipeline);
        *pipeline = VK_NULL_HANDLE;
    }
    return outcome;
}

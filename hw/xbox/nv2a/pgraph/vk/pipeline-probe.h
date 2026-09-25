/*
 * NV2A Vulkan compile-required pipeline probe
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_XBOX_NV2A_PGRAPH_VK_PIPELINE_PROBE_H
#define HW_XBOX_NV2A_PGRAPH_VK_PIPELINE_PROBE_H

#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

typedef enum PGRAPHVkPipelineProbeResult {
    PGRAPH_VK_PIPELINE_PROBE_READY,
    PGRAPH_VK_PIPELINE_PROBE_COMPILE_REQUIRED,
    PGRAPH_VK_PIPELINE_PROBE_ERROR,
} PGRAPHVkPipelineProbeResult;

typedef struct PGRAPHVkPipelineProbeOutcome {
    PGRAPHVkPipelineProbeResult status;
    VkResult vk_result;
} PGRAPHVkPipelineProbeOutcome;

typedef VkResult (*PGRAPHVkPipelineProbeCreateFunc)(
    void *opaque, VkDevice device, VkPipelineCache cache,
    const VkGraphicsPipelineCreateInfo *create_info, VkPipeline *pipeline);

bool pgraph_vk_pipeline_cache_control_is_core(uint32_t instance_api_version,
                                              uint32_t device_api_version);
bool pgraph_vk_pipeline_cache_control_api_available(
    uint32_t instance_api_version, uint32_t device_api_version,
    bool extension_enabled);
PGRAPHVkPipelineProbeOutcome pgraph_vk_probe_pipeline_without_compile(
    VkDevice device, VkPipelineCache cache,
    const VkGraphicsPipelineCreateInfo *create_info, VkPipeline *pipeline,
    PGRAPHVkPipelineProbeCreateFunc create, void *opaque);
/* Diagnostic-only: convert a driver-ready result into a synthetic miss so
 * warm host caches cannot bypass worker-route correctness tests. */
bool pgraph_vk_pipeline_probe_apply_diagnostic_override(
    bool force_compile_required, PGRAPHVkPipelineProbeOutcome *outcome);

#endif

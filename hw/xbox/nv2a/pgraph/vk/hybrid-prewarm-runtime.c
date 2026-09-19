/*
 * NV2A Vulkan fallback-family prewarm preparation boundary
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/xbox/nv2a/pgraph/vk/hybrid-prewarm-runtime.h"
#include "hw/xbox/nv2a/pgraph/vk/hybrid-family-codec.h"

PGRAPHVkHybridPrewarmAttemptResult pgraph_vk_hybrid_prewarm_prepare_record(
    const PGRAPHVkFamilyHistoryRecord *record,
    const PGRAPHVkHybridPrewarmPrepareOps *ops, void *opaque)
{
    if (!record || !ops || !ops->device_supported || !ops->pipeline_ready ||
        !ops->cached_modules || !ops->ready_binding ||
        !ops->submit_pipeline) {
        return PGRAPH_VK_HYBRID_PREWARM_REJECTED;
    }

    PipelineKey key;
    if (!pgraph_vk_family_key_decode(record->payload,
                                     record->payload_size, &key) ||
        !ops->device_supported(opaque, &key)) {
        /* Optional persisted data never reaches a generator or driver until
         * both host-independent and current-device checks accept it. */
        return PGRAPH_VK_HYBRID_PREWARM_REJECTED;
    }
    if (ops->pipeline_ready(opaque, &key)) {
        return PGRAPH_VK_HYBRID_PREWARM_READY;
    }

    switch (ops->cached_modules(opaque, &key.shader_state)) {
    case PGRAPH_VK_CACHED_FAMILY_MODULES_READY:
        break;
    case PGRAPH_VK_CACHED_FAMILY_MODULES_MISSING:
        return PGRAPH_VK_HYBRID_PREWARM_MISSING_ARTIFACT;
    case PGRAPH_VK_CACHED_FAMILY_MODULES_DEFERRED:
        return PGRAPH_VK_HYBRID_PREWARM_DEFERRED;
    case PGRAPH_VK_CACHED_FAMILY_MODULES_REJECTED:
        return PGRAPH_VK_HYBRID_PREWARM_REJECTED;
    default:
        return PGRAPH_VK_HYBRID_PREWARM_REJECTED;
    }

    ShaderBinding *binding = ops->ready_binding(opaque, &key.shader_state);
    if (!binding) {
        return PGRAPH_VK_HYBRID_PREWARM_DEFERRED;
    }
    switch (ops->submit_pipeline(opaque, &key, binding)) {
    case PGRAPH_VK_HYBRID_PIPELINE_ACCEPTED:
        return PGRAPH_VK_HYBRID_PREWARM_SUBMITTED;
    case PGRAPH_VK_HYBRID_PIPELINE_QUEUE_FULL:
    case PGRAPH_VK_HYBRID_PIPELINE_STOPPED:
        return PGRAPH_VK_HYBRID_PREWARM_DEFERRED;
    case PGRAPH_VK_HYBRID_PIPELINE_UNSUPPORTED_RECIPE:
        return PGRAPH_VK_HYBRID_PREWARM_REJECTED;
    default:
        return PGRAPH_VK_HYBRID_PREWARM_REJECTED;
    }
}

/*
 * NV2A Vulkan first-demand readiness attribution
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_XBOX_NV2A_PGRAPH_VK_READINESS_ATTRIBUTION_H
#define HW_XBOX_NV2A_PGRAPH_VK_READINESS_ATTRIBUTION_H

#include "hw/xbox/nv2a/pgraph/vk/renderer.h"

#define PGRAPH_VK_MAX_READINESS_RECORDS 64U

typedef enum PGRAPHVkReadinessClass {
    PGRAPH_VK_READINESS_HIT,
    PGRAPH_VK_READINESS_MISSED,
    PGRAPH_VK_READINESS_TOO_LATE,
    PGRAPH_VK_READINESS_UNSUPPORTED,
    PGRAPH_VK_READINESS_QUEUE_DEFERRED,
    PGRAPH_VK_READINESS_CLASS_COUNT,
} PGRAPHVkReadinessClass;

typedef struct PGRAPHVkReadinessRecord {
    bool in_use;
    PipelineKey key;
    uint64_t key_hash;
    uint64_t generation;
    uint64_t first_demand_us;
    uint64_t last_demand_us;
    uint64_t demand_count;
    PGRAPHVkReadinessClass classification;
} PGRAPHVkReadinessRecord;

typedef struct PGRAPHVkReadinessTelemetry {
    uint64_t classified[PGRAPH_VK_READINESS_CLASS_COUNT];
    uint64_t duplicate_demands;
    uint64_t tracker_evictions;
    uint64_t generation_reclassifications;
} PGRAPHVkReadinessTelemetry;

typedef struct PGRAPHVkReadinessAttributionState {
    PGRAPHVkReadinessRecord records[PGRAPH_VK_MAX_READINESS_RECORDS];
    PGRAPHVkReadinessTelemetry telemetry;
} PGRAPHVkReadinessAttributionState;

void pgraph_vk_readiness_attribution_init(
    PGRAPHVkReadinessAttributionState *state);
PGRAPHVkReadinessClass pgraph_vk_readiness_classify_miss(
    bool predicted, bool queue_deferred, bool unsupported);
bool pgraph_vk_readiness_note_first_demand(
    PGRAPHVkReadinessAttributionState *state, const PipelineKey *key,
    uint64_t generation, uint64_t demand_us,
    PGRAPHVkReadinessClass classification);
const PGRAPHVkReadinessRecord *pgraph_vk_readiness_find(
    const PGRAPHVkReadinessAttributionState *state, const PipelineKey *key,
    uint64_t generation);

#endif

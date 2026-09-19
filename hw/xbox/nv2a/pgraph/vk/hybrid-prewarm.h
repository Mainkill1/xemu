/*
 * NV2A Vulkan opportunistic fallback-family prewarm policy
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_HYBRID_PREWARM_H
#define HW_XBOX_NV2A_PGRAPH_VK_HYBRID_PREWARM_H

#include <stdbool.h>
#include <stdint.h>

#define PGRAPH_VK_HYBRID_PREWARM_MAX_CANDIDATES 32U

typedef enum PGRAPHVkHybridPrewarmAttemptResult {
    PGRAPH_VK_HYBRID_PREWARM_IDLE,
    PGRAPH_VK_HYBRID_PREWARM_NO_CANDIDATE,
    PGRAPH_VK_HYBRID_PREWARM_READY,
    PGRAPH_VK_HYBRID_PREWARM_SUBMITTED,
    PGRAPH_VK_HYBRID_PREWARM_MISSING_ARTIFACT,
    PGRAPH_VK_HYBRID_PREWARM_DEFERRED,
    PGRAPH_VK_HYBRID_PREWARM_REJECTED,
} PGRAPHVkHybridPrewarmAttemptResult;

typedef struct PGRAPHVkHybridPrewarmState {
    bool enabled;
    uint32_t attempted;
    uint32_t scheduled;
    uint32_t ready;
    uint32_t missing;
    uint32_t deferred;
    uint32_t rejected;
    uint32_t defer_services;
} PGRAPHVkHybridPrewarmState;

typedef enum PGRAPHVkHybridPrewarmStage {
    PGRAPH_VK_HYBRID_PREWARM_VERTEX,
    PGRAPH_VK_HYBRID_PREWARM_GEOMETRY,
    PGRAPH_VK_HYBRID_PREWARM_FRAGMENT,
} PGRAPHVkHybridPrewarmStage;

typedef enum PGRAPHVkCachedFamilyModulesResult {
    PGRAPH_VK_CACHED_FAMILY_MODULES_READY,
    PGRAPH_VK_CACHED_FAMILY_MODULES_MISSING,
    PGRAPH_VK_CACHED_FAMILY_MODULES_DEFERRED,
    PGRAPH_VK_CACHED_FAMILY_MODULES_REJECTED,
} PGRAPHVkCachedFamilyModulesResult;

typedef PGRAPHVkCachedFamilyModulesResult
(*PGRAPHVkHybridPrewarmStageFunc)(void *opaque,
                                 PGRAPHVkHybridPrewarmStage stage);

typedef PGRAPHVkHybridPrewarmAttemptResult
(*PGRAPHVkHybridPrewarmAttemptFunc)(void *opaque);

PGRAPHVkHybridPrewarmAttemptResult pgraph_vk_hybrid_prewarm_service(
    PGRAPHVkHybridPrewarmState *state, bool demand_work_waiting,
    PGRAPHVkHybridPrewarmAttemptFunc attempt, void *opaque);
PGRAPHVkCachedFamilyModulesResult pgraph_vk_hybrid_prewarm_modules(
    bool geometry_required, PGRAPHVkHybridPrewarmStageFunc materialize,
    void *opaque);

#endif

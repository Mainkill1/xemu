/*
 * NV2A Vulkan opportunistic fallback-family prewarm policy
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/xbox/nv2a/pgraph/vk/hybrid-prewarm.h"

PGRAPHVkHybridPrewarmAttemptResult pgraph_vk_hybrid_prewarm_service(
    PGRAPHVkHybridPrewarmState *state, bool demand_work_waiting,
    PGRAPHVkHybridPrewarmAttemptFunc attempt, void *opaque)
{
    if (!state || !state->enabled || demand_work_waiting || !attempt ||
        state->attempted >= PGRAPH_VK_HYBRID_PREWARM_MAX_CANDIDATES) {
        return PGRAPH_VK_HYBRID_PREWARM_IDLE;
    }
    if (state->defer_services) {
        state->defer_services--;
        return PGRAPH_VK_HYBRID_PREWARM_IDLE;
    }

    PGRAPHVkHybridPrewarmAttemptResult result = attempt(opaque);
    if (result == PGRAPH_VK_HYBRID_PREWARM_NO_CANDIDATE ||
        result == PGRAPH_VK_HYBRID_PREWARM_IDLE) {
        return result;
    }

    switch (result) {
    case PGRAPH_VK_HYBRID_PREWARM_READY:
        state->attempted++;
        state->ready++;
        break;
    case PGRAPH_VK_HYBRID_PREWARM_SUBMITTED:
        state->attempted++;
        state->scheduled++;
        break;
    case PGRAPH_VK_HYBRID_PREWARM_MISSING_ARTIFACT:
        state->attempted++;
        state->missing++;
        break;
    case PGRAPH_VK_HYBRID_PREWARM_DEFERRED:
        state->deferred++;
        state->defer_services = 8;
        break;
    case PGRAPH_VK_HYBRID_PREWARM_REJECTED:
        state->attempted++;
        state->rejected++;
        break;
    default:
        g_assert_not_reached();
    }
    return result;
}

PGRAPHVkCachedFamilyModulesResult pgraph_vk_hybrid_prewarm_modules(
    bool geometry_required, PGRAPHVkHybridPrewarmStageFunc materialize,
    void *opaque)
{
    if (!materialize) {
        return PGRAPH_VK_CACHED_FAMILY_MODULES_REJECTED;
    }
    const PGRAPHVkHybridPrewarmStage stages[] = {
        PGRAPH_VK_HYBRID_PREWARM_VERTEX,
        PGRAPH_VK_HYBRID_PREWARM_GEOMETRY,
        PGRAPH_VK_HYBRID_PREWARM_FRAGMENT,
    };
    for (size_t i = 0; i < G_N_ELEMENTS(stages); i++) {
        if (stages[i] == PGRAPH_VK_HYBRID_PREWARM_GEOMETRY &&
            !geometry_required) {
            continue;
        }
        PGRAPHVkCachedFamilyModulesResult result =
            materialize(opaque, stages[i]);
        if (result != PGRAPH_VK_CACHED_FAMILY_MODULES_READY) {
            return result;
        }
    }
    return PGRAPH_VK_CACHED_FAMILY_MODULES_READY;
}

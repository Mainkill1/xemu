/* SPDX-License-Identifier: LGPL-2.0-or-later */
#ifndef HW_XBOX_NV2A_PGRAPH_VK_TEXTURE_STAGE_COUNTERS_H
#define HW_XBOX_NV2A_PGRAPH_VK_TEXTURE_STAGE_COUNTERS_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Counts calls at their original sites; these are not stage durations. */
typedef enum PGRAPHVkTextureStageCounter {
    VK_TEXTURE_WHOLE_CLEAN_RETURNS,
    VK_TEXTURE_SLOW_BIND_CALLS,
    VK_TEXTURE_STAGE_CHECKS,
    VK_TEXTURE_CLEAN_STAGE_ELIGIBLE,
    VK_TEXTURE_CLEAN_STAGE_SKIPS,
    VK_TEXTURE_CLEAN_STAGE_FORCED_REFERENCE,
    VK_TEXTURE_DIRTY_RANGE_CHECKS,
    VK_TEXTURE_CACHE_WALKS,
    VK_TEXTURE_SURFACE_OVERLAP_QUERIES,
    VK_TEXTURE_DMA_RESOLVES,
    VK_TEXTURE_PALETTE_DMA_RESOLVES,
    VK_TEXTURE_POLICY_ENABLED_BIND_CALLS,
    VK_TEXTURE_POLICY_REFERENCE_BIND_CALLS,
    VK_TEXTURE_PERF_ENABLED_BIND_CALLS,
    VK_TEXTURE_PERF_DISABLED_BIND_CALLS,
    VK_TEXTURE_MIXED_POLICY_INTERVALS,
    VK_TEXTURE_MIXED_PERF_INTERVALS,
    VK_TEXTURE_FLIP_STALL_INTERVALS,
    VK_TEXTURE_COUNTER_COUNT,
} PGRAPHVkTextureStageCounter;

typedef struct PGRAPHVkTextureStageCounters {
    uint64_t values[VK_TEXTURE_COUNTER_COUNT];
    uint64_t frame;
    /* Bit 0 observes false, bit 1 observes true; both means mixed. */
    uint8_t observed_policy;
    uint8_t observed_perf;
    bool collect_evidence;
    bool in_bind;
    bool collecting;
    bool overflowed;
    bool frame_overflowed;
} PGRAPHVkTextureStageCounters;

static inline void pgraph_vk_texture_stage_counter_increment(
    PGRAPHVkTextureStageCounters *counters, PGRAPHVkTextureStageCounter counter)
{
    if (counters->values[counter] == UINT64_MAX) {
        counters->overflowed = true;
    } else {
        counters->values[counter]++;
    }
}

static inline void
pgraph_vk_texture_stage_counter_begin(PGRAPHVkTextureStageCounters *counters,
                                      bool perf_enabled, bool skip_clean)
{
    if (!perf_enabled && !counters->collect_evidence) {
        return;
    }
    counters->in_bind = true;
    counters->collecting = true;
    counters->observed_policy |= skip_clean ? 2 : 1;
    counters->observed_perf |= perf_enabled ? 2 : 1;
    pgraph_vk_texture_stage_counter_increment(
        counters, skip_clean ? VK_TEXTURE_POLICY_ENABLED_BIND_CALLS :
                               VK_TEXTURE_POLICY_REFERENCE_BIND_CALLS);
    pgraph_vk_texture_stage_counter_increment(
        counters, perf_enabled ? VK_TEXTURE_PERF_ENABLED_BIND_CALLS :
                                 VK_TEXTURE_PERF_DISABLED_BIND_CALLS);
}

static inline void
pgraph_vk_texture_stage_counter_add(PGRAPHVkTextureStageCounters *counters,
                                    PGRAPHVkTextureStageCounter counter)
{
    if (counters->in_bind && counters->collecting) {
        pgraph_vk_texture_stage_counter_increment(counters, counter);
    }
}

static inline void
pgraph_vk_texture_stage_counter_eligible(PGRAPHVkTextureStageCounters *counters,
                                         bool skipped)
{
    if (!counters->in_bind || !counters->collecting) {
        return;
    }
    PGRAPHVkTextureStageCounter selected =
        skipped ? VK_TEXTURE_CLEAN_STAGE_SKIPS :
                  VK_TEXTURE_CLEAN_STAGE_FORCED_REFERENCE;
    /* Increment the pair together so even an overflow retains the invariant. */
    if (counters->values[VK_TEXTURE_CLEAN_STAGE_ELIGIBLE] == UINT64_MAX ||
        counters->values[selected] == UINT64_MAX) {
        counters->overflowed = true;
        return;
    }
    counters->values[VK_TEXTURE_CLEAN_STAGE_ELIGIBLE]++;
    counters->values[selected]++;
}

static inline void
pgraph_vk_texture_stage_counter_end(PGRAPHVkTextureStageCounters *counters)
{
    if (counters->collecting) {
        counters->in_bind = false;
        counters->collecting = false;
    }
}

static inline void pgraph_vk_texture_stage_counter_finish_interval(
    PGRAPHVkTextureStageCounters *counters, bool perf_enabled, bool skip_clean)
{
    counters->observed_perf |= perf_enabled ? 2 : 1;
    counters->observed_policy |= skip_clean ? 2 : 1;
    if (counters->observed_perf == 3) {
        pgraph_vk_texture_stage_counter_increment(
            counters, VK_TEXTURE_MIXED_PERF_INTERVALS);
    }
    if (counters->observed_policy == 3) {
        pgraph_vk_texture_stage_counter_increment(
            counters, VK_TEXTURE_MIXED_POLICY_INTERVALS);
    }
    pgraph_vk_texture_stage_counter_increment(counters,
                                              VK_TEXTURE_FLIP_STALL_INTERVALS);
    if (counters->frame == UINT64_MAX) {
        counters->frame_overflowed = true;
    } else {
        counters->frame++;
    }
}

static inline void pgraph_vk_texture_stage_counter_reset_interval(
    PGRAPHVkTextureStageCounters *counters)
{
    memset(counters->values, 0, sizeof(counters->values));
    counters->observed_policy = 0;
    counters->observed_perf = 0;
    counters->in_bind = false;
    counters->collecting = false;
    counters->overflowed = false;
}

#endif

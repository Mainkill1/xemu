/*
 * MCPX APU voice work scheduling policy helpers
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_XBOX_MCPX_APU_VP_VOICE_WORK_SCHEDULE_H
#define HW_XBOX_MCPX_APU_VP_VOICE_WORK_SCHEDULE_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hw/xbox/mcpx/apu/apu_regs.h"

#define VOICE_WORK_INLINE_QUEUE_THRESHOLD 2
#define VOICE_WORK_DENSE_MIXBIN_THRESHOLD (NUM_MIXBINS / 2)

typedef struct MCPXAPUVoiceWorkScheduleState {
    unsigned int num_workers;
    unsigned int next_worker_to_schedule;
    bool group;
    uint32_t dirty;
    uint32_t touched_mixbins;
    uint64_t workers_pending;
} MCPXAPUVoiceWorkScheduleState;

static inline bool mcpx_apu_voice_work_should_process_inline(size_t queue_len)
{
    return queue_len <= VOICE_WORK_INLINE_QUEUE_THRESHOLD;
}

static inline unsigned int mcpx_apu_voice_work_signal_count(uint64_t pending)
{
    unsigned int count = 0;

    while (pending) {
        count += pending & 1;
        pending >>= 1;
    }

    return count;
}

static inline uint32_t mcpx_apu_voice_work_touched_mixbins(uint32_t src,
                                                           uint32_t dst,
                                                           uint32_t clr)
{
    return src | dst | clr;
}

static inline uint32_t mcpx_apu_voice_work_full_mixbin_mask(void)
{
#if NUM_MIXBINS >= 32
    return UINT32_MAX;
#else
    return (1U << NUM_MIXBINS) - 1;
#endif
}

static inline bool mcpx_apu_voice_work_mixbin_mask_is_full(uint32_t mask)
{
    return mask == mcpx_apu_voice_work_full_mixbin_mask();
}

static inline bool mcpx_apu_voice_work_mixbin_mask_is_dense(uint32_t mask)
{
    return mcpx_apu_voice_work_mixbin_mask_is_full(mask) ||
           mcpx_apu_voice_work_signal_count(mask) >
               VOICE_WORK_DENSE_MIXBIN_THRESHOLD;
}

static inline void mcpx_apu_voice_work_schedule_init(
    MCPXAPUVoiceWorkScheduleState *state, unsigned int num_workers)
{
    assert(state);
    assert(num_workers > 0);
    assert(num_workers <= 64);

    *state = (MCPXAPUVoiceWorkScheduleState) {
        .num_workers = num_workers,
    };
}

static inline unsigned int mcpx_apu_voice_work_schedule_assign_one(
    MCPXAPUVoiceWorkScheduleState *state, uint32_t src, uint32_t dst,
    uint32_t clr)
{
    const uint32_t multipass = MULTIPASS_BIN_MASK;
    const uint32_t touched_mixbins =
        mcpx_apu_voice_work_touched_mixbins(src, dst, clr);
    unsigned int worker;

    assert(state);
    assert(state->num_workers > 0);
    assert(state->num_workers <= 64);

    /*
     * These mirror the runtime assumptions documented in vp.c. The helper is
     * deliberately a policy extraction, not a broader scheduler rewrite.
     */
    assert(!src || (src == multipass));
    assert(!src || (clr == multipass));
    assert(src || (dst & multipass) || !(state->dirty & multipass));

    if ((dst & multipass) & ~state->dirty) {
        state->group = true;
    }

    worker = state->next_worker_to_schedule;
    state->workers_pending |= 1ULL << worker;
    state->touched_mixbins |= touched_mixbins;

    state->dirty = (state->dirty & ~clr) | dst;
    if (clr & multipass) {
        state->group = false;
    }

    if (!state->group && state->num_workers > 1) {
        state->next_worker_to_schedule =
            (state->next_worker_to_schedule + 1) % state->num_workers;
    }

    return worker;
}

#endif

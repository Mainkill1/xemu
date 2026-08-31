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
#define VOICE_WORK_MIN_VOICES_PER_WORKER 2

typedef struct MCPXAPUVoiceWorkScheduleState {
    unsigned int num_workers;
    unsigned int next_worker_to_schedule;
    bool group;
    uint32_t dirty;
    uint64_t workers_pending;
} MCPXAPUVoiceWorkScheduleState;

static inline bool mcpx_apu_voice_work_should_process_inline(size_t queue_len)
{
    return queue_len <= VOICE_WORK_INLINE_QUEUE_THRESHOLD;
}

static inline unsigned int mcpx_apu_voice_work_effective_workers(
    size_t queue_len, unsigned int configured_workers)
{
    size_t useful_workers;

    assert(configured_workers > 0);
    assert(configured_workers <= 64);

    if (mcpx_apu_voice_work_should_process_inline(queue_len)) {
        return 0;
    }

    useful_workers = 1 + (queue_len - 1) / VOICE_WORK_MIN_VOICES_PER_WORKER;
    return useful_workers < configured_workers ? useful_workers :
                                                 configured_workers;
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

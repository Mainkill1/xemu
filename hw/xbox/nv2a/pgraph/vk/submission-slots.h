/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_SUBMISSION_SLOTS_H
#define HW_XBOX_NV2A_PGRAPH_VK_SUBMISSION_SLOTS_H

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct PGRAPHVkSubmissionSlotState {
    bool in_flight;
    uint32_t submission_serial;
} PGRAPHVkSubmissionSlotState;

static inline uint32_t pgraph_vk_next_submission_slot(uint32_t current,
                                                      uint32_t num_slots)
{
    assert(num_slots > 0);
    assert(current < num_slots);
    return current + 1 == num_slots ? 0 : current + 1;
}

static inline void pgraph_vk_submission_slot_mark_submitted(
    PGRAPHVkSubmissionSlotState *slot, uint32_t submission_serial)
{
    assert(!slot->in_flight);
    slot->submission_serial = submission_serial;
    slot->in_flight = true;
}

static inline void pgraph_vk_submission_slot_mark_retired(
    PGRAPHVkSubmissionSlotState *slot)
{
    assert(slot->in_flight);
    slot->in_flight = false;
}

#endif

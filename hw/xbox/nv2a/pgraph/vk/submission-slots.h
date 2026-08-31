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
    bool submission_serial_valid;
    uint32_t submission_serial;
    uint32_t descriptor_set_index;
    uint32_t uniform_buffer_offsets[2];
    /* Cursor into shared buffers; backing storage is not yet per-slot. */
    uint64_t uniform_staging_offset;
} PGRAPHVkSubmissionSlotState;

typedef void (*PGRAPHVkSubmissionSlotCallback)(void *opaque);

typedef struct PGRAPHVkSubmissionSlotRetireCallbacks {
    PGRAPHVkSubmissionSlotCallback wait_for_completion;
    PGRAPHVkSubmissionSlotCallback retire_resources;
    PGRAPHVkSubmissionSlotCallback reset_completion;
    PGRAPHVkSubmissionSlotCallback reset_complete;
} PGRAPHVkSubmissionSlotRetireCallbacks;

static inline bool pgraph_vk_submission_slot_needs_retirement(
    const PGRAPHVkSubmissionSlotState *slot)
{
    assert(slot->in_flight == slot->submission_serial_valid);
    return slot->in_flight;
}

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
    assert(!slot->submission_serial_valid);
    slot->submission_serial = submission_serial;
    slot->submission_serial_valid = true;
    slot->in_flight = true;
}

static inline void pgraph_vk_submission_slot_mark_retired(
    PGRAPHVkSubmissionSlotState *slot)
{
    assert(slot->in_flight);
    assert(slot->submission_serial_valid);
    slot->in_flight = false;
    slot->submission_serial_valid = false;
    slot->submission_serial = 0;
}

static inline uint32_t pgraph_vk_submission_slot_allocate_descriptor_set(
    PGRAPHVkSubmissionSlotState *slot, uint32_t capacity)
{
    assert(!slot->in_flight);
    assert(slot->descriptor_set_index < capacity);
    return slot->descriptor_set_index++;
}

static inline uint64_t pgraph_vk_submission_slot_get_uniform_staging_offset(
    const PGRAPHVkSubmissionSlotState *slot)
{
    return slot->uniform_staging_offset;
}

static inline void pgraph_vk_submission_slot_set_uniform_staging_offset(
    PGRAPHVkSubmissionSlotState *slot, uint64_t offset)
{
    assert(!slot->in_flight);
    slot->uniform_staging_offset = offset;
}

static inline void pgraph_vk_submission_slot_reset_transients(
    PGRAPHVkSubmissionSlotState *slot)
{
    assert(!slot->in_flight);
    slot->descriptor_set_index = 0;
    slot->uniform_buffer_offsets[0] = 0;
    slot->uniform_buffer_offsets[1] = 0;
    slot->uniform_staging_offset = 0;
}

/*
 * Complete all host-side retirement in one ordered operation. Slot-owned
 * resources are retired after completion is observed, while the submission
 * serial and transient ownership are still intact. Only then is the reusable
 * completion primitive reset and the slot made available for reuse.
 */
static inline bool pgraph_vk_submission_slot_retire_with_callbacks(
    PGRAPHVkSubmissionSlotState *slot,
    const PGRAPHVkSubmissionSlotRetireCallbacks *callbacks,
    void *opaque)
{
    if (!pgraph_vk_submission_slot_needs_retirement(slot)) {
        return false;
    }

    assert(slot->submission_serial_valid);
    assert(callbacks);
    assert(callbacks->wait_for_completion);
    assert(callbacks->reset_completion);

    callbacks->wait_for_completion(opaque);
    if (callbacks->retire_resources) {
        callbacks->retire_resources(opaque);
    }
    callbacks->reset_completion(opaque);
    pgraph_vk_submission_slot_mark_retired(slot);
    pgraph_vk_submission_slot_reset_transients(slot);

    if (callbacks->reset_complete) {
        callbacks->reset_complete(opaque);
    }
    return true;
}

#endif

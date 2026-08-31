/*
 * Vulkan NV2A submission-slot tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/submission-slots.h"

static void test_slot_selection(void)
{
    g_assert_cmpuint(pgraph_vk_next_submission_slot(0, 1), ==, 0);
    g_assert_cmpuint(pgraph_vk_next_submission_slot(0, 2), ==, 1);
    g_assert_cmpuint(pgraph_vk_next_submission_slot(1, 2), ==, 0);
    g_assert_cmpuint(pgraph_vk_next_submission_slot(2, 3), ==, 0);
}

static void test_slot_lifetime(void)
{
    PGRAPHVkSubmissionSlotState slot = { 0 };

    g_assert_false(slot.in_flight);
    g_assert_false(slot.submission_serial_valid);
    g_assert_cmpuint(slot.submission_serial, ==, 0);
    g_assert_false(pgraph_vk_submission_slot_needs_retirement(&slot));

    pgraph_vk_submission_slot_mark_submitted(&slot, 7);
    g_assert_true(slot.in_flight);
    g_assert_true(slot.submission_serial_valid);
    g_assert_cmpuint(slot.submission_serial, ==, 7);
    g_assert_true(pgraph_vk_submission_slot_needs_retirement(&slot));

    pgraph_vk_submission_slot_mark_retired(&slot);
    g_assert_false(slot.in_flight);
    g_assert_false(slot.submission_serial_valid);
    g_assert_cmpuint(slot.submission_serial, ==, 0);
    g_assert_false(pgraph_vk_submission_slot_needs_retirement(&slot));

    pgraph_vk_submission_slot_mark_submitted(&slot, 8);
    g_assert_true(slot.in_flight);
    g_assert_cmpuint(slot.submission_serial, ==, 8);

    pgraph_vk_submission_slot_mark_retired(&slot);
    pgraph_vk_submission_slot_mark_submitted(&slot, 0);
    g_assert_true(slot.in_flight);
    g_assert_true(slot.submission_serial_valid);
    g_assert_cmpuint(slot.submission_serial, ==, 0);
}

enum RetireEvent {
    RETIRE_EVENT_WAIT,
    RETIRE_EVENT_RESOURCES,
    RETIRE_EVENT_RESET,
    RETIRE_EVENT_RESET_COMPLETE,
};

typedef struct RetireRecorder {
    PGRAPHVkSubmissionSlotState *slot;
    enum RetireEvent events[4];
    size_t num_events;
} RetireRecorder;

static void record_wait(void *opaque)
{
    RetireRecorder *recorder = opaque;

    g_assert_true(recorder->slot->in_flight);
    g_assert_true(recorder->slot->submission_serial_valid);
    recorder->events[recorder->num_events++] = RETIRE_EVENT_WAIT;
}

static void record_resources(void *opaque)
{
    RetireRecorder *recorder = opaque;

    g_assert_true(recorder->slot->in_flight);
    g_assert_true(recorder->slot->submission_serial_valid);
    g_assert_cmpuint(recorder->slot->submission_serial, ==, 11);
    g_assert_cmpuint(recorder->slot->descriptor_set_index, ==, 5);
    g_assert_cmpuint(recorder->slot->uniform_buffer_offsets[0], ==, 256);
    g_assert_cmpuint(recorder->slot->uniform_buffer_offsets[1], ==, 512);
    g_assert_cmpuint(recorder->slot->uniform_staging_offset, ==, 768);
    recorder->events[recorder->num_events++] = RETIRE_EVENT_RESOURCES;
}

static void record_reset(void *opaque)
{
    RetireRecorder *recorder = opaque;

    g_assert_true(recorder->slot->in_flight);
    g_assert_true(recorder->slot->submission_serial_valid);
    recorder->events[recorder->num_events++] = RETIRE_EVENT_RESET;
}

static void record_reset_complete(void *opaque)
{
    RetireRecorder *recorder = opaque;

    g_assert_false(recorder->slot->in_flight);
    g_assert_false(recorder->slot->submission_serial_valid);
    g_assert_cmpuint(recorder->slot->submission_serial, ==, 0);
    g_assert_cmpuint(recorder->slot->descriptor_set_index, ==, 0);
    g_assert_cmpuint(recorder->slot->uniform_staging_offset, ==, 0);
    recorder->events[recorder->num_events++] = RETIRE_EVENT_RESET_COMPLETE;
}

static void test_slot_retire_callback_order(void)
{
    PGRAPHVkSubmissionSlotState slot = {
        .descriptor_set_index = 5,
        .uniform_buffer_offsets = { 256, 512 },
        .uniform_staging_offset = 768,
    };
    RetireRecorder recorder = { .slot = &slot };
    const PGRAPHVkSubmissionSlotRetireCallbacks callbacks = {
        .wait_for_completion = record_wait,
        .retire_resources = record_resources,
        .reset_completion = record_reset,
        .reset_complete = record_reset_complete,
    };

    pgraph_vk_submission_slot_mark_submitted(&slot, 11);
    g_assert_true(pgraph_vk_submission_slot_retire_with_callbacks(
        &slot, &callbacks, &recorder));
    g_assert_cmpuint(recorder.num_events, ==, 4);
    g_assert_cmpint(recorder.events[0], ==, RETIRE_EVENT_WAIT);
    g_assert_cmpint(recorder.events[1], ==, RETIRE_EVENT_RESOURCES);
    g_assert_cmpint(recorder.events[2], ==, RETIRE_EVENT_RESET);
    g_assert_cmpint(recorder.events[3], ==, RETIRE_EVENT_RESET_COMPLETE);

    recorder.num_events = 0;
    g_assert_false(pgraph_vk_submission_slot_retire_with_callbacks(
        &slot, &callbacks, &recorder));
    g_assert_cmpuint(recorder.num_events, ==, 0);
}

static void test_slot_transient_allocation(void)
{
    PGRAPHVkSubmissionSlotState slot = { 0 };

    g_assert_cmpuint(pgraph_vk_submission_slot_allocate_descriptor_set(
                         &slot, 2),
                     ==, 0);
    g_assert_cmpuint(pgraph_vk_submission_slot_allocate_descriptor_set(
                         &slot, 2),
                     ==, 1);
    slot.uniform_buffer_offsets[0] = 256;
    slot.uniform_buffer_offsets[1] = 512;
    slot.uniform_staging_offset = 768;

    pgraph_vk_submission_slot_reset_transients(&slot);
    g_assert_cmpuint(slot.descriptor_set_index, ==, 0);
    g_assert_cmpuint(slot.uniform_buffer_offsets[0], ==, 0);
    g_assert_cmpuint(slot.uniform_buffer_offsets[1], ==, 0);
    g_assert_cmpuint(slot.uniform_staging_offset, ==, 0);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/vk-submission-slots/selection",
                    test_slot_selection);
    g_test_add_func("/xbox/vk-submission-slots/lifetime",
                    test_slot_lifetime);
    g_test_add_func("/xbox/vk-submission-slots/retire-callback-order",
                    test_slot_retire_callback_order);
    g_test_add_func("/xbox/vk-submission-slots/transient-allocation",
                    test_slot_transient_allocation);
    return g_test_run();
}

/*
 * Vulkan NV2A submission-slot tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/resource-pins.h"
#include "hw/xbox/nv2a/pgraph/vk/submission-slots.h"

typedef struct TestPinnedResource {
    unsigned int refs;
    unsigned int retains;
    unsigned int releases;
} TestPinnedResource;

typedef struct TestPinCallbacks {
    unsigned int retains;
    unsigned int releases;
} TestPinCallbacks;

static void test_resource_retain(void *opaque, void *resource)
{
    TestPinCallbacks *callbacks = opaque;
    TestPinnedResource *test_resource = resource;

    test_resource->refs++;
    test_resource->retains++;
    callbacks->retains++;
}

static void test_resource_release(void *opaque, void *resource)
{
    TestPinCallbacks *callbacks = opaque;
    TestPinnedResource *test_resource = resource;

    g_assert_cmpuint(test_resource->refs, >, 0);
    test_resource->refs--;
    test_resource->releases++;
    callbacks->releases++;
}

static void alternate_resource_release(void *opaque, void *resource)
{
    test_resource_release(opaque, resource);
}

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
    g_assert_cmpuint(recorder->slot->num_queries, ==, 3);
    g_assert_true(recorder->slot->new_query_needed);
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
    g_assert_cmpuint(recorder->slot->num_queries, ==, 0);
    g_assert_false(recorder->slot->new_query_needed);
    recorder->events[recorder->num_events++] = RETIRE_EVENT_RESET_COMPLETE;
}

static void test_slot_retire_callback_order(void)
{
    PGRAPHVkSubmissionSlotState slot = {
        .descriptor_set_index = 5,
        .uniform_buffer_offsets = { 256, 512 },
        .uniform_staging_offset = 768,
        .num_queries = 3,
        .new_query_needed = true,
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

static void test_slot_uniform_staging_offsets_are_independent(void)
{
    PGRAPHVkSubmissionSlotState slots[2] = { 0 };

    pgraph_vk_submission_slot_set_uniform_staging_offset(&slots[0], 256);
    pgraph_vk_submission_slot_set_uniform_staging_offset(&slots[1], 1024);
    g_assert_cmpuint(
        pgraph_vk_submission_slot_get_uniform_staging_offset(&slots[0]),
        ==, 256);
    g_assert_cmpuint(
        pgraph_vk_submission_slot_get_uniform_staging_offset(&slots[1]),
        ==, 1024);

    pgraph_vk_submission_slot_reset_transients(&slots[0]);
    g_assert_cmpuint(
        pgraph_vk_submission_slot_get_uniform_staging_offset(&slots[0]),
        ==, 0);
    g_assert_cmpuint(
        pgraph_vk_submission_slot_get_uniform_staging_offset(&slots[1]),
        ==, 1024);
}

static void test_slot_query_state_is_independent(void)
{
    PGRAPHVkSubmissionSlotState slots[2] = { 0 };
    uint32_t query_index;

    pgraph_vk_submission_slot_request_new_query(&slots[0]);
    g_assert_true(pgraph_vk_submission_slot_query_needs_begin(&slots[0]));
    g_assert_true(pgraph_vk_submission_slot_query_needs_begin(&slots[1]));

    g_assert_true(pgraph_vk_submission_slot_begin_query(
        &slots[0], 2, &query_index));
    g_assert_cmpuint(query_index, ==, 0);
    g_assert_true(slots[0].query_in_flight);
    g_assert_false(slots[0].new_query_needed);
    g_assert_cmpuint(slots[1].num_queries, ==, 0);

    g_assert_cmpuint(pgraph_vk_submission_slot_end_query(&slots[0]), ==, 0);
    pgraph_vk_submission_slot_request_new_query(&slots[0]);
    g_assert_true(pgraph_vk_submission_slot_begin_query(
        &slots[0], 2, &query_index));
    g_assert_cmpuint(query_index, ==, 1);
    g_assert_cmpuint(pgraph_vk_submission_slot_end_query(&slots[0]), ==, 1);
    g_assert_false(pgraph_vk_submission_slot_begin_query(
        &slots[0], 2, &query_index));

    pgraph_vk_submission_slot_reset_transients(&slots[0]);
    g_assert_cmpuint(slots[0].num_queries, ==, 0);
    g_assert_false(slots[0].new_query_needed);
    g_assert_cmpuint(slots[1].num_queries, ==, 0);
}

static void test_resource_pins_coalesce_and_grow(void)
{
    PGRAPHVkResourcePinRegistry registry;
    TestPinnedResource resources[6] = { 0 };
    TestPinCallbacks callbacks = { 0 };

    g_assert_true(pgraph_vk_resource_pin_registry_init(&registry,
                                                       G_N_ELEMENTS(resources)));
    g_assert_cmpint(pgraph_vk_resource_pin_registry_try_pin(
                        &registry, &resources[0], &resources[0],
                        test_resource_retain, test_resource_release,
                        &callbacks),
                    ==, PGRAPH_VK_RESOURCE_PIN_ADDED);
    g_assert_cmpint(pgraph_vk_resource_pin_registry_try_pin(
                        &registry, &resources[0], &resources[0],
                        test_resource_retain, test_resource_release,
                        &callbacks),
                    ==, PGRAPH_VK_RESOURCE_PIN_DUPLICATE);
    g_assert_cmpuint(resources[0].refs, ==, 1);
    g_assert_cmpuint(callbacks.retains, ==, 1);

    for (size_t i = 1; i < G_N_ELEMENTS(resources); i++) {
        g_assert_cmpint(pgraph_vk_resource_pin_registry_try_pin(
                            &registry, &resources[i], &resources[i],
                            test_resource_retain, test_resource_release,
                            &callbacks),
                        ==, PGRAPH_VK_RESOURCE_PIN_ADDED);
    }
    g_assert_cmpuint(registry.count, ==, G_N_ELEMENTS(resources));
    g_assert_cmpuint(registry.capacity, >=, 16);
    g_assert_cmpuint(callbacks.retains, ==, G_N_ELEMENTS(resources));
    for (size_t i = 0; i < G_N_ELEMENTS(resources); i++) {
        g_assert_cmpint(pgraph_vk_resource_pin_registry_try_pin(
                            &registry, &resources[i], &resources[i],
                            test_resource_retain, test_resource_release,
                            &callbacks),
                        ==, PGRAPH_VK_RESOURCE_PIN_DUPLICATE);
    }
    g_assert_cmpuint(callbacks.retains, ==, G_N_ELEMENTS(resources));

    g_assert_true(pgraph_vk_resource_pin_registry_finalize(&registry));
    g_assert_cmpuint(callbacks.releases, ==, G_N_ELEMENTS(resources));
    for (size_t i = 0; i < G_N_ELEMENTS(resources); i++) {
        g_assert_cmpuint(resources[i].refs, ==, 0);
    }
    g_assert_null(registry.pins);
    g_assert_cmpuint(registry.capacity, ==, 0);
}

static void test_resource_pins_are_bounded_and_conflicts_fail(void)
{
    PGRAPHVkResourcePinRegistry registry;
    PGRAPHVkResourcePinRegistry invalid_registry;
    TestPinnedResource resources[3] = { 0 };
    TestPinCallbacks callbacks = { 0 };

    g_assert_false(pgraph_vk_resource_pin_registry_init(&invalid_registry,
                                                         SIZE_MAX));
    g_assert_true(pgraph_vk_resource_pin_registry_init(&registry, 2));
    for (size_t i = 0; i < 2; i++) {
        g_assert_cmpint(pgraph_vk_resource_pin_registry_try_pin(
                            &registry, &resources[i], &resources[i],
                            test_resource_retain, test_resource_release,
                            &callbacks),
                        ==, PGRAPH_VK_RESOURCE_PIN_ADDED);
    }
    g_assert_cmpint(pgraph_vk_resource_pin_registry_try_pin(
                        &registry, &resources[2], &resources[2],
                        test_resource_retain, test_resource_release,
                        &callbacks),
                    ==, PGRAPH_VK_RESOURCE_PIN_FULL);
    g_assert_cmpint(pgraph_vk_resource_pin_registry_try_pin(
                        &registry, &resources[0], &resources[1],
                        test_resource_retain, test_resource_release,
                        &callbacks),
                    ==, PGRAPH_VK_RESOURCE_PIN_CONFLICT);
    g_assert_cmpint(pgraph_vk_resource_pin_registry_try_pin(
                        &registry, &resources[0], &resources[0],
                        test_resource_retain, alternate_resource_release,
                        &callbacks),
                    ==, PGRAPH_VK_RESOURCE_PIN_CONFLICT);
    g_assert_cmpuint(registry.count, ==, 2);
    g_assert_cmpuint(callbacks.retains, ==, 2);
    g_assert_true(pgraph_vk_resource_pin_registry_finalize(&registry));
    g_assert_cmpuint(callbacks.releases, ==, 2);
}

static void test_resource_pin_serial_ownership_wraps_safely(void)
{
    PGRAPHVkResourcePinRegistry registry;
    TestPinnedResource resource = { 0 };
    TestPinCallbacks callbacks = { 0 };

    g_assert_true(pgraph_vk_resource_pin_registry_init(&registry, 1));
    g_assert_cmpint(pgraph_vk_resource_pin_registry_try_pin(
                        &registry, &resource, &resource, test_resource_retain,
                        test_resource_release, &callbacks),
                    ==, PGRAPH_VK_RESOURCE_PIN_ADDED);
    g_assert_true(pgraph_vk_resource_pin_registry_mark_submitted(
        &registry, UINT32_MAX));
    g_assert_true(registry.submission_serial_valid);
    g_assert_cmpuint(registry.submission_serial, ==, UINT32_MAX);
    g_assert_cmpint(pgraph_vk_resource_pin_registry_try_pin(
                        &registry, &resource, &resource, test_resource_retain,
                        test_resource_release, &callbacks),
                    ==, PGRAPH_VK_RESOURCE_PIN_IN_FLIGHT);
    g_assert_false(pgraph_vk_resource_pin_registry_retire(&registry, 0));
    g_assert_cmpuint(resource.refs, ==, 1);
    g_assert_true(pgraph_vk_resource_pin_registry_retire(&registry,
                                                         UINT32_MAX));
    g_assert_false(registry.submission_serial_valid);
    g_assert_cmpuint(registry.count, ==, 0);
    g_assert_cmpuint(registry.capacity, ==, 8);
    g_assert_cmpuint(resource.refs, ==, 0);

    g_assert_cmpint(pgraph_vk_resource_pin_registry_try_pin(
                        &registry, &resource, &resource, test_resource_retain,
                        test_resource_release, &callbacks),
                    ==, PGRAPH_VK_RESOURCE_PIN_ADDED);
    g_assert_true(pgraph_vk_resource_pin_registry_mark_submitted(&registry, 0));
    g_assert_true(registry.submission_serial_valid);
    g_assert_cmpuint(registry.submission_serial, ==, 0);
    g_assert_true(pgraph_vk_resource_pin_registry_retire(&registry, 0));
    g_assert_cmpuint(resource.refs, ==, 0);
    g_assert_true(pgraph_vk_resource_pin_registry_finalize(&registry));
}

static void test_resource_pin_teardown_rejects_in_flight(void)
{
    PGRAPHVkResourcePinRegistry registry;
    TestPinnedResource resource = { 0 };
    TestPinCallbacks callbacks = { 0 };

    g_assert_true(pgraph_vk_resource_pin_registry_init(&registry, 1));
    g_assert_cmpint(pgraph_vk_resource_pin_registry_try_pin(
                        &registry, &resource, &resource, test_resource_retain,
                        test_resource_release, &callbacks),
                    ==, PGRAPH_VK_RESOURCE_PIN_ADDED);
    g_assert_true(pgraph_vk_resource_pin_registry_mark_submitted(&registry,
                                                                 17));
    g_assert_false(pgraph_vk_resource_pin_registry_finalize(&registry));
    g_assert_cmpuint(resource.refs, ==, 1);
    g_assert_true(pgraph_vk_resource_pin_registry_retire(&registry, 17));
    g_assert_true(pgraph_vk_resource_pin_registry_finalize(&registry));
    g_assert_cmpuint(resource.refs, ==, 0);
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
    g_test_add_func("/xbox/vk-submission-slots/uniform-staging-isolation",
                    test_slot_uniform_staging_offsets_are_independent);
    g_test_add_func("/xbox/vk-submission-slots/query-state-isolation",
                    test_slot_query_state_is_independent);
    g_test_add_func("/xbox/vk-submission-slots/resource-pins/coalesce-grow",
                    test_resource_pins_coalesce_and_grow);
    g_test_add_func("/xbox/vk-submission-slots/resource-pins/bounds-conflicts",
                    test_resource_pins_are_bounded_and_conflicts_fail);
    g_test_add_func("/xbox/vk-submission-slots/resource-pins/serial-wrap",
                    test_resource_pin_serial_ownership_wraps_safely);
    g_test_add_func("/xbox/vk-submission-slots/resource-pins/teardown",
                    test_resource_pin_teardown_rejects_in_flight);
    return g_test_run();
}

/*
 * NV2A Vulkan hybrid specialization policy tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hw/xbox/nv2a/pgraph/vk/hybrid-policy.h"

static PGRAPHVkHybridKey key(uint64_t value)
{
    return (PGRAPHVkHybridKey) {
        .words = { value, value ^ UINT64_C(0x5555555555555555),
                   value + 17, value * 3 },
    };
}

static PGRAPHVkHybridDecision choose(const PGRAPHVkHybridEntry *entry,
                                     PGRAPHVkHybridKey desired,
                                     uint64_t generation,
                                     uint64_t epoch,
                                     bool fallback_pipeline,
                                     bool fallback_resources,
                                     bool queue_capacity)
{
    return pgraph_vk_hybrid_choose(
        entry,
        &(PGRAPHVkHybridRequest) {
            .key = desired,
            .generation = generation,
            .epoch = epoch,
            .fallback_pipeline_ready = fallback_pipeline,
            .fallback_draw_resources_ready = fallback_resources,
            .queue_has_capacity = queue_capacity,
        });
}

static void test_route_selection_and_queue_full_behavior(void)
{
    static const struct {
        bool specialization_ready;
        bool fallback_pipeline;
        bool fallback_resources;
        PGRAPHVkHybridRoute route;
        PGRAPHVkHybridReason reason;
        bool request_specialization;
    } cases[] = {
        { false, false, false, PGRAPH_VK_HYBRID_USE_SYNCHRONOUS,
          PGRAPH_VK_HYBRID_REASON_NO_FALLBACK_PIPELINE, false },
        { false, false, true, PGRAPH_VK_HYBRID_USE_SYNCHRONOUS,
          PGRAPH_VK_HYBRID_REASON_NO_FALLBACK_PIPELINE, false },
        { false, true, false, PGRAPH_VK_HYBRID_USE_SYNCHRONOUS,
          PGRAPH_VK_HYBRID_REASON_NO_DRAW_RESOURCES, false },
        { false, true, true, PGRAPH_VK_HYBRID_USE_FALLBACK,
          PGRAPH_VK_HYBRID_REASON_FALLBACK_AND_ENQUEUE, true },
        { true, false, false, PGRAPH_VK_HYBRID_USE_SPECIALIZED,
          PGRAPH_VK_HYBRID_REASON_SPECIALIZATION_READY, false },
        { true, false, true, PGRAPH_VK_HYBRID_USE_SPECIALIZED,
          PGRAPH_VK_HYBRID_REASON_SPECIALIZATION_READY, false },
        { true, true, false, PGRAPH_VK_HYBRID_USE_SPECIALIZED,
          PGRAPH_VK_HYBRID_REASON_SPECIALIZATION_READY, false },
        { true, true, true, PGRAPH_VK_HYBRID_USE_SPECIALIZED,
          PGRAPH_VK_HYBRID_REASON_SPECIALIZATION_READY, false },
    };
    PGRAPHVkHybridKey desired = key(7);

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        PGRAPHVkHybridEntry entry = cases[i].specialization_ready ?
            (PGRAPHVkHybridEntry) {
                .key = desired,
                .generation = 3,
                .status = PGRAPH_VK_HYBRID_READY,
            } : (PGRAPHVkHybridEntry) { 0 };
        PGRAPHVkHybridDecision decision = choose(
            &entry, desired, 3, 10, cases[i].fallback_pipeline,
            cases[i].fallback_resources, true);

        assert(decision.route == cases[i].route);
        assert(decision.reason == cases[i].reason);
        assert(decision.request_specialization ==
               cases[i].request_specialization);
    }

    PGRAPHVkHybridEntry absent = { 0 };
    PGRAPHVkHybridDecision queue_full =
        choose(&absent, desired, 3, 10, true, true, false);
    assert(queue_full.route == PGRAPH_VK_HYBRID_USE_FALLBACK);
    assert(queue_full.reason == PGRAPH_VK_HYBRID_REASON_QUEUE_FULL);
    assert(!queue_full.request_specialization);
}

static void test_pending_and_failed_entries_use_fallback_with_backoff(void)
{
    PGRAPHVkHybridKey desired = key(9);
    PGRAPHVkHybridEntry entry = { 0 };
    PGRAPHVkHybridDecision decision;

    assert(pgraph_vk_hybrid_mark_pending(&entry, &desired, 4, 91));
    decision = choose(&entry, desired, 4, 100, true, true, true);
    assert(decision.route == PGRAPH_VK_HYBRID_USE_FALLBACK);
    assert(!decision.request_specialization);
    assert(decision.reason == PGRAPH_VK_HYBRID_REASON_SPECIALIZATION_PENDING);

    assert(pgraph_vk_hybrid_mark_failed(&entry, 100, 8));
    assert(entry.status == PGRAPH_VK_HYBRID_FAILED_BACKOFF);
    assert(entry.retry_after_epoch == 108);
    decision = choose(&entry, desired, 4, 107, true, true, true);
    assert(decision.route == PGRAPH_VK_HYBRID_USE_FALLBACK);
    assert(!decision.request_specialization);
    assert(decision.reason == PGRAPH_VK_HYBRID_REASON_FAILURE_BACKOFF);
    decision = choose(&entry, desired, 4, 108, true, true, true);
    assert(decision.route == PGRAPH_VK_HYBRID_USE_FALLBACK);
    assert(decision.request_specialization);
    assert(decision.reason == PGRAPH_VK_HYBRID_REASON_FALLBACK_AND_ENQUEUE);

    PGRAPHVkHybridEntry never_pending = { 0 };
    assert(!pgraph_vk_hybrid_mark_failed(&never_pending, 100, 8));
    assert(never_pending.status == PGRAPH_VK_HYBRID_ABSENT);
    assert(pgraph_vk_hybrid_defer_queue_full(&entry, &desired, 4, 108, 5));
    assert(entry.status == PGRAPH_VK_HYBRID_FAILED_BACKOFF);
    assert(entry.retry_after_epoch == 113);
    decision = choose(&entry, desired, 4, 112, true, true, true);
    assert(!decision.request_specialization);
    assert(decision.reason == PGRAPH_VK_HYBRID_REASON_FAILURE_BACKOFF);
}

static void test_first_queue_full_defers_the_exact_absent_request(void)
{
    PGRAPHVkHybridKey desired = key(10);
    PGRAPHVkHybridEntry entry = { 0 };
    PGRAPHVkHybridDecision decision;

    decision = choose(&entry, desired, 12, 40, true, true, false);
    assert(decision.route == PGRAPH_VK_HYBRID_USE_FALLBACK);
    assert(decision.reason == PGRAPH_VK_HYBRID_REASON_QUEUE_FULL);
    assert(!decision.request_specialization);
    assert(pgraph_vk_hybrid_defer_queue_full(&entry, &desired, 12, 40, 3));
    assert(entry.status == PGRAPH_VK_HYBRID_FAILED_BACKOFF);
    assert(entry.generation == 12);
    assert(entry.ticket == 0);
    assert(entry.retry_after_epoch == 43);
    assert(!memcmp(&entry.key, &desired, sizeof(desired)));

    decision = choose(&entry, desired, 12, 42, true, true, true);
    assert(decision.route == PGRAPH_VK_HYBRID_USE_FALLBACK);
    assert(decision.reason == PGRAPH_VK_HYBRID_REASON_FAILURE_BACKOFF);
    assert(!decision.request_specialization);
    decision = choose(&entry, desired, 12, 43, true, true, true);
    assert(decision.reason == PGRAPH_VK_HYBRID_REASON_FALLBACK_AND_ENQUEUE);
    assert(decision.request_specialization);
}

static void test_zero_ticket_never_marks_an_entry_pending(void)
{
    PGRAPHVkHybridKey desired = key(11);
    PGRAPHVkHybridEntry entry = { 0 };

    assert(!pgraph_vk_hybrid_mark_pending(&entry, &desired, 5, 0));
    assert(entry.status == PGRAPH_VK_HYBRID_ABSENT);
    assert(pgraph_vk_hybrid_mark_pending(&entry, &desired, 5, 1));
    assert(entry.status == PGRAPH_VK_HYBRID_PENDING);
    assert(entry.ticket == 1);
}

static PGRAPHVkHybridCompletion complete_for(
    const PGRAPHVkHybridEntry *entry)
{
    return (PGRAPHVkHybridCompletion) {
        .key = entry->key,
        .generation = entry->generation,
        .ticket = entry->ticket,
        .artifact_parts = PGRAPH_VK_HYBRID_ARTIFACT_COMPLETE,
    };
}

static void test_publication_requires_complete_exact_current_identity(void)
{
    PGRAPHVkHybridKey desired = key(13);
    PGRAPHVkHybridEntry entry;
    PGRAPHVkHybridEntry original;
    PGRAPHVkHybridCompletion completion;
    uint64_t selection_epoch;

    assert(pgraph_vk_hybrid_mark_pending(&entry, &desired, 6, 44));
    original = entry;
    completion = complete_for(&entry);

    completion.artifact_parts &= ~PGRAPH_VK_HYBRID_ARTIFACT_PIPELINE;
    selection_epoch = 20;
    assert(pgraph_vk_hybrid_publish(&entry, &completion, 6,
                                    &selection_epoch) ==
           PGRAPH_VK_HYBRID_PUBLISH_INCOMPLETE);
    assert(pgraph_vk_hybrid_entry_equal(&entry, &original));
    assert(selection_epoch == 20);

    completion = complete_for(&entry);
    completion.key.words[3] ^= 1;
    assert(pgraph_vk_hybrid_publish(&entry, &completion, 6,
                                    &selection_epoch) ==
           PGRAPH_VK_HYBRID_PUBLISH_KEY_MISMATCH);
    assert(pgraph_vk_hybrid_entry_equal(&entry, &original));

    completion = complete_for(&entry);
    completion.generation++;
    assert(pgraph_vk_hybrid_publish(&entry, &completion, 6,
                                    &selection_epoch) ==
           PGRAPH_VK_HYBRID_PUBLISH_GENERATION_MISMATCH);
    completion = complete_for(&entry);
    assert(pgraph_vk_hybrid_publish(&entry, &completion, 7,
                                    &selection_epoch) ==
           PGRAPH_VK_HYBRID_PUBLISH_GENERATION_MISMATCH);

    completion = complete_for(&entry);
    completion.ticket = 0;
    assert(pgraph_vk_hybrid_publish(&entry, &completion, 6,
                                    &selection_epoch) ==
           PGRAPH_VK_HYBRID_PUBLISH_INVALID_TICKET);
    completion = complete_for(&entry);
    completion.ticket++;
    assert(pgraph_vk_hybrid_publish(&entry, &completion, 6,
                                    &selection_epoch) ==
           PGRAPH_VK_HYBRID_PUBLISH_TICKET_MISMATCH);

    completion = complete_for(&entry);
    assert(pgraph_vk_hybrid_publish(&entry, &completion, 6,
                                    &selection_epoch) ==
           PGRAPH_VK_HYBRID_PUBLISH_ACCEPTED);
    assert(entry.status == PGRAPH_VK_HYBRID_READY);
    assert(selection_epoch == 21);
    assert(pgraph_vk_hybrid_publish(&entry, &completion, 6,
                                    &selection_epoch) ==
           PGRAPH_VK_HYBRID_PUBLISH_NOT_PENDING);
    assert(selection_epoch == 21);
}

static void test_ready_identity_and_selection_epoch_control_handoff(void)
{
    PGRAPHVkHybridKey desired = key(15);
    PGRAPHVkHybridEntry entry;
    PGRAPHVkHybridCompletion completion;
    PGRAPHVkHybridDecision decision;
    uint64_t selection_epoch = UINT64_MAX;

    assert(pgraph_vk_hybrid_mark_pending(&entry, &desired, 8, 72));
    completion = complete_for(&entry);
    assert(pgraph_vk_hybrid_publish(&entry, &completion, 8,
                                    &selection_epoch) ==
           PGRAPH_VK_HYBRID_PUBLISH_ACCEPTED);
    assert(selection_epoch == 1);
    assert(pgraph_vk_hybrid_selection_changed(0, selection_epoch));
    assert(!pgraph_vk_hybrid_selection_changed(selection_epoch,
                                                selection_epoch));

    decision = choose(&entry, desired, 8, 50, true, true, true);
    assert(decision.route == PGRAPH_VK_HYBRID_USE_SPECIALIZED);
    assert(!decision.request_specialization);
    assert(decision.reason == PGRAPH_VK_HYBRID_REASON_SPECIALIZATION_READY);

    decision = choose(&entry, key(16), 8, 50, true, true, true);
    assert(decision.route == PGRAPH_VK_HYBRID_USE_FALLBACK);
    assert(decision.request_specialization);
    decision = choose(&entry, desired, 9, 50, true, true, true);
    assert(decision.route == PGRAPH_VK_HYBRID_USE_FALLBACK);
    assert(decision.request_specialization);
}

int main(void)
{
    test_route_selection_and_queue_full_behavior();
    test_pending_and_failed_entries_use_fallback_with_backoff();
    test_first_queue_full_defers_the_exact_absent_request();
    test_zero_ticket_never_marks_an_entry_pending();
    test_publication_requires_complete_exact_current_identity();
    test_ready_identity_and_selection_epoch_control_handoff();
    puts("hybrid policy tests passed");
    return 0;
}

/*
 * Geforce NV2A PGRAPH Vulkan hybrid specialization policy
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "hw/xbox/nv2a/pgraph/vk/hybrid-policy.h"

#include <string.h>

static bool key_equal(const PGRAPHVkHybridKey *a,
                      const PGRAPHVkHybridKey *b)
{
    return !memcmp(a, b, sizeof(*a));
}

static bool entry_matches(const PGRAPHVkHybridEntry *entry,
                          const PGRAPHVkHybridRequest *request)
{
    return entry && entry->status != PGRAPH_VK_HYBRID_ABSENT &&
           entry->generation == request->generation &&
           key_equal(&entry->key, &request->key);
}

PGRAPHVkHybridDecision pgraph_vk_hybrid_choose(
    const PGRAPHVkHybridEntry *entry, const PGRAPHVkHybridRequest *request)
{
    bool matches = entry_matches(entry, request);
    bool can_retry =
        !matches || entry->status == PGRAPH_VK_HYBRID_ABSENT ||
        (entry->status == PGRAPH_VK_HYBRID_FAILED_BACKOFF &&
         request->epoch >= entry->retry_after_epoch);

    if (matches && entry->status == PGRAPH_VK_HYBRID_READY) {
        return (PGRAPHVkHybridDecision) {
            .route = PGRAPH_VK_HYBRID_USE_SPECIALIZED,
            .reason = PGRAPH_VK_HYBRID_REASON_SPECIALIZATION_READY,
        };
    }
    if (!request->fallback_pipeline_ready) {
        return (PGRAPHVkHybridDecision) {
            .route = PGRAPH_VK_HYBRID_USE_SYNCHRONOUS,
            .reason = PGRAPH_VK_HYBRID_REASON_NO_FALLBACK_PIPELINE,
        };
    }
    if (!request->fallback_draw_resources_ready) {
        return (PGRAPHVkHybridDecision) {
            .route = PGRAPH_VK_HYBRID_USE_SYNCHRONOUS,
            .reason = PGRAPH_VK_HYBRID_REASON_NO_DRAW_RESOURCES,
        };
    }
    if (matches && entry->status == PGRAPH_VK_HYBRID_PENDING) {
        return (PGRAPHVkHybridDecision) {
            .route = PGRAPH_VK_HYBRID_USE_FALLBACK,
            .reason = PGRAPH_VK_HYBRID_REASON_SPECIALIZATION_PENDING,
        };
    }
    if (!can_retry) {
        return (PGRAPHVkHybridDecision) {
            .route = PGRAPH_VK_HYBRID_USE_FALLBACK,
            .reason = PGRAPH_VK_HYBRID_REASON_FAILURE_BACKOFF,
        };
    }
    if (!request->queue_has_capacity) {
        return (PGRAPHVkHybridDecision) {
            .route = PGRAPH_VK_HYBRID_USE_FALLBACK,
            .reason = PGRAPH_VK_HYBRID_REASON_QUEUE_FULL,
        };
    }
    return (PGRAPHVkHybridDecision) {
        .route = PGRAPH_VK_HYBRID_USE_FALLBACK,
        .reason = PGRAPH_VK_HYBRID_REASON_FALLBACK_AND_ENQUEUE,
        .request_specialization = true,
    };
}

bool pgraph_vk_hybrid_mark_pending(PGRAPHVkHybridEntry *entry,
                                   const PGRAPHVkHybridKey *key,
                                   uint64_t generation, uint64_t ticket)
{
    if (!entry || !key || ticket == 0) {
        return false;
    }
    *entry = (PGRAPHVkHybridEntry) {
        .key = *key,
        .generation = generation,
        .ticket = ticket,
        .status = PGRAPH_VK_HYBRID_PENDING,
    };
    return true;
}

static uint64_t retry_epoch(uint64_t epoch, uint64_t backoff)
{
    return UINT64_MAX - epoch < backoff ? UINT64_MAX : epoch + backoff;
}

bool pgraph_vk_hybrid_mark_failed(PGRAPHVkHybridEntry *entry,
                                  uint64_t epoch, uint64_t backoff)
{
    if (!entry || entry->status != PGRAPH_VK_HYBRID_PENDING) {
        return false;
    }
    entry->status = PGRAPH_VK_HYBRID_FAILED_BACKOFF;
    entry->retry_after_epoch = retry_epoch(epoch, backoff);
    entry->ticket = 0;
    return true;
}

bool pgraph_vk_hybrid_defer_queue_full(PGRAPHVkHybridEntry *entry,
                                       const PGRAPHVkHybridKey *key,
                                       uint64_t generation, uint64_t epoch,
                                       uint64_t backoff)
{
    if (!entry || !key) {
        return false;
    }
    *entry = (PGRAPHVkHybridEntry) {
        .key = *key,
        .generation = generation,
        .retry_after_epoch = retry_epoch(epoch, backoff),
        .status = PGRAPH_VK_HYBRID_FAILED_BACKOFF,
    };
    return true;
}

PGRAPHVkHybridPublishResult pgraph_vk_hybrid_publish(
    PGRAPHVkHybridEntry *entry, const PGRAPHVkHybridCompletion *completion,
    uint64_t current_generation, uint64_t *selection_epoch)
{
    if (!entry || !completion || !selection_epoch ||
        entry->status != PGRAPH_VK_HYBRID_PENDING) {
        return PGRAPH_VK_HYBRID_PUBLISH_NOT_PENDING;
    }
    if (completion->artifact_parts != PGRAPH_VK_HYBRID_ARTIFACT_COMPLETE) {
        return PGRAPH_VK_HYBRID_PUBLISH_INCOMPLETE;
    }
    if (!key_equal(&entry->key, &completion->key)) {
        return PGRAPH_VK_HYBRID_PUBLISH_KEY_MISMATCH;
    }
    if (entry->generation != current_generation ||
        completion->generation != current_generation) {
        return PGRAPH_VK_HYBRID_PUBLISH_GENERATION_MISMATCH;
    }
    if (completion->ticket == 0 || entry->ticket == 0) {
        return PGRAPH_VK_HYBRID_PUBLISH_INVALID_TICKET;
    }
    if (entry->ticket != completion->ticket) {
        return PGRAPH_VK_HYBRID_PUBLISH_TICKET_MISMATCH;
    }

    entry->status = PGRAPH_VK_HYBRID_READY;
    entry->retry_after_epoch = 0;
    (*selection_epoch)++;
    if (*selection_epoch == 0) {
        *selection_epoch = 1;
    }
    return PGRAPH_VK_HYBRID_PUBLISH_ACCEPTED;
}

bool pgraph_vk_hybrid_selection_changed(uint64_t bound_epoch,
                                        uint64_t selection_epoch)
{
    return bound_epoch != selection_epoch;
}

bool pgraph_vk_hybrid_entry_equal(const PGRAPHVkHybridEntry *a,
                                  const PGRAPHVkHybridEntry *b)
{
    return a && b && key_equal(&a->key, &b->key) &&
           a->generation == b->generation && a->ticket == b->ticket &&
           a->retry_after_epoch == b->retry_after_epoch &&
           a->status == b->status;
}

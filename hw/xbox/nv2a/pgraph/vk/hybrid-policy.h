/*
 * Geforce NV2A PGRAPH Vulkan hybrid specialization policy
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_HYBRID_POLICY_H
#define HW_XBOX_NV2A_PGRAPH_VK_HYBRID_POLICY_H

#include <stdbool.h>
#include <stdint.h>

#define PGRAPH_VK_HYBRID_KEY_WORDS 4U

typedef struct PGRAPHVkHybridKey {
    uint64_t words[PGRAPH_VK_HYBRID_KEY_WORDS];
} PGRAPHVkHybridKey;

typedef enum PGRAPHVkHybridStatus {
    PGRAPH_VK_HYBRID_ABSENT,
    PGRAPH_VK_HYBRID_PENDING,
    PGRAPH_VK_HYBRID_READY,
    PGRAPH_VK_HYBRID_FAILED_BACKOFF,
} PGRAPHVkHybridStatus;

typedef enum PGRAPHVkHybridRoute {
    PGRAPH_VK_HYBRID_USE_SPECIALIZED,
    PGRAPH_VK_HYBRID_USE_FALLBACK,
    PGRAPH_VK_HYBRID_USE_SYNCHRONOUS,
} PGRAPHVkHybridRoute;

typedef enum PGRAPHVkHybridReason {
    PGRAPH_VK_HYBRID_REASON_SPECIALIZATION_READY,
    PGRAPH_VK_HYBRID_REASON_FALLBACK_AND_ENQUEUE,
    PGRAPH_VK_HYBRID_REASON_SPECIALIZATION_PENDING,
    PGRAPH_VK_HYBRID_REASON_FAILURE_BACKOFF,
    PGRAPH_VK_HYBRID_REASON_QUEUE_FULL,
    PGRAPH_VK_HYBRID_REASON_NO_FALLBACK_PIPELINE,
    PGRAPH_VK_HYBRID_REASON_NO_DRAW_RESOURCES,
} PGRAPHVkHybridReason;

typedef struct PGRAPHVkHybridEntry {
    PGRAPHVkHybridKey key;
    uint64_t generation;
    uint64_t ticket;
    uint64_t retry_after_epoch;
    PGRAPHVkHybridStatus status;
} PGRAPHVkHybridEntry;

typedef struct PGRAPHVkHybridRequest {
    PGRAPHVkHybridKey key;
    uint64_t generation;
    uint64_t epoch;
    bool fallback_pipeline_ready;
    bool fallback_draw_resources_ready;
    bool queue_has_capacity;
} PGRAPHVkHybridRequest;

typedef struct PGRAPHVkHybridDecision {
    PGRAPHVkHybridRoute route;
    PGRAPHVkHybridReason reason;
    bool request_specialization;
} PGRAPHVkHybridDecision;

enum {
    PGRAPH_VK_HYBRID_ARTIFACT_SHADERS = 1U << 0,
    PGRAPH_VK_HYBRID_ARTIFACT_REFLECTION = 1U << 1,
    PGRAPH_VK_HYBRID_ARTIFACT_LAYOUT = 1U << 2,
    PGRAPH_VK_HYBRID_ARTIFACT_PIPELINE = 1U << 3,
    PGRAPH_VK_HYBRID_ARTIFACT_COMPLETE =
        PGRAPH_VK_HYBRID_ARTIFACT_SHADERS |
        PGRAPH_VK_HYBRID_ARTIFACT_REFLECTION |
        PGRAPH_VK_HYBRID_ARTIFACT_LAYOUT |
        PGRAPH_VK_HYBRID_ARTIFACT_PIPELINE,
};

typedef struct PGRAPHVkHybridCompletion {
    PGRAPHVkHybridKey key;
    uint64_t generation;
    uint64_t ticket;
    uint32_t artifact_parts;
} PGRAPHVkHybridCompletion;

typedef enum PGRAPHVkHybridPublishResult {
    PGRAPH_VK_HYBRID_PUBLISH_ACCEPTED,
    PGRAPH_VK_HYBRID_PUBLISH_NOT_PENDING,
    PGRAPH_VK_HYBRID_PUBLISH_INCOMPLETE,
    PGRAPH_VK_HYBRID_PUBLISH_KEY_MISMATCH,
    PGRAPH_VK_HYBRID_PUBLISH_GENERATION_MISMATCH,
    PGRAPH_VK_HYBRID_PUBLISH_INVALID_TICKET,
    PGRAPH_VK_HYBRID_PUBLISH_TICKET_MISMATCH,
} PGRAPHVkHybridPublishResult;

PGRAPHVkHybridDecision pgraph_vk_hybrid_choose(
    const PGRAPHVkHybridEntry *entry, const PGRAPHVkHybridRequest *request);
bool pgraph_vk_hybrid_mark_pending(PGRAPHVkHybridEntry *entry,
                                   const PGRAPHVkHybridKey *key,
                                   uint64_t generation, uint64_t ticket);
bool pgraph_vk_hybrid_mark_failed(PGRAPHVkHybridEntry *entry,
                                  uint64_t epoch, uint64_t backoff);
bool pgraph_vk_hybrid_defer_queue_full(PGRAPHVkHybridEntry *entry,
                                       const PGRAPHVkHybridKey *key,
                                       uint64_t generation, uint64_t epoch,
                                       uint64_t backoff);
PGRAPHVkHybridPublishResult pgraph_vk_hybrid_publish(
    PGRAPHVkHybridEntry *entry, const PGRAPHVkHybridCompletion *completion,
    uint64_t current_generation, uint64_t *selection_epoch);
bool pgraph_vk_hybrid_selection_changed(uint64_t bound_epoch,
                                        uint64_t selection_epoch);
bool pgraph_vk_hybrid_entry_equal(const PGRAPHVkHybridEntry *a,
                                  const PGRAPHVkHybridEntry *b);

#endif

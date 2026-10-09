/*
 * NV2A explicit idle-read completion coordination
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef HW_XBOX_NV2A_PGRAPH_IDLE_COMPLETION_H
#define HW_XBOX_NV2A_PGRAPH_IDLE_COMPLETION_H

#include "qemu/thread.h"

typedef struct PGRAPHIdleCompletion {
    QemuEvent complete;
    uint64_t requested;
    uint64_t acknowledged;
    uint64_t frontier;
    bool pending;
    bool succeeded;
    bool cancelled;
} PGRAPHIdleCompletion;

typedef struct PGRAPHIdleCompletionCoordinator {
    QemuMutex *pfifo_lock;
    QemuMutex *pgraph_lock;
    PGRAPHIdleCompletion *completion;
} PGRAPHIdleCompletionCoordinator;

typedef struct PGRAPHIdleCompletionOps {
    /* Metadata callbacks run with PFIFO then PGRAPH held. */
    bool (*eligible)(void *opaque);
    /* Accepted requests may drain before a renderer handoff starts. */
    bool (*serviceable)(void *opaque);
    bool (*needed)(void *opaque);
    uint64_t (*frontier)(void *opaque);
    void (*kick)(void *opaque);
    void (*release_bql)(void *opaque);
    void (*acquire_bql)(void *opaque);
    /* Owner callback runs with PGRAPH, without PFIFO or BQL. */
    bool (*materialize)(void *opaque);
} PGRAPHIdleCompletionOps;

typedef enum PGRAPHIdleCompletionResult {
    PGRAPH_IDLE_SKIPPED,
    PGRAPH_IDLE_COMPLETE,
    PGRAPH_IDLE_CANCELLED,
    PGRAPH_IDLE_FAILED,
} PGRAPHIdleCompletionResult;

void pgraph_idle_completion_init(PGRAPHIdleCompletion *completion);
void pgraph_idle_completion_destroy(PGRAPHIdleCompletion *completion);
/* Cancel under PGRAPH; reset and renderer teardown must not leave a waiter. */
void pgraph_idle_completion_cancel(PGRAPHIdleCompletion *completion);
/* vCPU enters/returns with BQL, never with either device lock. */
PGRAPHIdleCompletionResult
pgraph_idle_completion_wait(PGRAPHIdleCompletionCoordinator *coordinator,
                            const PGRAPHIdleCompletionOps *ops, void *opaque);
/* Renderer owner enters/returns with PFIFO, never with PGRAPH or BQL. */
void pgraph_idle_completion_process(
    PGRAPHIdleCompletionCoordinator *coordinator,
    const PGRAPHIdleCompletionOps *ops, void *opaque);

#endif

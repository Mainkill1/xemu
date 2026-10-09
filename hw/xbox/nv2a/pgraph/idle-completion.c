/*
 * NV2A explicit idle-read completion coordination
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "qemu/osdep.h"
#include "qemu/atomic.h"

#include "idle-completion.h"

void pgraph_idle_completion_init(PGRAPHIdleCompletion *completion)
{
    memset(completion, 0, sizeof(*completion));
    qemu_event_init(&completion->complete, false);
}

void pgraph_idle_completion_destroy(PGRAPHIdleCompletion *completion)
{
    assert(!qatomic_read(&completion->pending));
    qemu_event_destroy(&completion->complete);
}

void pgraph_idle_completion_cancel(PGRAPHIdleCompletion *completion)
{
    if (qatomic_read(&completion->pending)) {
        completion->acknowledged = completion->requested;
        completion->succeeded = false;
        completion->cancelled = true;
        qatomic_set(&completion->pending, false);
        qemu_event_set(&completion->complete);
    }
}

PGRAPHIdleCompletionResult
pgraph_idle_completion_wait(PGRAPHIdleCompletionCoordinator *coordinator,
                            const PGRAPHIdleCompletionOps *ops, void *opaque)
{
    PGRAPHIdleCompletion *completion = coordinator->completion;
    PGRAPHIdleCompletionResult result = PGRAPH_IDLE_SKIPPED;
    uint64_t request = 0;

    ops->release_bql(opaque);
    qemu_mutex_lock(coordinator->pfifo_lock);
    qemu_mutex_lock(coordinator->pgraph_lock);
    if (!qatomic_read(&completion->pending) && ops->eligible(opaque) &&
        ops->needed(opaque)) {
        assert(completion->requested != UINT64_MAX);
        request = ++completion->requested;
        completion->frontier = ops->frontier(opaque);
        completion->succeeded = false;
        completion->cancelled = false;
        qemu_event_reset(&completion->complete);
        qatomic_set(&completion->pending, true);
    }
    qemu_mutex_unlock(coordinator->pgraph_lock);
    if (request) {
        ops->kick(opaque);
    }
    qemu_mutex_unlock(coordinator->pfifo_lock);

    if (request) {
        qemu_event_wait(&completion->complete);
        qemu_mutex_lock(coordinator->pgraph_lock);
        if (completion->acknowledged != request) {
            result = PGRAPH_IDLE_FAILED;
        } else if (completion->cancelled) {
            result = PGRAPH_IDLE_CANCELLED;
        } else {
            result = completion->succeeded ? PGRAPH_IDLE_COMPLETE :
                                             PGRAPH_IDLE_FAILED;
        }
        qemu_mutex_unlock(coordinator->pgraph_lock);
    }
    ops->acquire_bql(opaque);
    return result;
}

void pgraph_idle_completion_process(
    PGRAPHIdleCompletionCoordinator *coordinator,
    const PGRAPHIdleCompletionOps *ops, void *opaque)
{
    PGRAPHIdleCompletion *completion = coordinator->completion;

    if (!qatomic_read(&completion->pending)) {
        return;
    }
    qemu_mutex_lock(coordinator->pgraph_lock);
    if (!qatomic_read(&completion->pending)) {
        qemu_mutex_unlock(coordinator->pgraph_lock);
        return;
    }
    if (!ops->serviceable(opaque) ||
        completion->frontier != ops->frontier(opaque)) {
        pgraph_idle_completion_cancel(completion);
        qemu_mutex_unlock(coordinator->pgraph_lock);
        return;
    }

    /*
     * Consume only the captured, already-drained frontier. No guest progress
     * or future FIFO work can be required by this owner callback.
     */
    qemu_mutex_unlock(coordinator->pfifo_lock);
    completion->succeeded = ops->materialize(opaque);
    completion->acknowledged = completion->requested;
    qatomic_set(&completion->pending, false);
    qemu_event_set(&completion->complete);
    qemu_mutex_unlock(coordinator->pgraph_lock);
    qemu_mutex_lock(coordinator->pfifo_lock);
}

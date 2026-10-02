/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
/* Compile the production static stall path, without a test-only device API. */
#include "hw/xbox/nv2a/pfifo.c"

typedef struct StallCall {
    NV2AState *d;
    GMutex mutex;
    GCond condition;
    bool entered;
    bool completed;
    bool stalled;
} StallCall;

static void *stall_call(void *opaque)
{
    StallCall *call = opaque;
    qemu_mutex_lock(&call->d->pfifo.lock);
    g_mutex_lock(&call->mutex);
    call->entered = true;
    g_cond_broadcast(&call->condition);
    g_mutex_unlock(&call->mutex);
    call->stalled = pfifo_stall_for_flip(call->d);
    qemu_mutex_unlock(&call->d->pfifo.lock);
    g_mutex_lock(&call->mutex);
    call->completed = true;
    g_cond_broadcast(&call->condition);
    g_mutex_unlock(&call->mutex);
    return NULL;
}

static void stall_while_renderer_busy(bool complete)
{
    NV2AState *d = g_new0(NV2AState, 1);
    qemu_mutex_init(&d->pgraph.lock);
    qemu_mutex_init(&d->pfifo.lock);
    d->pgraph.waiting_for_flip = true;
    d->pgraph.regs_[NV_PGRAPH_SURFACE] = (3U << 28) | (complete ? 1U << 24 : 0);
    StallCall call = { .d = d };
    g_mutex_init(&call.mutex);
    g_cond_init(&call.condition);
    qemu_mutex_lock(&d->pgraph.lock);
    QemuThread thread;
    qemu_thread_create(&thread, "pfifo-flip", stall_call, &call,
                       QEMU_THREAD_JOINABLE);
    g_mutex_lock(&call.mutex);
    while (!call.entered) {
        g_cond_wait(&call.condition, &call.mutex);
    }
    int64_t deadline = g_get_monotonic_time() + G_USEC_PER_SEC;
    while (!call.completed &&
           g_cond_wait_until(&call.condition, &call.mutex, deadline)) {
    }
    bool completed_before_release = call.completed;
    g_mutex_unlock(&call.mutex);
    qemu_mutex_unlock(&d->pgraph.lock);
    qemu_thread_join(&thread);
    g_assert_true(completed_before_release);
    g_assert_cmpint(call.stalled, ==, !complete);
    g_assert_cmpint(d->pgraph.waiting_for_flip, ==, !complete);
    g_cond_clear(&call.condition);
    g_mutex_clear(&call.mutex);
    qemu_mutex_destroy(&d->pfifo.lock);
    qemu_mutex_destroy(&d->pgraph.lock);
    g_free(d);
}

static void pending(void)
{
    stall_while_renderer_busy(false);
}

static void complete(void)
{
    stall_while_renderer_busy(true);
}

static void count_stop(void *opaque)
{
    unsigned int *calls = opaque;
    (*calls)++;
}

static void relative_probe_boundary(void)
{
    NV2AState *d = g_new0(NV2AState, 1);
    AioContext *ctx = aio_context_new(&error_abort);
    unsigned int stops = 0;

    d->flip_probe.enabled = true;
    d->flip_probe.stop_bh = aio_bh_new(ctx, count_stop, &stops);
    d->flip_probe.counters.guest_read3d = 10;
    g_assert_true(nv2a_flip_probe_arm(&d->flip_probe.gate, 10, 0, 2));
    d->pgraph.regs_[NV_PGRAPH_SURFACE] = (3U << 28) | (1U << 24);
    d->pgraph.waiting_for_flip = true;
    g_assert_false(pfifo_stall_for_flip(d));
    nv2a_flip_probe_read3d(d);
    d->pgraph.waiting_for_flip = true;
    g_assert_false(pfifo_stall_for_flip(d));
    nv2a_flip_probe_read3d(d);
    /* The count alone does not hold PFIFO until a stall completes. */
    g_assert_false(pfifo_stall_for_flip(d));
    d->pgraph.waiting_for_flip = true;
    g_assert_true(pfifo_stall_for_flip(d));
    g_assert_false(d->pgraph.waiting_for_flip);
    g_assert_true(d->flip_probe.has_quiesce);
    g_assert_cmpuint(d->flip_probe.quiesce.guest_read3d, ==, 12);
    g_assert_cmpuint(d->flip_probe.quiesce.completed_flip_stall, ==, 3);
    g_assert_cmpuint(stops, ==, 0);
    aio_bh_poll(ctx);
    g_assert_cmpuint(stops, ==, 1);
    g_assert_true(pfifo_stall_for_flip(d));
    g_assert_cmpuint(d->flip_probe.counters.completed_flip_stall, ==, 3);
    nv2a_flip_probe_resume_locked(d);
    g_assert_false(pfifo_stall_for_flip(d));

    g_assert_true(nv2a_flip_probe_arm(&d->flip_probe.gate, 12, 3, 1));
    nv2a_flip_probe_read3d(d);
    d->pgraph.waiting_for_flip = true;
    g_assert_true(pfifo_stall_for_flip(d));
    nv2a_flip_probe_cancel_locked(d, "test-reset", true);
    aio_bh_poll(ctx);
    g_assert_cmpuint(stops, ==, 1);
    g_assert_false(pfifo_stall_for_flip(d));
    g_assert_false(d->flip_probe.has_quiesce);
    g_assert_cmpuint(d->flip_probe.epoch, ==, 1);
    qemu_bh_delete(d->flip_probe.stop_bh);
    aio_bh_poll(ctx);
    aio_context_unref(ctx);
    g_free(d);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/nv2a/pfifo/flip/pending-renderer-busy", pending);
    g_test_add_func("/nv2a/pfifo/flip/complete-renderer-busy", complete);
    g_test_add_func("/nv2a/pfifo/flip/relative-probe-boundary",
                    relative_probe_boundary);
    return g_test_run();
}

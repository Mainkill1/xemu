/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
/* Exercise the real callback; only the VM-stop boundary is substituted. */
#include "hw/xbox/nv2a/flip_probe.c"

NV2AState *g_nv2a;
static RunState test_state;
static unsigned int stop_action;

bool bql_locked(void)
{
    return true;
}

void bql_lock_impl(const char *file, int line)
{
}
void bql_unlock(void)
{
}
void bql_update_status(bool locked)
{
}
void bql_block_unlock(bool increase)
{
}
bool mutex_is_bql(QemuMutex *mutex)
{
    return false;
}
void pfifo_kick(NV2AState *d)
{
}

bool runstate_check(RunState state)
{
    return test_state == state;
}

bool runstate_is_running(void)
{
    return runstate_check(RUN_STATE_RUNNING);
}

int vm_stop(RunState state)
{
    test_state = state;
    qemu_mutex_lock(&g_nv2a->pfifo.lock);
    if (stop_action == 1 || stop_action == 3) {
        nv2a_flip_probe_cancel_locked(g_nv2a, "nested-disarm", false);
    }
    if (stop_action == 2) {
        nv2a_flip_probe_resume_locked(g_nv2a);
    }
    if (stop_action >= 2) {
        test_state = RUN_STATE_RUNNING;
    }
    if (stop_action == 3) {
        g_assert_true(nv2a_flip_probe_arm(&g_nv2a->flip_probe.gate, 1, 1, 1));
    }
    qemu_mutex_unlock(&g_nv2a->pfifo.lock);
    return 0;
}

static void check_stop(void)
{
    AioContext *ctx = aio_context_new(&error_abort);
    for (stop_action = 0; stop_action < 5; stop_action++) {
        NV2AState *d = g_new0(NV2AState, 1);
        g_nv2a = d;
        test_state = RUN_STATE_RUNNING;
        qemu_mutex_init(&d->pfifo.lock);
        d->flip_probe.enabled = true;
        d->flip_probe.stop_bh = aio_bh_new(ctx, stop_on_main_loop, d);
        g_assert_true(nv2a_flip_probe_arm(&d->flip_probe.gate, 0, 0, 1));
        nv2a_flip_probe_read3d(d);
        g_assert_true(nv2a_flip_probe_complete_stall(d));
        aio_bh_poll(ctx);
        g_assert_cmpuint(d->flip_probe.paused_generation, ==,
                         stop_action == 0 ? 1 : 0);
        g_assert_cmpint(d->flip_probe.has_pause, ==, stop_action == 0);
        if (stop_action == 3) {
            g_assert_cmpuint(d->flip_probe.gate.generation, ==, 2);
            g_assert_cmpint(d->flip_probe.gate.phase, ==,
                            NV2A_FLIP_PROBE_ARMED);
        }
        qemu_bh_delete(d->flip_probe.stop_bh);
        aio_bh_poll(ctx);
        qemu_mutex_destroy(&d->pfifo.lock);
        g_free(d);
        g_nv2a = NULL;
    }
    aio_context_unref(ctx);
}

static void check_save(void)
{
    AioContext *ctx = aio_context_new(&error_abort);
    NV2AState *d = g_new0(NV2AState, 1);
    Error *err = NULL;

    g_nv2a = d;
    qemu_mutex_init(&d->pfifo.lock);
    d->flip_probe.enabled = true;
    d->flip_probe.stop_bh = aio_bh_new(ctx, stop_on_main_loop, d);
    test_state = RUN_STATE_RUNNING;
    g_assert_true(nv2a_flip_probe_arm(&d->flip_probe.gate, 0, 0, 1));
    g_assert_true(nv2a_flip_probe_before_save(&err));
    g_assert_null(err);
    g_assert_cmpint(d->flip_probe.gate.phase, ==, NV2A_FLIP_PROBE_CANCELLED);

    g_assert_true(nv2a_flip_probe_arm(&d->flip_probe.gate, 0, 0, 1));
    nv2a_flip_probe_read3d(d);
    g_assert_true(nv2a_flip_probe_complete_stall(d));
    stop_action = 0;
    aio_bh_poll(ctx);
    g_assert_cmpuint(d->flip_probe.paused_generation, ==, 2);
    g_assert_false(nv2a_flip_probe_before_save(&err));
    g_assert_nonnull(err);
    g_assert_nonnull(strstr(error_get_pretty(err), "resume the VM"));
    g_assert_cmpuint(d->flip_probe.paused_generation, ==, 0);
    g_assert_cmpint(d->flip_probe.gate.phase, ==, NV2A_FLIP_PROBE_CANCELLED);
    g_assert_true(runstate_check(RUN_STATE_PAUSED));
    error_free(err);
    err = NULL;
    /* Retrying or disarming must not bypass the paused-save guard. */
    nv2a_flip_probe_cancel_locked(d, "disarm", false);
    g_assert_false(nv2a_flip_probe_before_save(&err));
    g_assert_nonnull(err);
    error_free(err);
    err = NULL;
    test_state = RUN_STATE_RUNNING;
    nv2a_flip_probe_resume_locked(d);
    g_assert_true(nv2a_flip_probe_before_save(&err));
    g_assert_null(err);
    test_state = RUN_STATE_PAUSED;
    d->flip_probe.enabled = false;
    g_assert_true(nv2a_flip_probe_before_save(&err));
    g_assert_null(err);
    qemu_bh_delete(d->flip_probe.stop_bh);
    aio_bh_poll(ctx);
    aio_context_unref(ctx);
    qemu_mutex_destroy(&d->pfifo.lock);
    g_free(d);
    g_nv2a = NULL;
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/nv2a/flip-probe/stop-reentrancy", check_stop);
    g_test_add_func("/nv2a/flip-probe/save-cancellation", check_save);
    return g_test_run();
}

/*
 * Experimental NV2A relative-flip diagnostic.
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "hw/xbox/nv2a/nv2a_int.h"
#include "qapi/qapi-commands-xemu-flip-probe.h"

static NV2AFlipProbeCounters snapshot(NV2AFlipProbe *probe)
{
    return (NV2AFlipProbeCounters) {
        .guest_read3d = qatomic_read(&probe->counters.guest_read3d),
        .completed_flip_stall =
            qatomic_read(&probe->counters.completed_flip_stall),
        .vblank = qatomic_read(&probe->counters.vblank),
        .host_present = qatomic_read(&probe->counters.host_present),
        .timestamp_us = qemu_clock_get_us(QEMU_CLOCK_REALTIME),
    };
}

static XemuFlipCounters *export_counters(NV2AFlipProbeCounters values)
{
    XemuFlipCounters *result = g_new0(XemuFlipCounters, 1);
    result->guest_read3d = values.guest_read3d;
    result->completed_flip_stall = values.completed_flip_stall;
    result->vblank = values.vblank;
    result->host_present = values.host_present;
    result->timestamp_us = values.timestamp_us;
    return result;
}

/*
 * Caller owns pfifo.lock; READ_3D and completed-stall counters are stable.
 * The refresh and presentation counters are independently sampled.
 */
static XemuFlipProbeStatus *status_locked(NV2AState *d)
{
    static const char *const phases[] = {
        "idle", "armed", "held", "paused", "released", "cancelled",
    };
    NV2AFlipProbe *probe = &d->flip_probe;
    XemuFlipProbeStatus *status = g_new0(XemuFlipProbeStatus, 1);
    status->enabled = probe->enabled;
    status->epoch = probe->epoch;
    status->generation = probe->gate.generation;
    status->phase = g_strdup(phases[probe->gate.phase]);
    status->target_read3d = probe->gate.target;
    status->presented_generation = qatomic_read(&probe->presented_generation);
    status->cancel_reason = g_strdup(probe->cancel_reason);
    status->current = export_counters(snapshot(probe));
    if (probe->has_arm) {
        status->arm = export_counters(probe->arm);
    }
    if (probe->has_quiesce) {
        status->quiesce = export_counters(probe->quiesce);
    }
    if (probe->has_pause) {
        status->pause = export_counters(probe->pause);
    }
    return status;
}

static void stop_on_main_loop(void *opaque)
{
    NV2AState *d = opaque;
    NV2AFlipProbe *probe = &d->flip_probe;
    uint64_t generation;
    NV2AFlipProbeCounters paused;

    assert(bql_locked());
    qemu_mutex_lock(&d->pfifo.lock);
    generation = probe->pending_generation;
    if (!nv2a_flip_probe_pause(&probe->gate, generation)) {
        qemu_mutex_unlock(&d->pfifo.lock);
        return;
    }
    qemu_mutex_unlock(&d->pfifo.lock);

    /*
     * PFIFO guest methods are held; renderer sync requests remain runnable.
     * Never stop while holding a device lock or from a vCPU callback.
     */
    vm_stop(RUN_STATE_PAUSED);

    qemu_mutex_lock(&d->pfifo.lock);
    paused = snapshot(probe);
    probe->pause = paused;
    probe->has_pause = true;
    qatomic_set(&probe->paused_generation, generation);
    qemu_mutex_unlock(&d->pfifo.lock);

    fprintf(stderr, "nv2a relative POC: generation=%" PRIu64
            " paused read3d=%" PRIu64 " completed=%" PRIu64
            " vblank=%" PRIu64 " present=%" PRIu64
            " timestamp_us=%" PRId64 "\n", generation,
            paused.guest_read3d, paused.completed_flip_stall,
            paused.vblank, paused.host_present, paused.timestamp_us);
}

void nv2a_flip_probe_init(NV2AState *d)
{
    NV2AFlipProbe *probe = &d->flip_probe;
    probe->enabled = g_strcmp0(g_getenv("XEMU_NV2A_FLIP_PROBE"), "1") == 0;
    probe->epoch = 1;
    probe->stop_bh = qemu_bh_new(stop_on_main_loop, d);
}

void nv2a_flip_probe_destroy(NV2AState *d)
{
    qatomic_set(&d->flip_probe.enabled, false);
    qemu_bh_delete(d->flip_probe.stop_bh);
}

void nv2a_flip_probe_read3d(NV2AState *d)
{
    if (qatomic_read(&d->flip_probe.enabled)) {
        qatomic_inc(&d->flip_probe.counters.guest_read3d);
    }
}

void nv2a_flip_probe_vblank(NV2AState *d)
{
    if (qatomic_read(&d->flip_probe.enabled)) {
        qatomic_inc(&d->flip_probe.counters.vblank);
    }
}

bool nv2a_flip_probe_complete_stall(NV2AState *d)
{
    NV2AFlipProbe *probe = &d->flip_probe;
    if (!qatomic_read(&probe->enabled)) {
        return false;
    }
    qatomic_inc(&probe->counters.completed_flip_stall);
    if (!nv2a_flip_probe_complete(&probe->gate,
            qatomic_read(&probe->counters.guest_read3d),
            qatomic_read(&probe->counters.completed_flip_stall))) {
        return false;
    }
    probe->quiesce = snapshot(probe);
    probe->has_quiesce = true;
    probe->pending_generation = probe->gate.generation;
    qemu_bh_schedule(probe->stop_bh);
    return true;
}

void nv2a_flip_probe_cancel_locked(NV2AState *d, const char *reason,
                                  bool new_epoch)
{
    NV2AFlipProbe *probe = &d->flip_probe;
    nv2a_flip_probe_cancel(&probe->gate);
    qemu_bh_cancel(probe->stop_bh);
    probe->cancel_reason = reason;
    qatomic_set(&probe->paused_generation, 0);
    if (new_epoch) {
        probe->epoch++;
        probe->has_arm = false;
        probe->has_quiesce = false;
        probe->has_pause = false;
        probe->gate.target = 0;
    }
}

void nv2a_flip_probe_resume_locked(NV2AState *d)
{
    nv2a_flip_probe_release(&d->flip_probe.gate);
    qatomic_set(&d->flip_probe.paused_generation, 0);
}

uint64_t nv2a_flip_probe_present_begin(void)
{
    return g_nv2a && qatomic_read(&g_nv2a->flip_probe.enabled) ?
        qatomic_read(&g_nv2a->flip_probe.paused_generation) : 0;
}

void nv2a_flip_probe_present_end(uint64_t generation)
{
    if (!g_nv2a || !qatomic_read(&g_nv2a->flip_probe.enabled)) {
        return;
    }
    NV2AFlipProbe *probe = &g_nv2a->flip_probe;
    qatomic_inc(&probe->counters.host_present);
    if (generation && generation == qatomic_read(&probe->paused_generation)) {
        qatomic_set(&probe->presented_generation, generation);
    }
}

XemuFlipProbeStatus *qmp_x_nv2a_flip_query(Error **errp)
{
    if (!g_nv2a) {
        error_setg(errp, "NV2A is not initialized");
        return NULL;
    }
    qemu_mutex_lock(&g_nv2a->pfifo.lock);
    XemuFlipProbeStatus *status = status_locked(g_nv2a);
    qemu_mutex_unlock(&g_nv2a->pfifo.lock);
    return status;
}

XemuFlipProbeStatus *qmp_x_nv2a_flip_arm(uint64_t delta, Error **errp)
{
    if (!g_nv2a || !qatomic_read(&g_nv2a->flip_probe.enabled) ||
        !runstate_is_running()) {
        error_setg(errp, "Probe requires XEMU_NV2A_FLIP_PROBE=1 "
                   "and a running VM");
        return NULL;
    }
    NV2AFlipProbe *probe = &g_nv2a->flip_probe;
    qemu_mutex_lock(&g_nv2a->pfifo.lock);
    NV2AFlipProbeCounters current = snapshot(probe);
    if (!nv2a_flip_probe_arm(&probe->gate, current.guest_read3d,
                            current.completed_flip_stall, delta)) {
        qemu_mutex_unlock(&g_nv2a->pfifo.lock);
        error_setg(errp, "Invalid delta (1..100000), overflow or active probe");
        return NULL;
    }
    probe->arm = current;
    probe->has_arm = true;
    probe->has_quiesce = false;
    probe->has_pause = false;
    probe->cancel_reason = NULL;
    qatomic_set(&probe->presented_generation, 0);
    XemuFlipProbeStatus *status = status_locked(g_nv2a);
    qemu_mutex_unlock(&g_nv2a->pfifo.lock);
    return status;
}

XemuFlipProbeStatus *qmp_x_nv2a_flip_disarm(Error **errp)
{
    if (!g_nv2a) {
        error_setg(errp, "NV2A is not initialized");
        return NULL;
    }
    qemu_mutex_lock(&g_nv2a->pfifo.lock);
    nv2a_flip_probe_cancel_locked(g_nv2a, "disarm", false);
    pfifo_kick(g_nv2a);
    XemuFlipProbeStatus *status = status_locked(g_nv2a);
    qemu_mutex_unlock(&g_nv2a->pfifo.lock);
    return status;
}

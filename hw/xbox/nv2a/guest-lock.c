/*
 * Observe host renderer waits without changing guest clock or lock behavior.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "system/cpus.h"
#include "system/cpu-timers.h"
#include "system/runstate.h"
#include "system/tcg.h"
#include "exec/icount.h"
#include "trace.h"

#include "guest-lock.h"

/* Diagnostic branch only. Observation must retain the original lock and
 * clock behavior so the timer failure can still be attributed. */
static bool observe_locks_only(void)
{
    static gsize initialized;
    static bool observe;

    if (g_once_init_enter(&initialized)) {
        observe = g_strcmp0(g_getenv("XEMU_ISSUE266_OBSERVE_LOCKS"), "1") == 0;
        g_once_init_leave(&initialized, 1);
    }
    return observe;
}

void nv2a_guest_download_wait(QemuEvent *event, const char *name)
{
    bool enabled = observe_locks_only() &&
                   trace_event_get_state(TRACE_NV2A_ISSUE266_GPU_WAIT);
    bool sample_vm = enabled && !icount_enabled();
    int64_t vm_start_ns =
        sample_vm ? qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) : -1;
    bool held_bql = enabled && bql_locked();
    int64_t host_start_us = enabled ? g_get_monotonic_time() : 0;

    /* Observe the existing readback wait; neither mode pauses time here. */
    qemu_event_wait(event);
    if (enabled) {
        int64_t host_end_us = g_get_monotonic_time();
        if (host_end_us - host_start_us >= 500) {
            trace_nv2a_issue266_gpu_wait(
                name, qemu_get_thread_id(), qemu_in_vcpu_thread(), held_bql,
                host_start_us, host_end_us, vm_start_ns,
                sample_vm ? qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) : -1);
        }
    }
}

void nv2a_guest_mmio_lock(QemuMutex *lock, const char *name)
{
    bool enabled = observe_locks_only() &&
                   trace_event_get_state(TRACE_NV2A_ISSUE266_GPU_WAIT);
    bool sample_vm = enabled && !icount_enabled();
    int64_t vm_start_ns =
        sample_vm ? qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) : -1;
    bool held_bql = enabled && bql_locked();
    int64_t host_start_us = enabled ? g_get_monotonic_time() : 0;

    /* Diagnostic observation never changes the guest clock. */
    qemu_mutex_lock(lock);
    if (enabled) {
        int64_t host_end_us = g_get_monotonic_time();
        if (host_end_us - host_start_us >= 500) {
            trace_nv2a_issue266_gpu_wait(
                name, qemu_get_thread_id(), qemu_in_vcpu_thread(), held_bql,
                host_start_us, host_end_us, vm_start_ns,
                sample_vm ? qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) : -1);
        }
    }
}

void nv2a_guest_mmio_lock_address(QemuMutex *lock, const char *name,
                                  uint64_t address)
{
    char label[64];
    snprintf(label, sizeof(label), "%s-0x%" PRIx64, name, address);
    nv2a_guest_mmio_lock(lock, label);
}

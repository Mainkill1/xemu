/*
 * Keep host renderer work from consuming guest time while the guest vCPU
 * cannot complete an NV2A register or protected VRAM access.
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
    if (observe_locks_only()) {
        bool enabled = trace_event_get_state(TRACE_NV2A_ISSUE266_GPU_WAIT);
        /* Mid-TB icount sampling can update the execution budget or abort.
         * Omit VM time entirely in that mode rather than perturb the probe. */
        bool sample_vm = enabled && !icount_enabled();
        int64_t vm_start_ns =
            sample_vm ? qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) : -1;
        bool held_bql = enabled && bql_locked();
        int64_t host_start_us = enabled ? g_get_monotonic_time() : 0;

        /* Do not acquire BQL or stop ticks: this is the original mutex call. */
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
        return;
    }

    if (qemu_mutex_trylock(lock) == 0) {
        return;
    }

    /*
     * Shader compilation and graphics-pipeline creation run under PGRAPH's
     * state lock. If the vCPU waits there, the host wall clock otherwise
     * advances the Xbox TSC and PTIMER without executing guest instructions.
     * Def Jam can then receive its periodic alarm after the next deadline
     * it intended to program. Stop only that blocked vCPU interval; normal
     * GPU work and uncontended register accesses never alter guest timing.
     * The BQL keeps runstate and this single-vCPU clock transition stable.
     */
    bool vcpu = qemu_in_vcpu_thread();
    bool acquired_bql = vcpu && tcg_enabled() && !bql_locked();
    if (acquired_bql) {
        /* Protected VRAM callbacks also execute from TCG RAM accesses,
         * which arrive without the BQL. Keep the usual BQL -> GPU lock
         * acquisition order while this vCPU is blocked. */
        bql_lock();
    }
    /* KVM/WHPX guest RDTSC is not derived from cpu_get_clock(). Pausing only
     * QEMU_CLOCK_VIRTUAL there would desynchronize PTIMER from the CPU. */
    bool suspend_guest_clock =
        tcg_enabled() && vcpu && bql_locked() && runstate_is_running();
    int64_t start_us = trace_event_get_state(TRACE_NV2A_GUEST_LOCK_WAIT) ?
                           g_get_monotonic_time() :
                           0;
    if (suspend_guest_clock) {
        cpu_disable_ticks();
    }
    qemu_mutex_lock(lock);
    if (suspend_guest_clock && runstate_is_running()) {
        cpu_enable_ticks();
    }
    if (start_us) {
        trace_nv2a_guest_lock_wait(name, g_get_monotonic_time() - start_us,
                                   suspend_guest_clock);
    }
    if (acquired_bql) {
        bql_unlock();
    }
}

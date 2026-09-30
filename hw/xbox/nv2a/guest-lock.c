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
#include "trace.h"

#include "guest-lock.h"

void nv2a_guest_mmio_lock(QemuMutex *lock, const char *name)
{
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
    bool suspend_guest_clock = tcg_enabled() && vcpu && bql_locked() &&
                               runstate_is_running();
    int64_t start_us = trace_event_get_state(TRACE_NV2A_GUEST_LOCK_WAIT) ?
                       g_get_monotonic_time() : 0;
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

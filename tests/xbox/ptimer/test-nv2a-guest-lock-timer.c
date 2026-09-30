/*
 * Exercise NV2A lock waits with the real QEMU VM clock and timer list.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "qemu/seqlock.h"
#include "system/cpu-timers-internal.h"
#include "system/cpu-timers.h"
#include "system/cpus.h"
#include "system/tcg.h"
#include "hw/xbox/nv2a/nv2a_int.h"
#include "hw/xbox/nv2a/guest-lock.h"

static QemuMutex test_bql;
static __thread bool test_bql_held;
static bool irq_raised;
static unsigned int neighbor_callbacks;
bool tcg_allowed = true;
__thread CPUState *current_cpu;
CPUTailQ cpus_queue;

/* This fixture uses normal TCG wall-clock timing, without CPU execution or
 * instruction counting. The real timer notify path must never need either. */
void cpu_exit(CPUState *cpu)
{
    g_assert_not_reached();
}

void async_run_on_cpu(CPUState *cpu, run_on_cpu_func func, run_on_cpu_data data)
{
    g_assert_not_reached();
}

bool qemu_in_vcpu_thread(void)
{
    return true;
}

bool runstate_is_running(void)
{
    return true;
}

bool bql_locked(void)
{
    return test_bql_held;
}

void bql_lock_impl(const char *file, int line)
{
    g_assert_false(test_bql_held);
    qemu_mutex_lock(&test_bql);
    test_bql_held = true;
}

void bql_unlock(void)
{
    g_assert_true(test_bql_held);
    test_bql_held = false;
    qemu_mutex_unlock(&test_bql);
}

void rust_bql_mock_lock(void)
{
    bql_lock();
}

void bql_block_unlock(bool increase)
{
    (void)increase;
}

bool mutex_is_bql(QemuMutex *mutex)
{
    return mutex == &test_bql;
}

void bql_update_status(bool locked)
{
    test_bql_held = locked;
}

void nv2a_update_irq(NV2AState *d)
{
    g_assert_true(bql_locked());
    irq_raised =
        (d->ptimer.pending_interrupts & d->ptimer.enabled_interrupts) != 0;
}

const NV2ABlockInfo blocktable[NV_NUM_BLOCKS] = { 0 };

typedef struct HostWork {
    QemuMutex lock;
    QemuEvent ready;
    QemuEvent start;
    NV2AState *d;
    QEMUTimer *neighbor;
    uint32_t now;
    uint32_t period;
    int64_t deadline_ns;
    unsigned int wait_entries;
} HostWork;

static HostWork *active_work;
static __thread bool waiting_for_host;
static QemuMutexLockFunc saved_lock_func;

/* Use QEMU's existing lock instrumentation boundary to arm both timers and
 * release the holder only when the waiter enters the blocking lock call.
 * The candidate has frozen its clock by this point. The plain-lock control
 * reaches exactly the same boundary without freezing it. Scheduler delays
 * during setup cannot expire the test deadline or remove contention. */
static void waiter_lock(QemuMutex *lock, const char *file, int line)
{
    if (waiting_for_host && lock == &active_work->lock) {
        HostWork *work = active_work;
        g_assert_true(bql_locked());
        g_assert_cmpuint(work->wait_entries++, ==, 0);
        work->now = ptimer_read(work->d, NV_PTIMER_TIME_0, 4);
        work->period = (uint64_t)work->d->pramdac.core_clock_freq * 32U / 20U;
        ptimer_write(work->d, NV_PTIMER_ALARM_0, work->now + work->period, 4);
        ptimer_write(work->d, NV_PTIMER_INTR_EN_0, NV_PTIMER_INTR_0_ALARM, 4);
        work->deadline_ns = timer_expire_time_ns(&work->d->ptimer.timer);
        g_assert_true(timer_pending(&work->d->ptimer.timer));
        timer_mod_ns(work->neighbor, work->deadline_ns - 1000000);
        qemu_event_set(&work->start);
    }
    saved_lock_func(lock, file, line);
}

static void *host_work(void *opaque)
{
    HostWork *work = opaque;
    qemu_mutex_lock(&work->lock);
    qemu_event_set(&work->ready);
    qemu_event_wait(&work->start);
    g_usleep(120000);
    qemu_mutex_unlock(&work->lock);
    return NULL;
}

static void neighbor_fired(void *opaque)
{
    unsigned int *count = opaque;
    (*count)++;
}

static void wait_for_deadline(int64_t deadline_ns)
{
    int64_t host_limit_us = g_get_monotonic_time() + 1000000;

    while (qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) < deadline_ns) {
        g_assert_cmpint(g_get_monotonic_time(), <, host_limit_us);
        g_usleep(100);
    }
}

static void run_timer_chain(bool initially_holds_bql, bool use_guest_lock)
{
    NV2AState *d = g_new0(NV2AState, 1);
    HostWork work;
    QemuThread thread;
    QEMUTimer neighbor;

    work.wait_entries = 0;
    work.d = d;
    work.neighbor = &neighbor;
    active_work = &work;
    saved_lock_func = qatomic_read(&qemu_mutex_lock_func);
    qatomic_set(&qemu_mutex_lock_func, waiter_lock);
    qemu_mutex_init(&work.lock);
    qemu_event_init(&work.ready, false);
    qemu_event_init(&work.start, false);
    qemu_thread_create(&thread, "nv2a-host-work", host_work, &work,
                       QEMU_THREAD_JOINABLE);
    qemu_event_wait(&work.ready);

    bql_lock();
    cpu_enable_ticks();
    d->pramdac.core_clock_freq = 233333333;
    d->ptimer.numerator = 1;
    d->ptimer.denominator = 1;
    ptimer_init(d);
    irq_raised = false;
    neighbor_callbacks = 0;

    /* A 50 ms guest period, shorter than the 120 ms host-only lock hold.
     * The lock hook programs it at the actual wait boundary. */
    timer_init_ns(&neighbor, QEMU_CLOCK_VIRTUAL, neighbor_fired,
                  &neighbor_callbacks);
    if (!initially_holds_bql) {
        bql_unlock();
    }
    waiting_for_host = true;
    if (use_guest_lock) {
        nv2a_guest_mmio_lock(&work.lock, "timer-chain");
    } else {
        qemu_mutex_lock(&work.lock);
    }
    waiting_for_host = false;
    g_assert_cmpuint(work.wait_entries, ==, 1);
    qemu_mutex_unlock(&work.lock);
    g_assert_cmpint(bql_locked(), ==, initially_holds_bql);
    if (!initially_holds_bql) {
        bql_lock();
    }

    int64_t deadline_ns = work.deadline_ns;
    int64_t next_target_ns = deadline_ns + 50000000;
    g_assert_cmpint(timer_expire_time_ns(&d->ptimer.timer), ==, deadline_ns);
    if (use_guest_lock) {
        /* Neither real timer is expired by the host wait, nor requeued. */
        g_assert_cmpint(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL), <, deadline_ns);
        g_assert_false(qemu_clock_run_timers(QEMU_CLOCK_VIRTUAL));
        g_assert_false(irq_raised);
        g_assert_cmpuint(neighbor_callbacks, ==, 0);

        wait_for_deadline(deadline_ns);
        g_assert_true(qemu_clock_run_timers(QEMU_CLOCK_VIRTUAL));
        g_assert_true(irq_raised);
        g_assert_cmpuint(neighbor_callbacks, ==, 1);
        g_assert_cmpint(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL), <,
                        next_target_ns);
        ptimer_write(d, NV_PTIMER_INTR_0, NV_PTIMER_INTR_0_ALARM, 4);
        g_assert_false(irq_raised);

        /* The guest can arm the next occurrence after acknowledging one. */
        ptimer_write(d, NV_PTIMER_ALARM_0, work.now + work.period * 2U, 4);
        deadline_ns = timer_expire_time_ns(&d->ptimer.timer);
        g_assert_cmpint(deadline_ns, >, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
        wait_for_deadline(deadline_ns);
        g_assert_true(qemu_clock_run_timers(QEMU_CLOCK_VIRTUAL));
        g_assert_true(irq_raised);
    } else {
        /* Control: the real callback fires, but a proposed next target is
         * already past. This does not execute the title's scheduling code. */
        g_assert_true(qemu_clock_run_timers(QEMU_CLOCK_VIRTUAL));
        g_assert_true(irq_raised);
        g_assert_cmpuint(neighbor_callbacks, ==, 1);
        g_assert_cmpint(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL), >,
                        next_target_ns);
    }

    timer_del(&neighbor);
    timer_del(&d->ptimer.timer);
    cpu_disable_ticks();
    bql_unlock();
    qemu_thread_join(&thread);
    qatomic_set(&qemu_mutex_lock_func, saved_lock_func);
    active_work = NULL;
    qemu_event_destroy(&work.start);
    qemu_event_destroy(&work.ready);
    qemu_mutex_destroy(&work.lock);
    g_free(d);
}

static void test_mmio_timer_chain(void)
{
    run_timer_chain(true, true);
}

static void test_ram_timer_chain(void)
{
    run_timer_chain(false, true);
}

static void test_late_guest_target_control(void)
{
    run_timer_chain(true, false);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qemu_mutex_init(&test_bql);
    seqlock_init(&timers_state.vm_clock_seqlock);
    qemu_spin_init(&timers_state.vm_clock_lock);
    qemu_init_clocks(NULL);
    bql_lock();
    qemu_clock_enable(QEMU_CLOCK_VIRTUAL, true);
    bql_unlock();
    g_test_add_func("/nv2a/real-timers/mmio-wait", test_mmio_timer_chain);
    g_test_add_func("/nv2a/real-timers/ram-wait", test_ram_timer_chain);
    g_test_add_func("/nv2a/real-timers/late-target-control",
                    test_late_guest_target_control);
    return g_test_run();
}

/* NV2A guest MMIO lock waits must not spend guest timer time. */
#include "qemu/osdep.h"
#include "qemu/thread.h"
#include "qemu/main-loop.h"
#include "system/cpus.h"
#include "system/cpu-timers.h"
#include "system/runstate.h"
#include "system/tcg.h"

#include "hw/xbox/nv2a/guest-lock.h"

static bool mock_vcpu = true;
static bool mock_running = true;
bool tcg_allowed = true;
static __thread bool mock_bql_held;
static int bql_acquires;
static int bql_releases;
static int pauses;
static int resumes;
static int64_t clock_offset_us;
static int64_t pause_start_us;
static QemuEvent *pause_observed;

bool qemu_in_vcpu_thread(void)
{
    return mock_vcpu;
}

bool bql_locked(void)
{
    return mock_bql_held;
}

void bql_lock_impl(const char *file, int line)
{
    g_assert_false(mock_bql_held);
    mock_bql_held = true;
    bql_acquires++;
}

void bql_unlock(void)
{
    g_assert_true(mock_bql_held);
    mock_bql_held = false;
    bql_releases++;
}

/* Supply the remaining test-only BQL stub symbols so qemuutil does not pull
 * its global-state stub, which cannot model an initially unlocked vCPU. */
void rust_bql_mock_lock(void)
{
    mock_bql_held = true;
}

void bql_block_unlock(bool increase)
{
    (void)increase;
}

bool mutex_is_bql(QemuMutex *mutex)
{
    (void)mutex;
    return false;
}

void bql_update_status(bool locked)
{
    mock_bql_held = locked;
}

bool runstate_is_running(void)
{
    return mock_running;
}

void cpu_disable_ticks(void)
{
    g_assert_cmpint(pause_start_us, ==, 0);
    pause_start_us = g_get_monotonic_time();
    pauses++;
    if (pause_observed) {
        qemu_event_set(pause_observed);
    }
}

void cpu_enable_ticks(void)
{
    g_assert_cmpint(pause_start_us, !=, 0);
    clock_offset_us += g_get_monotonic_time() - pause_start_us;
    pause_start_us = 0;
    resumes++;
}

static int64_t guest_clock_us(void)
{
    int64_t now = g_get_monotonic_time();
    return (pause_start_us ? pause_start_us : now) - clock_offset_us;
}

typedef struct LockHolder {
    QemuMutex lock;
    QemuEvent acquired;
    QemuEvent release;
} LockHolder;

static void *hold_lock(void *opaque)
{
    LockHolder *holder = opaque;
    qemu_mutex_lock(&holder->lock);
    qemu_event_set(&holder->acquired);
    qemu_event_wait(&holder->release);
    qemu_mutex_unlock(&holder->lock);
    return NULL;
}

typedef struct LockWaiter {
    LockHolder *holder;
    QemuEvent started;
    bool initially_holds_bql;
    bool ended_holding_bql;
    int64_t wall_elapsed;
    int64_t guest_elapsed;
} LockWaiter;

static void *wait_for_lock(void *opaque)
{
    LockWaiter *waiter = opaque;
    mock_bql_held = waiter->initially_holds_bql;
    int64_t wall_start = g_get_monotonic_time();
    int64_t guest_start = guest_clock_us();
    qemu_event_set(&waiter->started);
    nv2a_guest_mmio_lock(&waiter->holder->lock, "test");
    waiter->wall_elapsed = g_get_monotonic_time() - wall_start;
    waiter->guest_elapsed = guest_clock_us() - guest_start;
    waiter->ended_holding_bql = mock_bql_held;
    qemu_mutex_unlock(&waiter->holder->lock);
    return NULL;
}

static void test_uncontended(void)
{
    QemuMutex lock;
    qemu_mutex_init(&lock);
    pauses = resumes = 0;
    nv2a_guest_mmio_lock(&lock, "test");
    g_assert_cmpint(pauses, ==, 0);
    g_assert_cmpint(resumes, ==, 0);
    qemu_mutex_unlock(&lock);
    qemu_mutex_destroy(&lock);
}

static void test_contended(bool vcpu, bool bql, bool running, bool tcg,
                           bool expected_pause)
{
    LockHolder holder = { 0 };
    LockWaiter waiter = { .holder = &holder, .initially_holds_bql = bql };
    QemuEvent paused;
    QemuThread holder_thread;
    QemuThread waiter_thread;
    qemu_mutex_init(&holder.lock);
    qemu_event_init(&holder.acquired, false);
    qemu_event_init(&holder.release, false);
    qemu_event_init(&waiter.started, false);
    qemu_event_init(&paused, false);
    mock_vcpu = vcpu;
    mock_running = running;
    tcg_allowed = tcg;
    pauses = resumes = bql_acquires = bql_releases = 0;
    clock_offset_us = pause_start_us = 0;
    pause_observed = &paused;
    qemu_thread_create(&holder_thread, "nv2a-lock-holder", hold_lock, &holder,
                       QEMU_THREAD_JOINABLE);
    qemu_event_wait(&holder.acquired);
    qemu_thread_create(&waiter_thread, "nv2a-lock-waiter", wait_for_lock,
                       &waiter, QEMU_THREAD_JOINABLE);
    qemu_event_wait(&waiter.started);
    if (expected_pause) {
        qemu_event_wait(&paused);
        g_usleep(40000);
    }
    qemu_event_set(&holder.release);
    qemu_thread_join(&waiter_thread);
    qemu_thread_join(&holder_thread);

    g_assert_cmpint(pauses, ==, expected_pause ? 1 : 0);
    g_assert_cmpint(resumes, ==, expected_pause ? 1 : 0);
    g_assert_cmpint(bql_acquires, ==, vcpu && tcg && !bql ? 1 : 0);
    g_assert_cmpint(bql_releases, ==, vcpu && tcg && !bql ? 1 : 0);
    g_assert_cmpint(waiter.ended_holding_bql, ==, bql);
    if (expected_pause) {
        g_assert_cmpint(waiter.wall_elapsed, >, 20000);
        /* A 16 ms guest alarm must not expire during a host GPU wait. */
        g_assert_cmpint(waiter.guest_elapsed, <, 16000);
    }
    pause_observed = NULL;
    qemu_event_destroy(&paused);
    qemu_event_destroy(&waiter.started);
    qemu_event_destroy(&holder.release);
    qemu_event_destroy(&holder.acquired);
    qemu_mutex_destroy(&holder.lock);
}

static void test_vcpu_wait(void)
{
    test_contended(true, true, true, true, true);
}

static void test_ram_callback_wait(void)
{
    test_contended(true, false, true, true, true);
}

static void test_host_wait(void)
{
    test_contended(false, false, true, true, false);
}

static void test_paused_vm_wait(void)
{
    test_contended(true, true, false, true, false);
}

static void test_accelerator_wait(void)
{
    test_contended(true, true, true, false, false);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/nv2a/guest-lock/uncontended", test_uncontended);
    g_test_add_func("/nv2a/guest-lock/vcpu-wait", test_vcpu_wait);
    g_test_add_func("/nv2a/guest-lock/ram-callback-wait",
                    test_ram_callback_wait);
    g_test_add_func("/nv2a/guest-lock/host-wait", test_host_wait);
    g_test_add_func("/nv2a/guest-lock/paused-vm-wait", test_paused_vm_wait);
    g_test_add_func("/nv2a/guest-lock/accelerator-wait", test_accelerator_wait);
    return g_test_run();
}

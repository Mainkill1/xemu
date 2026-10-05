/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Direct production integration: the inclusions also expose private FIFO
 * predicates and Kelvin methods without adding a public/test API to NV2A. */
#include "contention-hooks.h"
#include "hw/xbox/nv2a/pgraph/pgraph.c"

static void test_renderer_init(NV2AState *d);
static void test_renderer_pending(NV2AState *d);
static void test_renderer_reports(NV2AState *d);
static int test_unused_method(NV2AState *d, unsigned int subchannel,
                              unsigned int method, uint32_t parameter,
                              uint32_t *parameters, size_t available,
                              size_t lookahead, bool inc);
/* Renderer/draw service is outside this fixture. The real FIFO loop, stall
 * predicate, kick flag, broadcasts and atomic cond-wait release are retained.
 */
#define pgraph_init_thread test_renderer_init
#define pgraph_process_pending test_renderer_pending
#define pgraph_process_pending_reports test_renderer_reports
#define pgraph_method test_unused_method
#include "hw/xbox/nv2a/pfifo.c"
#undef pgraph_init_thread
#undef pgraph_process_pending
#undef pgraph_process_pending_reports
#undef pgraph_method

#define PERIOD_TICKS UINT64_C(12222222)
#define TSC_HZ UINT64_C(733333333)
#define PERIODS 10000
#define STRESS_ROUNDS 2000
#define FIFO_ROUNDS 256
#define MODULO 7
#define UNRELATED 0x0005a55aU

typedef enum Actor { CONTROL, RENDERER, TIMER, CAS_OWNER, FIFO } Actor;
static _Thread_local Actor actor;
static _Thread_local bool bql_owned, fifo_owned, pause_cas;
static QemuMutex test_bql;
static NV2AState *device;
static int64_t virtual_ns;
static uint64_t callbacks, alarm_writes, rejections, renderer_waits;
static uint64_t control_accesses, cas_retries, fifo_evaluations,
    flip_transitions;
static uint64_t fifo_wakeups;
static int64_t maximum_lateness, maximum_bql_wait;
static uint64_t previous_target;
static bool chain_active, negative_control;
static bool renderer_held;
static QemuEvent renderer_ready, renderer_release, timer_go, timer_done;
static QemuEvent timer_waiting, cas_ready, cas_resume, cas_go, cas_done;
static QemuEvent fifo_boundary, fifo_resume, fifo_waiting, fifo_sleeping;
static int fifo_scenario;
static bool fifo_pause_once;
static bool shutdown_actors;
unsigned int xemu_tweaks_active;
const NV2ABlockInfo blocktable[NV_NUM_BLOCKS] = { 0 };

bool bql_locked(void)
{
    return bql_owned;
}
bool mutex_is_bql(QemuMutex *m)
{
    return m == &test_bql;
}
void bql_update_status(bool locked)
{
    bql_owned = locked;
}
void bql_block_unlock(bool increase)
{
    g_assert_true(bql_owned);
}

void bql_lock_impl(const char *file, int line)
{
    g_assert_false(bql_owned);
    int64_t start = qatomic_read(&virtual_ns);
    if (actor == TIMER && qemu_mutex_trylock_impl(&test_bql, file, line)) {
        qemu_event_set(&timer_waiting);
        qemu_mutex_lock_impl(&test_bql, file, line);
    } else if (actor != TIMER) {
        qemu_mutex_lock_impl(&test_bql, file, line);
    }
    bql_owned = true;
    if (actor == TIMER) {
        maximum_bql_wait =
            MAX(maximum_bql_wait, qatomic_read(&virtual_ns) - start);
    }
}

void bql_unlock(void)
{
    g_assert_true(bql_owned);
    bql_owned = false;
    qemu_mutex_unlock_impl(&test_bql, __FILE__, __LINE__);
}

void contention_mutex_lock(QemuMutex *m, const char *file, int line)
{
    if (device && m == &device->pgraph.lock) {
        /* A FIFO predicate must never acquire the renderer mutex. */
        if (actor == FIFO) {
            g_assert_false(fifo_owned);
        }
        if (bql_owned && qatomic_read(&renderer_held)) {
            renderer_waits++;
            /* Reach the timer deadline while BQL remains owned by the guest.
             * Confirm actual contention with trylock before adding lateness. */
            qatomic_set(&virtual_ns, device->ptimer.timer.expire_time);
            qemu_event_set(&timer_go);
            qemu_event_wait(&timer_waiting);
            qatomic_set(&virtual_ns,
                        virtual_ns +
                            (PERIOD_TICKS * UINT64_C(1000000000) / TSC_HZ) +
                            9000000);
            qemu_event_set(&renderer_release);
        }
    }
    if (device && m == &device->pfifo.lock && actor == CONTROL) {
        qemu_event_set(&fifo_waiting);
    }
    qemu_mutex_lock_impl(m, file, line);
    if (device && m == &device->pfifo.lock) {
        fifo_owned = true;
    }
}

void contention_mutex_unlock(QemuMutex *m, const char *file, int line)
{
    if (device && m == &device->pfifo.lock) {
        fifo_owned = false;
    }
    qemu_mutex_unlock_impl(m, file, line);
}

void contention_cond_wait(QemuCond *c, QemuMutex *m, const char *file, int line)
{
    if (device && c == &device->pfifo.fifo_cond && actor == FIFO) {
        /* The kick producer must block on this same mutex until cond-wait
         * atomically releases it. Test both sides of the flag/check boundary.
         */
        if (fifo_pause_once && fifo_scenario == 6) {
            fifo_pause_once = false;
            qemu_event_set(&fifo_boundary);
            qemu_event_wait(&fifo_resume);
        }
        qemu_event_set(&fifo_sleeping);
    }
    fifo_owned = false;
    qemu_cond_wait_impl(c, m, file, line);
    fifo_owned = true;
    if (device && c == &device->pfifo.fifo_cond && actor == FIFO) {
        qatomic_inc(&fifo_wakeups);
    }
}

uint32_t contention_cas(uint32_t *ptr, uint32_t old, uint32_t next)
{
    if (pause_cas && ptr == &device->pgraph.regs_[NV_PGRAPH_SURFACE]) {
        pause_cas = false;
        qemu_event_set(&cas_ready);
        qemu_event_wait(&cas_resume);
    }
    uint32_t result = contention_real_cas(ptr, old, next);
    if (result != old) {
        qatomic_inc(&cas_retries);
    }
    return result;
}

/* Only the clock/queue effects are fake; callbacks and all PTIMER arithmetic
 * run from linked ptimer.c/ptimer_core.c. BQL is a real host mutex. */
int64_t qemu_clock_get_ns(QEMUClockType type)
{
#ifdef NV2A_CONTENTION_COST
    if (type == QEMU_CLOCK_REALTIME) {
        return get_clock();
    }
#endif
    g_assert_cmpint(type, ==, QEMU_CLOCK_VIRTUAL);
    return qatomic_read(&virtual_ns);
}
void timer_init_full(QEMUTimer *t, QEMUTimerListGroup *group,
                     QEMUClockType type, int scale, int attributes,
                     QEMUTimerCB *cb, void *opaque)
{
    t->cb = cb;
    t->opaque = opaque;
    t->scale = scale;
    t->expire_time = -1;
}
void timer_mod(QEMUTimer *t, int64_t expiry)
{
    t->expire_time = expiry;
}
void timer_del(QEMUTimer *t)
{
    t->expire_time = -1;
}
bool timer_pending(const QEMUTimer *t)
{
    return t->expire_time >= 0;
}
uint64_t timer_expire_time_ns(const QEMUTimer *t)
{
    return t->expire_time;
}
void nv2a_update_irq(NV2AState *d)
{
    g_assert_true(bql_owned);
}
#ifndef NV2A_CONTENTION_COST
#ifdef NV2A_CONTENTION_PRE_FLIP
void nv2a_profile_increment(void)
{
}
#else
int64_t nv2a_profile_increment(void)
{
    return virtual_ns;
}
void nv2a_profile_log_increment(int64_t now)
{
}
#endif
#endif
uint64_t memory_region_size(MemoryRegion *mr)
{
    g_assert_not_reached();
}
void *nv_dma_map(NV2AState *d, hwaddr address, hwaddr *len)
{
    g_assert_not_reached();
}
uint32_t pgraph_rdi_read(PGRAPHState *pg, unsigned int select,
                         unsigned int address)
{
    g_assert_not_reached();
}
void pgraph_rdi_write(PGRAPHState *pg, unsigned int select,
                      unsigned int address, uint32_t value)
{
    g_assert_not_reached();
}

static uint64_t rdtsc_now(void)
{
    uint64_t ns = virtual_ns;
    return ns / UINT64_C(1000000000) * TSC_HZ +
           ns % UINT64_C(1000000000) * TSC_HZ / UINT64_C(1000000000);
}
static void program_alarm(uint64_t target)
{
    uint64_t ns =
        target / TSC_HZ * UINT64_C(1000000000) +
        (target % TSC_HZ * UINT64_C(1000000000) + TSC_HZ - 1) / TSC_HZ;
    /* PTIMER units are GPU ticks shifted5, not RDTSC ticks. Round one GPU
     * tick forward so the synthetic target cannot become the current epoch. */
    uint64_t ticks =
        ns / UINT64_C(1000000000) * device->pramdac.core_clock_freq +
        ns % UINT64_C(1000000000) * device->pramdac.core_clock_freq /
            UINT64_C(1000000000);
    uint32_t alarm = (ticks + 1) << 5;
    ptimer_write(device, NV_PTIMER_ALARM_0, alarm, 4);
    alarm_writes++;
}
static void scheduler_rearm(void)
{
    uint64_t next = previous_target + PERIOD_TICKS;
    if ((int64_t)(next - rdtsc_now()) <= 0) {
        rejections++;
        chain_active = false;
        return;
    }
    previous_target = next;
    program_alarm(next);
}
static void *renderer_worker(void *opaque)
{
    actor = RENDERER;
    qemu_mutex_lock(&device->pgraph.lock);
    qatomic_set(&renderer_held, true);
    qemu_event_set(&renderer_ready);
    qemu_event_wait(&renderer_release);
    qatomic_set(&renderer_held, false);
    qemu_mutex_unlock(&device->pgraph.lock);
    return NULL;
}
static void *timer_worker(void *opaque)
{
    actor = TIMER;
    for (;;) {
        qemu_event_wait(&timer_go);
        qemu_event_reset(&timer_go);
        if (qatomic_read(&shutdown_actors)) {
            break;
        }
        bql_lock();
        QEMUTimer *t = &device->ptimer.timer;
        g_assert_true(timer_pending(t));
        maximum_lateness = MAX(maximum_lateness, virtual_ns - t->expire_time);
        timer_del(t);
        t->cb(t->opaque);
        g_assert_cmphex(device->ptimer.pending_interrupts &
                            NV_PTIMER_INTR_0_ALARM,
                        ==, NV_PTIMER_INTR_0_ALARM);
        callbacks++;
        ptimer_write(device, NV_PTIMER_INTR_0, NV_PTIMER_INTR_0_ALARM, 4);
        scheduler_rearm();
        bql_unlock();
        qemu_event_set(&timer_done);
    }
    return NULL;
}
static void access_control(void)
{
    pgraph_read(device, NV_PGRAPH_INCREMENT, 4);
    pgraph_write(device, NV_PGRAPH_INCREMENT, NV_PGRAPH_INCREMENT_READ_3D, 4);
    uint32_t s = pgraph_read(device, NV_PGRAPH_SURFACE, 4);
    pgraph_write(device, NV_PGRAPH_SURFACE, s, 4);
    control_accesses += 4;
}
static void report(const char *phase)
{
    g_test_message(
        "%s callback_count=%" PRIu64 " alarm_write_count=%" PRIu64
        " past_target_rejections=%" PRIu64
        " maximum_callback_lateness_ns=%" PRId64
        " maximum_BQL_wait_virtual_ns=%" PRId64
        " forbidden_renderer_waits=%" PRIu64 " PGRAPH_control_accesses=%" PRIu64
        " CAS_retries=%" PRIu64 " PFIFO_evaluations=%" PRIu64
        " PFIFO_wakeups=%" PRIu64 " flip_stall_transitions=%" PRIu64,
        phase, callbacks, alarm_writes, rejections, maximum_lateness,
        maximum_bql_wait, renderer_waits, control_accesses, cas_retries,
        fifo_evaluations, fifo_wakeups, flip_transitions);
}
static void recurring_chain(void)
{
    QemuThread renderer, timer;
    callbacks = alarm_writes = rejections = renderer_waits = control_accesses =
        0;
    maximum_lateness = maximum_bql_wait = 0;
    virtual_ns = 0;
    chain_active = true;
    shutdown_actors = false;
    qemu_event_reset(&renderer_ready);
    qemu_event_reset(&renderer_release);
    qemu_event_reset(&timer_go);
    qemu_event_reset(&timer_done);
    qemu_event_reset(&timer_waiting);
    bql_lock();
    ptimer_init(device);
    ptimer_write(device, NV_PTIMER_INTR_EN_0, NV_PTIMER_INTR_0_ALARM, 4);
    previous_target = PERIOD_TICKS;
    program_alarm(previous_target);
    bql_unlock();
    qemu_thread_create(&renderer, "renderer", renderer_worker, NULL,
                       QEMU_THREAD_JOINABLE);
    qemu_event_wait(&renderer_ready);
    qemu_thread_create(&timer, "timer", timer_worker, NULL,
                       QEMU_THREAD_JOINABLE);
    for (unsigned i = 0; i < PERIODS && chain_active; i++) {
        qemu_event_reset(&timer_done);
        bql_lock();
        access_control();
        if (!renderer_waits) {
            virtual_ns = device->ptimer.timer.expire_time;
            qemu_event_set(&timer_go);
        }
        bql_unlock();
        qemu_event_wait(&timer_done);
    }
    qatomic_set(&shutdown_actors, true);
    qemu_event_set(&timer_go);
    qemu_event_set(&renderer_release);
    qemu_thread_join(&timer);
    qemu_thread_join(&renderer);
    report("recurring");
    if (negative_control) {
        g_assert_cmpuint(renderer_waits, >, 0);
        g_assert_cmpuint(rejections, >, 0);
        g_assert_false(chain_active);
        g_assert_cmpuint(callbacks, !=, PERIODS);
    } else {
        g_assert_cmpuint(renderer_waits, ==, 0);
        g_assert_cmpuint(rejections, ==, 0);
        g_assert_true(chain_active);
        g_assert_cmphex(device->ptimer.enabled_interrupts &
                            NV_PTIMER_INTR_0_ALARM,
                        ==, NV_PTIMER_INTR_0_ALARM);
        g_assert_cmpuint(callbacks, ==, PERIODS);
        g_assert_cmpuint(alarm_writes, ==, PERIODS + 1);
    }
}

typedef enum FlipOp { READ_INC, WRITE_INC, WRITE_SET, MODULO_SET } FlipOp;
static FlipOp owner_op;
static uint32_t owner_value;
static void flip_operation(FlipOp op, uint32_t value)
{
    size_t consumed = 0;
    switch (op) {
    case READ_INC:
        bql_lock();
        pgraph_write(device, NV_PGRAPH_INCREMENT, NV_PGRAPH_INCREMENT_READ_3D,
                     4);
        bql_unlock();
        break;
    case WRITE_INC:
        pgraph_NV097_FLIP_INCREMENT_WRITE_handler(
            device, &device->pgraph, 0, NV097_FLIP_INCREMENT_WRITE, value, NULL,
            1, &consumed, false);
        break;
    case WRITE_SET:
        pgraph_NV097_SET_FLIP_WRITE_handler(device, &device->pgraph, 0,
                                            NV097_SET_FLIP_WRITE, value, NULL,
                                            1, &consumed, false);
        break;
    case MODULO_SET:
        pgraph_NV097_SET_FLIP_MODULO_handler(device, &device->pgraph, 0,
                                             NV097_SET_FLIP_MODULO, value, NULL,
                                             1, &consumed, false);
        break;
    }
}
static void *cas_worker(void *opaque)
{
    actor = CAS_OWNER;
    for (;;) {
        qemu_event_wait(&cas_go);
        qemu_event_reset(&cas_go);
        if (qatomic_read(&shutdown_actors)) {
            break;
        }
        pause_cas = true;
        flip_operation(owner_op, owner_value);
        qemu_event_set(&cas_done);
    }
    return NULL;
}
static uint32_t oracle_word(unsigned r, unsigned w, unsigned m, uint32_t bits)
{
    /* Independent literal bit positions, no production SET_MASK/helper. */
    return (r << 24) | (w << 20) | (m << 28) | bits;
}
static void cas_matrix(void)
{
    QemuThread thread;
    shutdown_actors = false;
    cas_retries = 0;
    qemu_thread_create(&thread, "cas-owner", cas_worker, NULL,
                       QEMU_THREAD_JOINABLE);
    for (unsigned pattern = 0; pattern < 5; pattern++) {
        for (unsigned i = 0; i < STRESS_ROUNDS; i++) {
            unsigned r = i % MODULO, w = (i * 3) % MODULO;
            uint32_t expected;
            pgraph_reg_w(&device->pgraph, NV_PGRAPH_SURFACE,
                         oracle_word(r, w, MODULO, UNRELATED));
            owner_op = pattern == 2 || pattern == 4 ? WRITE_SET :
                       pattern == 3                 ? WRITE_INC :
                                                      READ_INC;
            owner_value = (w + 2) % MODULO;
            qemu_event_reset(&cas_ready);
            qemu_event_reset(&cas_resume);
            qemu_event_reset(&cas_done);
            qemu_event_set(&cas_go);
            qemu_event_wait(&cas_ready);
            if (pattern == 0) {
                flip_operation(WRITE_INC, 0);
                expected = oracle_word((r + 1) % MODULO, (w + 1) % MODULO,
                                       MODULO, UNRELATED);
            } else if (pattern == 1 || pattern == 2) {
                flip_operation(MODULO_SET, 5);
                expected =
                    oracle_word(pattern == 1 ? (r + 1) % 5 : r,
                                pattern == 2 ? owner_value : w, 5, UNRELATED);
            } else {
                /* Replace the whole word after A has read its old generation.
                 */
                bql_lock();
                pgraph_write(device, NV_PGRAPH_SURFACE,
                             oracle_word(2, 1, 5, 0x000a1234), 4);
                bql_unlock();
                expected = oracle_word(2, pattern == 4 ? owner_value : 2, 5,
                                       0x000a1234);
            }
            qemu_event_set(&cas_resume);
            qemu_event_wait(&cas_done);
            g_assert_cmphex(pgraph_reg_r(&device->pgraph, NV_PGRAPH_SURFACE),
                            ==, expected);
        }
    }
    qatomic_set(&shutdown_actors, true);
    qemu_event_set(&cas_go);
    qemu_thread_join(&thread);
    g_assert_cmpuint(cas_retries, ==, STRESS_ROUNDS * 5);
    report("CAS-matrix");
}

static void test_renderer_init(NV2AState *d)
{
    actor = FIFO;
}
static void test_renderer_pending(NV2AState *d)
{
    if (fifo_pause_once && fifo_scenario < 5) {
        fifo_pause_once = false;
        qemu_event_set(&fifo_boundary);
        qemu_event_wait(&fifo_resume);
    }
    bool before = qatomic_read(&d->pgraph.waiting_for_flip);
    bool stalled = pfifo_stall_for_flip(d);
    qatomic_inc(&fifo_evaluations);
    if (before && !stalled) {
        qatomic_inc(&flip_transitions);
    }
}
static void test_renderer_reports(NV2AState *d)
{
    if (fifo_pause_once && fifo_scenario == 5) {
        fifo_pause_once = false;
        qemu_event_set(&fifo_boundary);
        qemu_event_wait(&fifo_resume);
    }
}
static int test_unused_method(NV2AState *d, unsigned int subchannel,
                              unsigned int method, uint32_t parameter,
                              uint32_t *parameters, size_t available,
                              size_t lookahead, bool inc)
{
    g_assert_not_reached();
}
static void *fifo_producer(void *opaque)
{
    actor = CONTROL;
    bql_lock();
    if (fifo_scenario == 1) {
        pgraph_write(device, NV_PGRAPH_INCREMENT, NV_PGRAPH_INCREMENT_READ_3D,
                     4);
    } else {
        qemu_mutex_lock(&device->pfifo.lock);
        if (fifo_scenario == 2) {
            flip_operation(WRITE_INC, 0);
        } else {
            pgraph_reg_w(&device->pgraph, NV_PGRAPH_SURFACE,
                         oracle_word(1, 0, MODULO, UNRELATED));
        }
        if (fifo_scenario == 3) {
            qatomic_set(&device->pgraph.waiting_for_flip, true);
        }
        for (unsigned i = 0; i < (fifo_scenario == 4 ? 100 : 1); i++) {
            pfifo_kick(device);
        }
        qemu_mutex_unlock(&device->pfifo.lock);
    }
    bql_unlock();
    return NULL;
}
static void fifo_matrix(void)
{
    for (fifo_scenario = 0; fifo_scenario < 7; fifo_scenario++) {
        for (unsigned round = 0; round < FIFO_ROUNDS; round++) {
            QemuThread worker, producer;
            device->exiting = false;
            device->pfifo.halt = true;
            fifo_pause_once = true;
            pgraph_reg_w(&device->pgraph, NV_PGRAPH_SURFACE,
                         oracle_word(0, 0, MODULO, UNRELATED));
            qatomic_set(&device->pgraph.waiting_for_flip, fifo_scenario != 3);
            qemu_event_reset(&fifo_boundary);
            qemu_event_reset(&fifo_resume);
            qemu_event_reset(&fifo_waiting);
            qemu_event_reset(&fifo_sleeping);
            uint64_t transitions = flip_transitions;
            qemu_thread_create(&worker, "fifo-worker", pfifo_thread, device,
                               QEMU_THREAD_JOINABLE);
            qemu_event_wait(&fifo_boundary);
            qemu_thread_create(&producer, "fifo-producer", fifo_producer, NULL,
                               QEMU_THREAD_JOINABLE);
            qemu_event_wait(&fifo_waiting);
            qemu_event_set(&fifo_resume);
            qemu_thread_join(&producer);
            /* Acquiring PFIFO after a post-kick idle broadcast proves the
             * worker evaluated the new state before entering a real cond-wait.
             */
            qemu_mutex_lock(&device->pfifo.lock);
            while (qatomic_read(&device->pgraph.waiting_for_flip)) {
                qemu_cond_wait(&device->pfifo.fifo_idle_cond,
                               &device->pfifo.lock);
            }
            g_assert_cmpuint(flip_transitions, ==, transitions + 1);
            g_assert_false(pfifo_stall_for_flip(device));
            device->exiting = true;
            pfifo_kick(device);
            qemu_mutex_unlock(&device->pfifo.lock);
            qemu_thread_join(&worker);
        }
    }
    report("PFIFO-matrix");
}
#ifdef NV2A_CONTENTION_COST
static volatile uint64_t cost_sink;
static void control_cost(void)
{
    const unsigned iterations = 100000;
    /* Include the real profiling helpers, disabled file logging, PFIFO mutex,
     * CAS and broadcasts. COST disables every lock/CAS scheduling hook. This
     * is a path-cost diagnostic, never a candidate-versus-baseline speedup. */
    bql_lock();
    for (unsigned pair = 0; pair < 30; pair++) {
        for (unsigned position = 0; position < 2; position++) {
            bool mmio = (pair + position) % 2;
            uint64_t checksum = 0;
            struct timespec begin, end;
            clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &begin);
            if (mmio) {
                for (unsigned i = 0; i < iterations; i++) {
                    checksum += pgraph_read(device, NV_PGRAPH_INCREMENT, 4);
                    pgraph_write(device, NV_PGRAPH_INCREMENT,
                                 NV_PGRAPH_INCREMENT_READ_3D, 4);
                }
            } else {
                for (unsigned i = 0; i < iterations; i++) {
                    checksum += device->pgraph.regs_[NV_PGRAPH_INCREMENT];
                    asm volatile("" : "+r"(checksum) : : "memory");
                }
            }
            clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &end);
            cost_sink = checksum;
            int64_t elapsed =
                (end.tv_sec - begin.tv_sec) * INT64_C(1000000000) +
                end.tv_nsec - begin.tv_nsec;
            g_test_message("cost pair=%u position=%u mmio=%u iterations=%u "
                           "cpu_ns=%" PRId64,
                           pair, position, mmio, iterations, elapsed);
        }
    }
    g_assert_cmpuint((device->pgraph.regs_[NV_PGRAPH_SURFACE] >> 24) & 15, ==,
                     (30 * iterations) % MODULO);
    bql_unlock();
}
#endif

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--negative-control")) {
        negative_control = true;
        argv++;
        argc--;
    }
    g_test_init(&argc, &argv, NULL);
    device = g_new0(NV2AState, 1);
    qemu_mutex_init(&test_bql);
    qemu_mutex_init(&device->pgraph.lock);
    qemu_mutex_init(&device->pfifo.lock);
    qemu_cond_init(&device->pfifo.fifo_cond);
    qemu_cond_init(&device->pfifo.fifo_idle_cond);
    QemuEvent *events[] = { &renderer_ready, &renderer_release, &timer_go,
                            &timer_done,     &timer_waiting,    &cas_ready,
                            &cas_resume,     &cas_go,           &cas_done,
                            &fifo_boundary,  &fifo_resume,      &fifo_waiting,
                            &fifo_sleeping };
    for (unsigned i = 0; i < G_N_ELEMENTS(events); i++) {
        qemu_event_init(events[i], false);
    }
    device->pramdac.core_clock_freq = 233333333;
    device->ptimer.numerator = device->ptimer.denominator = 1;
    pgraph_reg_w(&device->pgraph, NV_PGRAPH_SURFACE,
                 oracle_word(0, 0, MODULO, UNRELATED));
#ifdef NV2A_CONTENTION_COST
    /* Synthetic fixture callbacks are not registered in the unhooked build. */
    (void)recurring_chain;
    (void)cas_matrix;
    (void)fifo_matrix;
    g_test_add_func("/nv2a/contention/control-cost", control_cost);
#else
    g_test_add_func("/nv2a/contention/recurring-chain", recurring_chain);
    if (!negative_control) {
        g_test_add_func("/nv2a/contention/CAS-matrix", cas_matrix);
        g_test_add_func("/nv2a/contention/PFIFO-matrix", fifo_matrix);
    }
#endif
    int result = g_test_run();
    for (unsigned i = 0; i < G_N_ELEMENTS(events); i++) {
        qemu_event_destroy(events[i]);
    }
    qemu_cond_destroy(&device->pfifo.fifo_cond);
    qemu_cond_destroy(&device->pfifo.fifo_idle_cond);
    qemu_mutex_destroy(&test_bql);
    qemu_mutex_destroy(&device->pfifo.lock);
    qemu_mutex_destroy(&device->pgraph.lock);
    g_free(device);
    return result;
}

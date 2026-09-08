/*
 * Deterministic NV2A PTIMER alarm, IRQ, and restore tests.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/timer.h"

#include "hw/xbox/nv2a/nv2a_int.h"
#include "ptimer-test.h"

static bool irq_asserted;

#define PTIMER_REG_EPOCH_NS (1ULL << 27)
#define TEST_ALARM_LOW 0x100

void nv2a_update_irq(NV2AState *d)
{
    if (d->ptimer.pending_interrupts & d->ptimer.enabled_interrupts) {
        d->pmc.pending_interrupts |= NV_PMC_INTR_0_PTIMER;
    } else {
        d->pmc.pending_interrupts &= ~NV_PMC_INTR_0_PTIMER;
    }

    irq_asserted = d->pmc.pending_interrupts && d->pmc.enabled_interrupts;
}

static void init_nv2a_ptimer(NV2AState *d)
{
    memset(d, 0, sizeof(*d));
    ptimer_test_time_ns = 0;
    irq_asserted = false;

    d->pramdac.core_clock_freq = NANOSECONDS_PER_SECOND;
    d->ptimer.numerator = 1;
    d->ptimer.denominator = 1;
    d->pmc.enabled_interrupts = NV_PMC_INTR_EN_0_HARDWARE;
    ptimer_init(d);
}

static void fire_alarm_at(NV2AState *d, int64_t now_ns)
{
    QEMUTimer *timer = &d->ptimer.timer;

    g_assert_true(timer_pending(timer));
    ptimer_test_time_ns = now_ns;
    timer_del(timer);
    timer->next = NULL;
    timer->expire_time = -1;
    timer->cb(timer->opaque);
}

static void expire_alarm(NV2AState *d)
{
    fire_alarm_at(d, timer_expire_time_ns(&d->ptimer.timer));
}

static void make_alarm_four_epochs_overdue(NV2AState *d)
{
    int64_t first_expiry_ns = timer_expire_time_ns(&d->ptimer.timer);

    ptimer_test_time_ns = first_expiry_ns + 4 * PTIMER_REG_EPOCH_NS + 1000;
}

static void assert_alarm_caught_up(const NV2AState *d)
{
    g_assert_cmphex(d->ptimer.alarm_time, ==,
                    (5ULL << 32) | TEST_ALARM_LOW);
    g_assert_cmpint(timer_expire_time_ns(&d->ptimer.timer), >,
                    ptimer_test_time_ns);
}
static void test_alarm_assert_and_ack(void)
{
    NV2AState d;

    init_nv2a_ptimer(&d);
    ptimer_write(&d, NV_PTIMER_INTR_EN_0, NV_PTIMER_INTR_EN_0_ALARM, 4);
    ptimer_write(&d, NV_PTIMER_ALARM_0, 0x100, 4);

    g_assert_false(irq_asserted);
    expire_alarm(&d);

    g_assert_cmphex(d.ptimer.pending_interrupts, ==,
                    NV_PTIMER_INTR_0_ALARM);
    g_assert_cmphex(d.pmc.pending_interrupts & NV_PMC_INTR_0_PTIMER, ==,
                    NV_PMC_INTR_0_PTIMER);
    g_assert_true(irq_asserted);
    g_assert_cmphex(d.ptimer.alarm_time, ==,
                    (1ULL << 32) | TEST_ALARM_LOW);

    ptimer_write(&d, NV_PTIMER_INTR_0, NV_PTIMER_INTR_0_ALARM, 4);
    g_assert_cmphex(d.ptimer.pending_interrupts, ==, 0);
    g_assert_false(irq_asserted);

    ptimer_reset(&d);
}

static void test_pending_alarm_asserts_when_enabled(void)
{
    NV2AState d;

    init_nv2a_ptimer(&d);
    ptimer_write(&d, NV_PTIMER_ALARM_0, 0x100, 4);
    expire_alarm(&d);
    g_assert_false(irq_asserted);

    ptimer_write(&d, NV_PTIMER_INTR_EN_0, NV_PTIMER_INTR_EN_0_ALARM, 4);
    g_assert_true(irq_asserted);

    ptimer_reset(&d);
}

static void test_time_registers_and_future_epoch(void)
{
    NV2AState d;

    init_nv2a_ptimer(&d);
    ptimer_write(&d, NV_PTIMER_TIME_1, 0x12, 4);
    ptimer_write(&d, NV_PTIMER_TIME_0, 0x345678e0, 4);

    g_assert_cmphex(ptimer_read(&d, NV_PTIMER_TIME_1, 4), ==, 0x12);
    g_assert_cmphex(ptimer_read(&d, NV_PTIMER_TIME_0, 4), ==, 0x345678e0);

    ptimer_write(&d, NV_PTIMER_ALARM_0, 0x345678c0, 4);
    g_assert_true(timer_pending(&d.ptimer.timer));
    g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer), >,
                    ptimer_test_time_ns);

    ptimer_reset(&d);
}

static void test_runtime_overdue_alarm_skips_missed_epochs(void)
{
    NV2AState d;
    init_nv2a_ptimer(&d);
    d.ptimer.enabled_interrupts = NV_PTIMER_INTR_EN_0_ALARM;
    ptimer_write(&d, NV_PTIMER_ALARM_0, TEST_ALARM_LOW, 4);

    make_alarm_four_epochs_overdue(&d);
    fire_alarm_at(&d, ptimer_test_time_ns);

    g_assert_cmphex(d.ptimer.pending_interrupts, ==,
                    NV_PTIMER_INTR_0_ALARM);
    g_assert_true(irq_asserted);
    assert_alarm_caught_up(&d);

    ptimer_reset(&d);
}

static void test_post_load_reconciles_overdue_alarm(void)
{
    NV2AState d;

    init_nv2a_ptimer(&d);
    d.ptimer.enabled_interrupts = NV_PTIMER_INTR_EN_0_ALARM;
    ptimer_write(&d, NV_PTIMER_ALARM_0, TEST_ALARM_LOW, 4);

    make_alarm_four_epochs_overdue(&d);
    g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer), <,
                    ptimer_test_time_ns);

    ptimer_post_load(&d);

    g_assert_cmphex(d.ptimer.pending_interrupts, ==,
                    NV_PTIMER_INTR_0_ALARM);
    g_assert_true(irq_asserted);
    assert_alarm_caught_up(&d);

    ptimer_reset(&d);
}

static void test_intr_read_reconciles_overdue_alarm(void)
{
    NV2AState d;

    init_nv2a_ptimer(&d);
    d.ptimer.enabled_interrupts = NV_PTIMER_INTR_EN_0_ALARM;
    ptimer_write(&d, NV_PTIMER_ALARM_0, TEST_ALARM_LOW, 4);
    make_alarm_four_epochs_overdue(&d);

    g_assert_cmphex(ptimer_read(&d, NV_PTIMER_INTR_0, 4), ==,
                    NV_PTIMER_INTR_0_ALARM);
    g_assert_true(irq_asserted);
    assert_alarm_caught_up(&d);

    ptimer_reset(&d);
}

static void test_intr_enable_reconciles_overdue_alarm(void)
{
    NV2AState d;

    init_nv2a_ptimer(&d);
    ptimer_write(&d, NV_PTIMER_ALARM_0, TEST_ALARM_LOW, 4);
    make_alarm_four_epochs_overdue(&d);

    ptimer_write(&d, NV_PTIMER_INTR_EN_0, NV_PTIMER_INTR_EN_0_ALARM, 4);

    g_assert_cmphex(d.ptimer.pending_interrupts, ==,
                    NV_PTIMER_INTR_0_ALARM);
    g_assert_true(irq_asserted);
    assert_alarm_caught_up(&d);

    ptimer_reset(&d);
}

static void test_zero_ratio_stops_clock_without_division(void)
{
    NV2AState d;

    memset(&d, 0, sizeof(d));
    ptimer_test_time_ns = 0;
    irq_asserted = false;
    d.pramdac.core_clock_freq = NANOSECONDS_PER_SECOND;
    d.pmc.enabled_interrupts = NV_PMC_INTR_EN_0_HARDWARE;
    ptimer_init(&d);

    g_assert_cmphex(ptimer_read(&d, NV_PTIMER_TIME_0, 4), ==, 0);
    g_assert_cmphex(ptimer_read(&d, NV_PTIMER_TIME_1, 4), ==, 0);

    ptimer_write(&d, NV_PTIMER_ALARM_0, TEST_ALARM_LOW, 4);
    g_assert_true(timer_pending(&d.ptimer.timer));
    g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer), ==, INT64_MAX);

    ptimer_write(&d, NV_PTIMER_DENOMINATOR, 1, 4);
    g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer), ==, INT64_MAX);

    ptimer_write(&d, NV_PTIMER_NUMERATOR, 1, 4);
    g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer), <, INT64_MAX);

    ptimer_write(&d, NV_PTIMER_NUMERATOR, 0, 4);
    g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer), ==, INT64_MAX);
    g_assert_cmphex(ptimer_read(&d, NV_PTIMER_NUMERATOR, 4), ==, 0);

    ptimer_write(&d, NV_PTIMER_NUMERATOR, UINT32_MAX, 4);
    ptimer_write(&d, NV_PTIMER_DENOMINATOR, UINT32_MAX, 4);
    g_assert_cmphex(ptimer_read(&d, NV_PTIMER_NUMERATOR, 4), ==,
                    UINT32_MAX);
    g_assert_cmphex(ptimer_read(&d, NV_PTIMER_DENOMINATOR, 4), ==,
                    UINT32_MAX);

    ptimer_write(&d, NV_PTIMER_DENOMINATOR, 0, 4);
    ptimer_post_load(&d);
    g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer), ==, INT64_MAX);

    ptimer_reset(&d);
}

static void test_post_load_rebuilds_irq_without_timer(void)
{
    NV2AState d;

    init_nv2a_ptimer(&d);
    d.ptimer.pending_interrupts = NV_PTIMER_INTR_0_ALARM;
    d.ptimer.enabled_interrupts = NV_PTIMER_INTR_EN_0_ALARM;

    ptimer_post_load(&d);

    g_assert_true(irq_asserted);
    g_assert_cmphex(d.pmc.pending_interrupts & NV_PMC_INTR_0_PTIMER, ==,
                    NV_PMC_INTR_0_PTIMER);

    ptimer_reset(&d);
}

static void test_fractional_deadline(gconstpointer opaque)
{
    NV2AState d;

    init_nv2a_ptimer(&d);
    d.pramdac.core_clock_freq = 233333333;
    d.ptimer.denominator = GPOINTER_TO_UINT(opaque);
    ptimer_write(&d, NV_PTIMER_ALARM_0, 1 << 5, 4);
    /* Both ratios need the first GPU tick, which occurs at 5 ns. */
    g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer), ==, 5);
    expire_alarm(&d);
    g_assert_cmphex(d.ptimer.pending_interrupts, ==, NV_PTIMER_INTR_0_ALARM);
    g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer), >,
                   ptimer_test_time_ns);
    ptimer_reset(&d);
}

static void test_deadline_preserves_clock_phase(void)
{
    NV2AState d;

    init_nv2a_ptimer(&d);
    d.pramdac.core_clock_freq = 100000000;
    ptimer_test_time_ns = 9;
    ptimer_write(&d, NV_PTIMER_ALARM_0, 1 << 5, 4);
    g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer), ==, 10);
    ptimer_reset(&d);

    init_nv2a_ptimer(&d);
    d.pramdac.core_clock_freq = 100000000;
    d.ptimer.numerator = 3;
    d.ptimer.denominator = 2;
    ptimer_test_time_ns = 19; /* G=1, P=0: preserve both remainders. */
    ptimer_write(&d, NV_PTIMER_ALARM_0, 1 << 5, 4);
    g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer), ==, 20);
    ptimer_reset(&d);
}

static void test_deadline_range(void)
{
    NV2AState d;

    init_nv2a_ptimer(&d);
    d.pramdac.core_clock_freq = 1;
    d.ptimer.numerator = UINT32_MAX;
    ptimer_write(&d, NV_PTIMER_ALARM_0, 0xffffffe0, 4);
    g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer), ==, INT64_MAX);
    ptimer_reset(&d);

    init_nv2a_ptimer(&d);
    ptimer_test_time_ns = INT64_MAX - 1;
    ptimer_write(&d, NV_PTIMER_ALARM_0, 0x100, 4);
    g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer), ==, INT64_MAX);
    ptimer_reset(&d);
}

static uint64_t reference_ticks(uint64_t ns, uint64_t frequency,
                                unsigned numerator, unsigned denominator)
{
    /* Test domain keeps these products in 64 bits; no inverse/helper reuse. */
    return ((ns * frequency / 1000000000) * denominator) / numerator;
}

static void test_deadline_forward_oracle(void)
{
    const uint64_t frequencies[] = { 100000000, 233333333, 1000000000 };
    const uint64_t phases[] = { 0, 1, 9, 19, 1234 };
    NV2AState d;
    unsigned cases = 0;

    for (unsigned f = 0; f < G_N_ELEMENTS(frequencies); f++) {
        for (unsigned n = 1; n <= 6; n++) {
            for (unsigned den = 1; den <= 6; den++) {
                for (unsigned p = 0; p < G_N_ELEMENTS(phases); p++) {
                    uint64_t now = phases[p];
                    uint64_t initial = reference_ticks(now, frequencies[f], n, den);
                    for (unsigned distance = 1; distance <= 64; distance++) {
                        uint64_t target = initial + distance;
                        uint64_t expected = now + 1;
                        while (reference_ticks(expected, frequencies[f], n, den)
                               < target) {
                            expected++;
                        }
                        init_nv2a_ptimer(&d);
                        d.pramdac.core_clock_freq = frequencies[f];
                        d.ptimer.numerator = n;
                        d.ptimer.denominator = den;
                        ptimer_test_time_ns = now;
                        ptimer_write(&d, NV_PTIMER_ALARM_0, target << 5, 4);
                        g_assert_cmpuint(timer_expire_time_ns(&d.ptimer.timer),
                                         ==, expected);
                        expire_alarm(&d);
                        g_assert_cmphex(d.ptimer.pending_interrupts, ==,
                                        NV_PTIMER_INTR_0_ALARM);
                        g_assert_cmpuint(timer_expire_time_ns(&d.ptimer.timer),
                                         >, expected);
                        ptimer_reset(&d);
                        cases++;
                    }
                }
            }
        }
    }
    g_assert_cmpuint(cases, ==, 34560);
}

static void test_deadline_wrap_and_stopped_source(void)
{
    NV2AState d;

    init_nv2a_ptimer(&d);
    /* Preserve guest offset and the full 61-bit register wrap. */
    ptimer_write(&d, NV_PTIMER_TIME_1, 0x1fffffff, 4);
    ptimer_write(&d, NV_PTIMER_TIME_0, 0xffffffe0, 4);
    ptimer_write(&d, NV_PTIMER_ALARM_0, 0, 4);
    g_assert_cmpuint(timer_expire_time_ns(&d.ptimer.timer), ==, 1);
    expire_alarm(&d);
    g_assert_cmphex(ptimer_read(&d, NV_PTIMER_TIME_1, 4), ==, 0);
    g_assert_cmphex(ptimer_read(&d, NV_PTIMER_TIME_0, 4), ==, 0);
    g_assert_cmphex(d.ptimer.pending_interrupts, ==, NV_PTIMER_INTR_0_ALARM);
    ptimer_reset(&d);

    init_nv2a_ptimer(&d);
    d.pramdac.core_clock_freq = 0;
    ptimer_write(&d, NV_PTIMER_ALARM_0, 0x100, 4);
    g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer), ==, INT64_MAX);
    d.pramdac.core_clock_freq = 1000000000;
    ptimer_write(&d, NV_PTIMER_NUMERATOR, 1, 4);
    g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer), ==, 8);
    ptimer_reset(&d);
}

static void test_wide_deadline_reconciles_source_wrap(void)
{
    NV2AState d;

    init_nv2a_ptimer(&d);
    d.pramdac.core_clock_freq = UINT32_MAX;
    d.ptimer.numerator = UINT32_MAX;
    ptimer_write(&d, NV_PTIMER_ALARM_0, 0x100, 4);
    /* A restored distant epoch requires a >64-bit GPU-tick quotient. The
     * forward model wraps GPU ticks first, before the alarm can be reached.
     * ceil(2^64 * 1e9 / (2^32 - 1)) = 4294967297000000001.
     */
    d.ptimer.alarm_time = 1ULL << 38;
    ptimer_post_load(&d);
    g_assert_cmpuint(timer_expire_time_ns(&d.ptimer.timer), ==,
                     UINT64_C(4294967297000000001));
    expire_alarm(&d);
    g_assert_cmphex(d.ptimer.pending_interrupts, ==, 0);
    g_assert_cmpuint(timer_expire_time_ns(&d.ptimer.timer), >,
                     UINT64_C(4294967297000000001));
    ptimer_reset(&d);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    for (int i = 0; i < QEMU_CLOCK_MAX; i++) {
        main_loop_tlg.tl[i] = g_new0(QEMUTimerList, 1);
    }
    qtest_allowed = true;

    g_test_add_func("/xbox/nv2a/ptimer/alarm-assert-ack",
                    test_alarm_assert_and_ack);
    g_test_add_func("/xbox/nv2a/ptimer/enable-pending",
                    test_pending_alarm_asserts_when_enabled);
    g_test_add_func("/xbox/nv2a/ptimer/time-registers-epoch",
                    test_time_registers_and_future_epoch);
    g_test_add_func("/xbox/nv2a/ptimer/runtime-overdue",
                    test_runtime_overdue_alarm_skips_missed_epochs);
    g_test_add_func("/xbox/nv2a/ptimer/post-load-overdue",
                    test_post_load_reconciles_overdue_alarm);
    g_test_add_func("/xbox/nv2a/ptimer/intr-read-overdue",
                    test_intr_read_reconciles_overdue_alarm);
    g_test_add_func("/xbox/nv2a/ptimer/intr-enable-overdue",
                    test_intr_enable_reconciles_overdue_alarm);
    g_test_add_func("/xbox/nv2a/ptimer/zero-ratio",
                    test_zero_ratio_stops_clock_without_division);
    g_test_add_func("/xbox/nv2a/ptimer/post-load-irq",
                    test_post_load_rebuilds_irq_without_timer);
    g_test_add_data_func("/xbox/nv2a/ptimer/fractional-deadline/ratio-1-1",
                         GUINT_TO_POINTER(1), test_fractional_deadline);
    g_test_add_data_func("/xbox/nv2a/ptimer/fractional-deadline/ratio-1-2",
                         GUINT_TO_POINTER(2), test_fractional_deadline);
    g_test_add_func("/xbox/nv2a/ptimer/deadline-phase",
                    test_deadline_preserves_clock_phase);
    g_test_add_func("/xbox/nv2a/ptimer/deadline-range", test_deadline_range);
    g_test_add_func("/xbox/nv2a/ptimer/deadline-forward-oracle",
                    test_deadline_forward_oracle);
    g_test_add_func("/xbox/nv2a/ptimer/deadline-wrap-stopped-source",
                    test_deadline_wrap_and_stopped_source);
    g_test_add_func("/xbox/nv2a/ptimer/deadline-wide-source-wrap",
                    test_wide_deadline_reconciles_source_wrap);

    return g_test_run();
}

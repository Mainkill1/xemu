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
    ptimer_test_timer_mod_calls = 0;
    ptimer_test_timer_del_calls = 0;
    irq_asserted = false;

    d->pramdac.core_clock_freq = NANOSECONDS_PER_SECOND;
    d->ptimer.numerator = 1;
    d->ptimer.denominator = 1;
    d->ptimer.enabled_interrupts = NV_PTIMER_INTR_EN_0_ALARM;
    d->pmc.enabled_interrupts = NV_PMC_INTR_EN_0_HARDWARE;
    ptimer_init(d);
}

static void fire_alarm_at(NV2AState *d, int64_t now_ns)
{
    QEMUTimer *timer = &d->ptimer.timer;

    g_assert_true(timer_pending(timer));
    ptimer_test_time_ns = now_ns;
    timer_del(timer);
    g_assert_false(timer_pending(timer));
    timer->cb(timer->opaque);
}

static void expire_alarm(NV2AState *d)
{
    fire_alarm_at(d, timer_expire_time_ns(&d->ptimer.timer));
}

static void make_alarm_four_epochs_overdue(NV2AState *d)
{
    int64_t first_expiry_ns = 8; /* TEST_ALARM_LOW at the 1 GHz 1:1 ratio. */

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
    ptimer_write(&d, NV_PTIMER_INTR_EN_0, 0, 4);
    ptimer_write(&d, NV_PTIMER_ALARM_0, 0x100, 4);
    ptimer_test_time_ns = 8;
    ptimer_read(&d, NV_PTIMER_INTR_0, 4);
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

    ptimer_post_load(&d, 5);

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
    ptimer_write(&d, NV_PTIMER_INTR_EN_0, 0, 4);
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
    d.ptimer.enabled_interrupts = NV_PTIMER_INTR_EN_0_ALARM;

    g_assert_cmphex(ptimer_read(&d, NV_PTIMER_TIME_0, 4), ==, 0);
    g_assert_cmphex(ptimer_read(&d, NV_PTIMER_TIME_1, 4), ==, 0);

    ptimer_write(&d, NV_PTIMER_ALARM_0, TEST_ALARM_LOW, 4);
    g_assert_true(d.ptimer.alarm_armed);
    g_assert_false(timer_pending(&d.ptimer.timer));

    ptimer_write(&d, NV_PTIMER_DENOMINATOR, 1, 4);
    g_assert_false(timer_pending(&d.ptimer.timer));

    ptimer_write(&d, NV_PTIMER_NUMERATOR, 1, 4);
    g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer), <, INT64_MAX);

    ptimer_write(&d, NV_PTIMER_NUMERATOR, 0, 4);
    g_assert_false(timer_pending(&d.ptimer.timer));
    g_assert_cmphex(ptimer_read(&d, NV_PTIMER_NUMERATOR, 4), ==, 0);

    ptimer_write(&d, NV_PTIMER_NUMERATOR, UINT32_MAX, 4);
    ptimer_write(&d, NV_PTIMER_DENOMINATOR, UINT32_MAX, 4);
    g_assert_cmphex(ptimer_read(&d, NV_PTIMER_NUMERATOR, 4), ==,
                    UINT32_MAX);
    g_assert_cmphex(ptimer_read(&d, NV_PTIMER_DENOMINATOR, 4), ==,
                    UINT32_MAX);

    ptimer_write(&d, NV_PTIMER_DENOMINATOR, 0, 4);
    ptimer_post_load(&d, 5);
    g_assert_false(timer_pending(&d.ptimer.timer));

    ptimer_reset(&d);
}

static void test_post_load_rebuilds_irq_without_timer(void)
{
    NV2AState d;

    init_nv2a_ptimer(&d);
    d.ptimer.pending_interrupts = NV_PTIMER_INTR_0_ALARM;
    d.ptimer.enabled_interrupts = NV_PTIMER_INTR_EN_0_ALARM;

    ptimer_post_load(&d, 5);

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
    g_assert_false(timer_pending(&d.ptimer.timer));
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
    ptimer_post_load(&d, 5);
    g_assert_cmpuint(timer_expire_time_ns(&d.ptimer.timer), ==,
                     UINT64_C(4294967297000000001));
    expire_alarm(&d);
    g_assert_cmphex(d.ptimer.pending_interrupts, ==, 0);
    g_assert_cmpuint(timer_expire_time_ns(&d.ptimer.timer), >,
                     UINT64_C(4294967297000000001));
    ptimer_reset(&d);
}

static void test_masked_ack_reconciles_elapsed_alarm(void)
{
    NV2AState d;

    init_nv2a_ptimer(&d);
    ptimer_write(&d, NV_PTIMER_INTR_EN_0, 0, 4);
    ptimer_write(&d, NV_PTIMER_ALARM_0, TEST_ALARM_LOW, 4);
    /* Advance without dispatch: acknowledgment must consume this epoch even
     * when no callback materialized its pending bit. */
    ptimer_test_time_ns = 8;
    ptimer_write(&d, NV_PTIMER_INTR_0, NV_PTIMER_INTR_0_ALARM, 4);
    ptimer_write(&d, NV_PTIMER_INTR_EN_0, NV_PTIMER_INTR_EN_0_ALARM, 4);
    g_assert_cmphex(d.ptimer.pending_interrupts, ==, 0);
    g_assert_false(irq_asserted);
    g_assert_cmphex(d.ptimer.alarm_time, ==,
                   (1ULL << 32) | TEST_ALARM_LOW);
    expire_alarm(&d);
    g_assert_true(irq_asserted);
    ptimer_reset(&d);
}

static void test_stopped_clock_does_not_latch(gconstpointer restore)
{
    NV2AState d;

    init_nv2a_ptimer(&d);
    /* Restore an armed comparator at zero with the source stopped. Preserve
     * pending state; equality alone is not an elapsed running-clock event. */
    d.ptimer.numerator = 0;
    d.ptimer.alarm_time = 0;
    d.ptimer.alarm_armed = true;
    d.ptimer.enabled_interrupts = NV_PTIMER_INTR_EN_0_ALARM;
    timer_mod(&d.ptimer.timer, INT64_MAX);
    if (GPOINTER_TO_INT(restore)) {
        ptimer_post_load(&d, 5);
    } else {
        ptimer_read(&d, NV_PTIMER_INTR_0, 4);
    }
    g_assert_cmphex(d.ptimer.pending_interrupts, ==, 0);
    g_assert_false(irq_asserted);
    d.ptimer.pending_interrupts = NV_PTIMER_INTR_0_ALARM;
    ptimer_post_load(&d, 5);
    g_assert_cmphex(d.ptimer.pending_interrupts, ==, NV_PTIMER_INTR_0_ALARM);
    g_assert_true(irq_asserted);
    ptimer_reset(&d);
}

static void test_masked_alarm_has_no_callback(void)
{
    NV2AState d;

    init_nv2a_ptimer(&d);
    ptimer_write(&d, NV_PTIMER_INTR_EN_0, 0, 4);
    ptimer_write(&d, NV_PTIMER_ALARM_0, TEST_ALARM_LOW, 4);
    g_assert_false(timer_pending(&d.ptimer.timer));
    ptimer_test_time_ns = 4 * PTIMER_REG_EPOCH_NS + 1008;
    g_assert_cmphex(ptimer_read(&d, NV_PTIMER_INTR_0, 4), ==,
                   NV_PTIMER_INTR_0_ALARM);
    g_assert_false(timer_pending(&d.ptimer.timer));
    g_assert_false(irq_asserted);
    ptimer_write(&d, NV_PTIMER_INTR_EN_0, NV_PTIMER_INTR_EN_0_ALARM, 4);
    g_assert_true(irq_asserted);
    assert_alarm_caught_up(&d);
    ptimer_write(&d, NV_PTIMER_INTR_EN_0, 0, 4);
    g_assert_false(timer_pending(&d.ptimer.timer));
    ptimer_reset(&d);
}

static void test_mask_unmask_before_expiry_requeues_alarm(void)
{
    NV2AState d;

    init_nv2a_ptimer(&d);
    ptimer_write(&d, NV_PTIMER_ALARM_0, TEST_ALARM_LOW, 4);
    g_assert_true(timer_pending(&d.ptimer.timer));
    g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer), ==, 8);

    ptimer_write(&d, NV_PTIMER_INTR_EN_0, 0, 4);
    g_assert_false(timer_pending(&d.ptimer.timer));
    g_assert_cmphex(d.ptimer.pending_interrupts, ==, 0);

    ptimer_test_time_ns = 4;
    ptimer_write(&d, NV_PTIMER_INTR_EN_0, NV_PTIMER_INTR_EN_0_ALARM, 4);
    g_assert_true(timer_pending(&d.ptimer.timer));
    g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer), ==, 8);
    g_assert_cmphex(d.ptimer.pending_interrupts, ==, 0);
    g_assert_false(irq_asserted);

    expire_alarm(&d);
    g_assert_cmphex(d.ptimer.pending_interrupts, ==,
                    NV_PTIMER_INTR_0_ALARM);
    g_assert_true(irq_asserted);
    ptimer_reset(&d);
}

static void test_rate_changes_keep_masked_state(void)
{
    NV2AState d;

    init_nv2a_ptimer(&d);
    ptimer_write(&d, NV_PTIMER_ALARM_0, TEST_ALARM_LOW, 4);
    ptimer_test_time_ns = 4;
    ptimer_set_core_clock(&d, 500000000);
    g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer), ==, 16);
    ptimer_set_core_clock(&d, 0);
    g_assert_false(timer_pending(&d.ptimer.timer));
    g_assert_true(d.ptimer.alarm_armed);
    ptimer_write(&d, NV_PTIMER_INTR_EN_0, 0, 4);
    ptimer_test_time_ns = 20;
    ptimer_set_core_clock(&d, 1000000000);
    g_assert_false(timer_pending(&d.ptimer.timer));
    ptimer_write(&d, NV_PTIMER_INTR_0, NV_PTIMER_INTR_0_ALARM, 4);
    ptimer_write(&d, NV_PTIMER_INTR_EN_0, NV_PTIMER_INTR_EN_0_ALARM, 4);
    g_assert_false(irq_asserted);
    g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer), >, 20);
    ptimer_reset(&d);
    g_assert_false(d.ptimer.alarm_armed);
    g_assert_false(timer_pending(&d.ptimer.timer));
}

static void test_restore_alarm_versions(void)
{
    NV2AState d;

    /* v4: the serialized timer represented armed state even while masked. */
    init_nv2a_ptimer(&d);
    d.ptimer.enabled_interrupts = 0;
    d.ptimer.alarm_time = TEST_ALARM_LOW;
    timer_mod(&d.ptimer.timer, 8);
    ptimer_test_time_ns = 10;
    ptimer_post_load(&d, 4);
    g_assert_true(d.ptimer.alarm_armed);
    g_assert_false(timer_pending(&d.ptimer.timer));
    g_assert_cmphex(d.ptimer.pending_interrupts, ==, NV_PTIMER_INTR_0_ALARM);
    ptimer_write(&d, NV_PTIMER_INTR_0, NV_PTIMER_INTR_0_ALARM, 4);

    /* v5: no timer is queued, but the serialized armed bit survives. */
    ptimer_post_load(&d, 5);
    g_assert_true(d.ptimer.alarm_armed);
    g_assert_false(timer_pending(&d.ptimer.timer));
    ptimer_write(&d, NV_PTIMER_INTR_EN_0, NV_PTIMER_INTR_EN_0_ALARM, 4);
    g_assert_true(timer_pending(&d.ptimer.timer));
    g_assert_false(irq_asserted);

    /* Pre-load resets fields absent from v1-v3 before reading the stream. */
    ptimer_reset(&d);
    d.ptimer.pending_interrupts = NV_PTIMER_INTR_0_ALARM;
    ptimer_post_load(&d, 3);
    g_assert_false(d.ptimer.alarm_armed);
    g_assert_false(timer_pending(&d.ptimer.timer));
    g_assert_true(irq_asserted);
    ptimer_reset(&d);

    /* An unarmed v4 timer must not arm the reset-zero comparator. */
    init_nv2a_ptimer(&d);
    ptimer_post_load(&d, 4);
    g_assert_false(d.ptimer.alarm_armed);
    g_assert_false(irq_asserted);
    ptimer_reset(&d);
}

static void test_pll_register_reschedules_alarm(void)
{
    NV2AState d;

    init_nv2a_ptimer(&d);
    ptimer_write(&d, NV_PTIMER_ALARM_0, TEST_ALARM_LOW, 4);
    pramdac_write(&d, NV_PRAMDAC_NVPLL_COEFF, 0x601, 4);
    g_assert_cmpuint(pramdac_read(&d, NV_PRAMDAC_NVPLL_COEFF, 4), ==, 0x601);
    g_assert_cmpuint(d.pramdac.core_clock_freq, ==, 99999996);
    g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer), ==, 81);
    ptimer_test_time_ns = 20;
    pramdac_write(&d, NV_PRAMDAC_NVPLL_COEFF, 0x301, 4);
    g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer), ==, 161);
    ptimer_test_time_ns = 161;
    pramdac_write(&d, NV_PRAMDAC_NVPLL_COEFF, 0, 4);
    g_assert_false(timer_pending(&d.ptimer.timer));
    g_assert_true(irq_asserted); /* Elapsed under the old rate before stopping. */
    ptimer_write(&d, NV_PTIMER_INTR_0, NV_PTIMER_INTR_0_ALARM, 4);
    pramdac_write(&d, NV_PRAMDAC_NVPLL_COEFF, 0x601, 4);
    g_assert_false(irq_asserted);
    g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer), >, 161);
    ptimer_reset(&d);
}

enum TimebaseChange {
    CHANGE_CORE,
    CHANGE_NUMERATOR,
    CHANGE_DENOMINATOR,
    CHANGE_TIME_LOW,
    CHANGE_TIME_HIGH,
    CHANGE_CORE_RESTART,
    CHANGE_PLL,
    CHANGE_NUMERATOR_RESTART,
    CHANGE_DENOMINATOR_RESTART,
    TIMEBASE_CHANGE_COUNT,
};

enum TimebaseObservation {
    OBSERVE_POLL,
    OBSERVE_CALLBACK,
    OBSERVE_MASKED_POLL,
    OBSERVE_ALREADY_PENDING,
    OBSERVE_MASKED_ACK,
    TIMEBASE_OBSERVATION_COUNT,
};

/* Characterize the current absolute-time mapping and observation order.
 * A due-now timer after crossing an alarm is already overdue; this is not the
 * positive-future-distance rounding bug. These assertions do not establish
 * whether Xbox hardware publishes an IRQ synchronously on a timebase write.
 */
static void test_timebase_crosses_alarm_characterization(gconstpointer opaque)
{
    unsigned int scenario = GPOINTER_TO_UINT(opaque);
    enum TimebaseChange change = scenario / TIMEBASE_OBSERVATION_COUNT;
    enum TimebaseObservation observation = scenario % TIMEBASE_OBSERVATION_COUNT;
    bool masked = observation == OBSERVE_MASKED_POLL ||
                  observation == OBSERVE_MASKED_ACK;
    uint32_t initial_pending = observation == OBSERVE_ALREADY_PENDING ?
                              NV_PTIMER_INTR_0_ALARM : 0;
    NV2AState d;

    init_nv2a_ptimer(&d);
    d.pramdac.core_clock_freq = 100000000;
    if (change == CHANGE_NUMERATOR || change == CHANGE_NUMERATOR_RESTART) {
        d.pramdac.core_clock_freq = 1000000000;
        d.ptimer.numerator = 10;
    }
    ptimer_test_time_ns = 100;
    ptimer_write(&d, NV_PTIMER_ALARM_0, 0x1e0, 4);
    g_assert_cmphex(ptimer_read(&d, NV_PTIMER_TIME_0, 4), ==, 0x140);
    g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer), ==, 150);
    if (masked) {
        ptimer_write(&d, NV_PTIMER_INTR_EN_0, 0, 4);
    }
    d.ptimer.pending_interrupts = initial_pending;
    nv2a_update_irq(&d);

    switch (change) {
    case CHANGE_CORE:
        ptimer_set_core_clock(&d, 1000000000);
        break;
    case CHANGE_NUMERATOR:
        ptimer_write(&d, NV_PTIMER_NUMERATOR, 1, 4);
        break;
    case CHANGE_DENOMINATOR:
        ptimer_write(&d, NV_PTIMER_DENOMINATOR, 10, 4);
        break;
    case CHANGE_TIME_LOW:
        ptimer_write(&d, NV_PTIMER_TIME_0, 0xc80, 4);
        break;
    case CHANGE_TIME_HIGH:
        ptimer_write(&d, NV_PTIMER_TIME_1, 1, 4);
        break;
    case CHANGE_CORE_RESTART:
        ptimer_set_core_clock(&d, 0);
        g_assert_false(timer_pending(&d.ptimer.timer));
        ptimer_set_core_clock(&d, 1000000000);
        break;
    case CHANGE_PLL:
        pramdac_write(&d, NV_PRAMDAC_NVPLL_COEFF, 0x3c01, 4);
        g_assert_cmpuint(d.pramdac.core_clock_freq, ==, 999999960);
        break;
    case CHANGE_NUMERATOR_RESTART:
        ptimer_write(&d, NV_PTIMER_NUMERATOR, 0, 4);
        g_assert_false(timer_pending(&d.ptimer.timer));
        ptimer_write(&d, NV_PTIMER_NUMERATOR, 1, 4);
        break;
    case CHANGE_DENOMINATOR_RESTART:
        ptimer_write(&d, NV_PTIMER_DENOMINATOR, 0, 4);
        g_assert_false(timer_pending(&d.ptimer.timer));
        ptimer_write(&d, NV_PTIMER_DENOMINATOR, 10, 4);
        break;
    default:
        g_assert_not_reached();
    }

    /* The mapping changed, but no callback has run and no status was polled. */
    g_assert_true(d.ptimer.alarm_armed);
    g_assert_cmphex(d.ptimer.alarm_time, ==, 0x1e0);
    g_assert_cmphex(d.ptimer.pending_interrupts, ==, initial_pending);
    g_assert_cmpint(timer_pending(&d.ptimer.timer), ==, !masked);
    g_assert_cmpint(irq_asserted, ==, initial_pending != 0);
    if (!masked) {
        g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer), ==, 100);
    }

    if (observation == OBSERVE_MASKED_ACK) {
        /* Reconcile before write-one-to-clear, then do not resurrect it. */
        ptimer_write(&d, NV_PTIMER_INTR_0, NV_PTIMER_INTR_0_ALARM, 4);
        g_assert_cmphex(ptimer_read(&d, NV_PTIMER_INTR_0, 4), ==, 0);
        g_assert_false(irq_asserted);
    } else {
        if (observation == OBSERVE_CALLBACK) {
            fire_alarm_at(&d, ptimer_test_time_ns);
        }
        g_assert_cmphex(ptimer_read(&d, NV_PTIMER_INTR_0, 4), ==,
                        NV_PTIMER_INTR_0_ALARM);
        g_assert_cmpint(irq_asserted, ==, !masked);
    }
    g_assert_cmphex(d.ptimer.alarm_time, >, 0x1e0);
    if (masked) {
        g_assert_false(timer_pending(&d.ptimer.timer));
        ptimer_write(&d, NV_PTIMER_INTR_EN_0, NV_PTIMER_INTR_EN_0_ALARM, 4);
        g_assert_cmpint(irq_asserted, ==, observation != OBSERVE_MASKED_ACK);
    }
    g_assert_true(timer_pending(&d.ptimer.timer));
    g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer), >,
                    ptimer_test_time_ns);
    ptimer_reset(&d);
}

static void test_timebase_backward_characterization(void)
{
    NV2AState d;

    init_nv2a_ptimer(&d);
    d.pramdac.core_clock_freq = 100000000;
    ptimer_test_time_ns = 100;
    ptimer_write(&d, NV_PTIMER_ALARM_0, 0x1e0, 4);
    ptimer_write(&d, NV_PTIMER_TIME_0, 0x80, 4);
    g_assert_cmphex(ptimer_read(&d, NV_PTIMER_TIME_0, 4), ==, 0x80);
    g_assert_cmphex(ptimer_read(&d, NV_PTIMER_INTR_0, 4), ==, 0);
    g_assert_false(irq_asserted);
    g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer), ==, 210);
    expire_alarm(&d);
    g_assert_true(irq_asserted);
    ptimer_reset(&d);
}

bool ptimer_test_bql_locked(void)
{
    return true;
}

static void test_ack_keeps_future_schedule(void)
{
    NV2AState d;

    init_nv2a_ptimer(&d);
    ptimer_write(&d, NV_PTIMER_INTR_EN_0, NV_PTIMER_INTR_EN_0_ALARM, 4);
    ptimer_write(&d, NV_PTIMER_ALARM_0, TEST_ALARM_LOW, 4);
    ptimer_test_time_ns = 1;
    uint64_t mods = ptimer_test_timer_mod_calls;
    uint64_t dels = ptimer_test_timer_del_calls;
    int64_t deadline = timer_expire_time_ns(&d.ptimer.timer);

    for (unsigned i = 0; i < 1000; i++) {
        ptimer_write(&d, NV_PTIMER_INTR_0, NV_PTIMER_INTR_0_ALARM, 4);
    }

    g_assert_cmpuint(ptimer_test_timer_mod_calls, ==, mods);
    g_assert_cmpuint(ptimer_test_timer_del_calls, ==, dels);
    g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer), ==, deadline);
    g_assert_true(timer_pending(&d.ptimer.timer));
    g_assert_false(irq_asserted);
    ptimer_reset(&d);
}

static void test_masked_ack_keeps_absent_schedule(void)
{
    NV2AState d;

    init_nv2a_ptimer(&d);
    ptimer_write(&d, NV_PTIMER_ALARM_0, TEST_ALARM_LOW, 4);
    uint64_t dels = ptimer_test_timer_del_calls;

    for (unsigned i = 0; i < 1000; i++) {
        ptimer_write(&d, NV_PTIMER_INTR_0, NV_PTIMER_INTR_0_ALARM, 4);
    }

    g_assert_cmpuint(ptimer_test_timer_del_calls, ==, dels);
    g_assert_false(timer_pending(&d.ptimer.timer));
    ptimer_reset(&d);
}

static void test_overdue_ack_rearms(void)
{
    NV2AState d;

    init_nv2a_ptimer(&d);
    ptimer_write(&d, NV_PTIMER_INTR_EN_0, NV_PTIMER_INTR_EN_0_ALARM, 4);
    ptimer_write(&d, NV_PTIMER_ALARM_0, TEST_ALARM_LOW, 4);
    ptimer_test_time_ns = 8;
    uint64_t mods = ptimer_test_timer_mod_calls;

    ptimer_write(&d, NV_PTIMER_INTR_0, NV_PTIMER_INTR_0_ALARM, 4);

    g_assert_cmphex(d.ptimer.pending_interrupts, ==, 0);
    g_assert_cmphex(d.ptimer.alarm_time, ==,
                    (1ULL << 32) | TEST_ALARM_LOW);
    g_assert_cmpuint(ptimer_test_timer_mod_calls, ==, mods + 1);
    g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer), >, 8);
    ptimer_reset(&d);
}

static void test_callback_consumption_rearms(void)
{
    NV2AState d;

    init_nv2a_ptimer(&d);
    ptimer_write(&d, NV_PTIMER_INTR_EN_0, NV_PTIMER_INTR_EN_0_ALARM, 4);
    ptimer_write(&d, NV_PTIMER_ALARM_0, TEST_ALARM_LOW, 4);
    uint64_t mods = ptimer_test_timer_mod_calls;

    expire_alarm(&d);

    g_assert_true(irq_asserted);
    g_assert_true(timer_pending(&d.ptimer.timer));
    g_assert_cmpuint(ptimer_test_timer_mod_calls, ==, mods + 1);
    g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer), >,
                    ptimer_test_time_ns);
    ptimer_reset(&d);
}

static void test_restore_rebuilds_host_schedule(void)
{
    NV2AState d;

    init_nv2a_ptimer(&d);
    ptimer_write(&d, NV_PTIMER_INTR_EN_0, NV_PTIMER_INTR_EN_0_ALARM, 4);
    ptimer_write(&d, NV_PTIMER_ALARM_0, TEST_ALARM_LOW, 4);
    uint64_t mods = ptimer_test_timer_mod_calls;

    ptimer_post_load(&d, 5);

    g_assert_cmpuint(ptimer_test_timer_mod_calls, ==, mods + 1);
    g_assert_true(timer_pending(&d.ptimer.timer));
    g_assert_cmpint(d.ptimer.host.state, ==, PTIMER_HOST_QUEUED);
    g_assert_false(d.ptimer.host.dirty);
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

    g_test_add_func("/xbox/nv2a/ptimer/masked-ack",
                    test_masked_ack_reconciles_elapsed_alarm);
    g_test_add_data_func("/xbox/nv2a/ptimer/stopped-read",
                        GINT_TO_POINTER(0), test_stopped_clock_does_not_latch);
    g_test_add_data_func("/xbox/nv2a/ptimer/stopped-restore",
                        GINT_TO_POINTER(1), test_stopped_clock_does_not_latch);
    g_test_add_func("/xbox/nv2a/ptimer/masked-no-callback",
                    test_masked_alarm_has_no_callback);
    g_test_add_func("/xbox/nv2a/ptimer/mask-unmask-before-expiry",
                    test_mask_unmask_before_expiry_requeues_alarm);
    g_test_add_func("/xbox/nv2a/ptimer/rate-changes-masked",
                    test_rate_changes_keep_masked_state);
    g_test_add_func("/xbox/nv2a/ptimer/restore-versions",
                    test_restore_alarm_versions);
    g_test_add_func("/xbox/nv2a/ptimer/pll-register",
                    test_pll_register_reschedules_alarm);
    g_test_add_func("/xbox/nv2a/ptimer/ack-keeps-future-schedule",
                    test_ack_keeps_future_schedule);
    g_test_add_func("/xbox/nv2a/ptimer/masked-ack-keeps-absent",
                    test_masked_ack_keeps_absent_schedule);
    g_test_add_func("/xbox/nv2a/ptimer/overdue-ack-rearms",
                    test_overdue_ack_rearms);
    g_test_add_func("/xbox/nv2a/ptimer/callback-consumption-rearms",
                    test_callback_consumption_rearms);
    g_test_add_func("/xbox/nv2a/ptimer/restore-rebuilds-host-schedule",
                    test_restore_rebuilds_host_schedule);
    const char *changes[TIMEBASE_CHANGE_COUNT] = {
        "core", "numerator", "denominator", "time-low", "time-high",
        "core-restart", "pll-register", "numerator-restart", "denominator-restart",
    };
    const char *observations[TIMEBASE_OBSERVATION_COUNT] = {
        "poll", "callback", "masked-poll", "already-pending", "masked-ack",
    };
    for (unsigned int change = 0; change < TIMEBASE_CHANGE_COUNT; change++) {
        for (unsigned int observation = 0; observation < TIMEBASE_OBSERVATION_COUNT;
             observation++) {
            g_autofree char *path = g_strdup_printf(
                "/xbox/nv2a/ptimer/timebase-characterization/%s/%s",
                changes[change], observations[observation]);
            g_test_add_data_func(path,
                GUINT_TO_POINTER(change * TIMEBASE_OBSERVATION_COUNT + observation),
                test_timebase_crosses_alarm_characterization);
        }
    }
    g_test_add_func("/xbox/nv2a/ptimer/timebase-characterization/backward",
                    test_timebase_backward_characterization);
    return g_test_run();
}

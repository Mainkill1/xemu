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
    /* The alarm can be masked, so derive its first occurrence from guest
     * state rather than an intentionally absent host timer (1 GHz, 1/1). */
    int64_t first_expiry_ns = (d->ptimer.alarm_time & 0xffffffffULL) >> 5;

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
    g_assert_false(timer_pending(&d.ptimer.timer));
    g_assert_false(irq_asserted);

    ptimer_test_time_ns = 8;
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

    ptimer_write(&d, NV_PTIMER_INTR_EN_0, NV_PTIMER_INTR_EN_0_ALARM, 4);
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

    ptimer_write(&d, NV_PTIMER_INTR_EN_0, NV_PTIMER_INTR_EN_0_ALARM, 4);
    ptimer_write(&d, NV_PTIMER_ALARM_0, TEST_ALARM_LOW, 4);
    g_assert_true(d.ptimer.alarm_armed);
    g_assert_false(timer_pending(&d.ptimer.timer));

    ptimer_write(&d, NV_PTIMER_DENOMINATOR, 1, 4);
    g_assert_true(d.ptimer.alarm_armed);
    g_assert_false(timer_pending(&d.ptimer.timer));

    ptimer_write(&d, NV_PTIMER_NUMERATOR, 1, 4);
    g_assert_true(timer_pending(&d.ptimer.timer));
    g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer), <, INT64_MAX);

    ptimer_write(&d, NV_PTIMER_NUMERATOR, 0, 4);
    g_assert_true(d.ptimer.alarm_armed);
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
    g_assert_true(d.ptimer.alarm_armed);
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

/* Restored semantic controls from #59/#81 plus post-#120 v4 recovery.
 * These exercise production functions; post-load cases are not VMState streams.
 */
static void test_masked_expiry_ack_before_unmask(void)
{
    NV2AState d;
    init_nv2a_ptimer(&d);
    ptimer_write(&d, NV_PTIMER_ALARM_0, TEST_ALARM_LOW, 4);
    ptimer_test_time_ns = 8;

    /* A status read here would hide the missing pre-W1C reconciliation. */
    ptimer_write(&d, NV_PTIMER_INTR_0, NV_PTIMER_INTR_0_ALARM, 4);
    ptimer_write(&d, NV_PTIMER_INTR_EN_0, NV_PTIMER_INTR_EN_0_ALARM, 4);
    g_assert_cmphex(d.ptimer.pending_interrupts, ==, 0);
    g_assert_false(irq_asserted);
    g_assert_true(timer_pending(&d.ptimer.timer));
    g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer), >, 8);
    ptimer_reset(&d);
}

static void test_enabled_expiry_ack_before_callback(void)
{
    NV2AState d;
    init_nv2a_ptimer(&d);
    ptimer_write(&d, NV_PTIMER_INTR_EN_0, NV_PTIMER_INTR_EN_0_ALARM, 4);
    ptimer_write(&d, NV_PTIMER_ALARM_0, TEST_ALARM_LOW, 4);
    ptimer_test_time_ns = 8;
    ptimer_write(&d, NV_PTIMER_INTR_0, NV_PTIMER_INTR_0_ALARM, 4);
    g_assert_cmphex(d.ptimer.pending_interrupts, ==, 0);
    g_assert_false(irq_asserted);
    g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer), >, 8);
    ptimer_reset(&d);
}

static void test_masked_alarm_replacement_keeps_pending(void)
{
    NV2AState d;
    init_nv2a_ptimer(&d);
    ptimer_write(&d, NV_PTIMER_ALARM_0, TEST_ALARM_LOW, 4);
    ptimer_test_time_ns = 8;
    ptimer_write(&d, NV_PTIMER_ALARM_0, 0x200, 4);
    g_assert_cmphex(d.ptimer.pending_interrupts, ==, NV_PTIMER_INTR_0_ALARM);
    g_assert_false(timer_pending(&d.ptimer.timer));
    ptimer_write(&d, NV_PTIMER_INTR_EN_0, NV_PTIMER_INTR_EN_0_ALARM, 4);
    g_assert_true(irq_asserted);
    g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer), ==, 16);
    ptimer_reset(&d);
}

static void arm_wrapped_zero(NV2AState *d)
{
    init_nv2a_ptimer(d);
    ptimer_write(d, NV_PTIMER_TIME_1, 0x1fffffff, 4);
    ptimer_write(d, NV_PTIMER_TIME_0, 0xffffffe0, 4);
    ptimer_write(d, NV_PTIMER_INTR_EN_0, NV_PTIMER_INTR_EN_0_ALARM, 4);
    ptimer_write(d, NV_PTIMER_ALARM_0, 0, 4);
}

static void test_full_counter_wrap_zero_alarm(void)
{
    NV2AState d;
    arm_wrapped_zero(&d);
    g_assert_true(d.ptimer.alarm_armed);
    g_assert_cmphex(d.ptimer.alarm_time, ==, 0);
    g_assert_true(timer_pending(&d.ptimer.timer));
    g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer), ==, 1);
    expire_alarm(&d);
    g_assert_true(irq_asserted);
    ptimer_reset(&d);
}

static void test_phase_correct_first_tick(void)
{
    NV2AState d;
    init_nv2a_ptimer(&d);
    d.pramdac.core_clock_freq = 100000000;
    ptimer_test_time_ns = 9;
    ptimer_write(&d, NV_PTIMER_INTR_EN_0, NV_PTIMER_INTR_EN_0_ALARM, 4);
    ptimer_write(&d, NV_PTIMER_ALARM_0, 32, 4);
    g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer), ==, 10);
    g_assert_cmphex(ptimer_read(&d, NV_PTIMER_TIME_0, 4), ==, 0);
    expire_alarm(&d);
    g_assert_cmphex(ptimer_read(&d, NV_PTIMER_TIME_0, 4), ==, 32);
    g_assert_true(irq_asserted);
    ptimer_reset(&d);
}

/* Bounded inputs below fit in 64 bits, providing an independent forward
 * oracle without calling the production inverse or its wide helpers. */
static uint64_t forward_test_ticks(uint64_t ns, uint64_t frequency,
                                   uint32_t numerator, uint32_t denominator)
{
    uint64_t gpu = ns * frequency / NANOSECONDS_PER_SECOND;
    return gpu * denominator / numerator;
}

static void test_phase_deadline_forward_oracle(void)
{
    const uint64_t frequencies[] = { 100000000, 233333333, 1000000000 };
    unsigned int cases = 0;

    for (size_t f = 0; f < G_N_ELEMENTS(frequencies); f++) {
        for (uint32_t n = 1; n <= 6; n++) {
            for (uint32_t den = 1; den <= 6; den++) {
                for (int64_t now = 0; now < 32; now++) {
                    for (uint64_t ticks = 1; ticks <= 16; ticks++) {
                        NV2AState d;
                        init_nv2a_ptimer(&d);
                        d.pramdac.core_clock_freq = frequencies[f];
                        d.ptimer.numerator = n;
                        d.ptimer.denominator = den;
                        ptimer_test_time_ns = now;
                        uint64_t target = forward_test_ticks(
                            now, frequencies[f], n, den) + ticks;
                        ptimer_write(&d, NV_PTIMER_INTR_EN_0,
                                     NV_PTIMER_INTR_EN_0_ALARM, 4);
                        ptimer_write(&d, NV_PTIMER_ALARM_0, target << 5, 4);
                        int64_t deadline = timer_expire_time_ns(&d.ptimer.timer);
                        g_assert_cmpint(deadline, >, now);
                        g_assert_cmpuint(forward_test_ticks(
                            deadline, frequencies[f], n, den), >=, target);
                        g_assert_cmpuint(forward_test_ticks(
                            deadline - 1, frequencies[f], n, den), <, target);
                        expire_alarm(&d);
                        g_assert_true(irq_asserted);
                        g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer),
                                        >, ptimer_test_time_ns);
                        ptimer_reset(&d);
                        cases++;
                    }
                }
            }
        }
    }
    g_assert_cmpuint(cases, ==, 55296);
}

static void test_ack_keeps_valid_schedule(void)
{
    NV2AState d;
    init_nv2a_ptimer(&d);
    ptimer_write(&d, NV_PTIMER_INTR_EN_0, NV_PTIMER_INTR_EN_0_ALARM, 4);
    ptimer_write(&d, NV_PTIMER_ALARM_0, TEST_ALARM_LOW, 4);
    uint64_t calls = ptimer_test_timer_mod_calls;
    ptimer_test_time_ns = 1;
    ptimer_write(&d, NV_PTIMER_INTR_0, NV_PTIMER_INTR_0_ALARM, 4);
    g_assert_cmpuint(ptimer_test_timer_mod_calls, ==, calls);
    g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer), ==, 8);
    ptimer_reset(&d);
}

static void test_core_clock_change_rebuilds_deadline(void)
{
    NV2AState d;
    init_nv2a_ptimer(&d);
    ptimer_write(&d, NV_PTIMER_INTR_EN_0, NV_PTIMER_INTR_EN_0_ALARM, 4);
    ptimer_write(&d, NV_PTIMER_ALARM_0, TEST_ALARM_LOW, 4);
    ptimer_set_core_clock(&d, 100000000);
    g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer), ==, 80);
    ptimer_set_core_clock(&d, 0);
    g_assert_true(d.ptimer.alarm_armed);
    g_assert_false(timer_pending(&d.ptimer.timer));
    ptimer_set_core_clock(&d, NANOSECONDS_PER_SECOND);
    g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer), ==, 8);
    ptimer_test_time_ns = 8;
    ptimer_set_core_clock(&d, 100000000);
    g_assert_true(irq_asserted);
    g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer), >, 8);
    ptimer_reset(&d);
}

static void test_stopped_equality_preserves_pending(void)
{
    NV2AState d;
    arm_wrapped_zero(&d);
    ptimer_write(&d, NV_PTIMER_NUMERATOR, 0, 4);
    ptimer_write(&d, NV_PTIMER_TIME_1, 0, 4);
    ptimer_write(&d, NV_PTIMER_TIME_0, 0, 4);
    g_assert_cmphex(ptimer_read(&d, NV_PTIMER_INTR_0, 4), ==, 0);
    ptimer_post_load(&d, 5);
    g_assert_true(d.ptimer.alarm_armed);
    g_assert_false(timer_pending(&d.ptimer.timer));
    g_assert_false(irq_asserted);
    d.ptimer.pending_interrupts = NV_PTIMER_INTR_0_ALARM;
    ptimer_post_load(&d, 5);
    g_assert_true(irq_asserted);
    ptimer_reset(&d);
}

static void test_v4_masked_restore_preserves_alarm(void)
{
    NV2AState d;
    init_nv2a_ptimer(&d);
    ptimer_write(&d, NV_PTIMER_ALARM_0, TEST_ALARM_LOW, 4);
    d.ptimer.alarm_armed = false; /* Absent from the v4 stream. */
    g_assert_false(timer_pending(&d.ptimer.timer));
    ptimer_post_load(&d, 4);
    g_assert_true(d.ptimer.alarm_armed);
    g_assert_false(timer_pending(&d.ptimer.timer));
    ptimer_test_time_ns = 8;
    ptimer_post_load(&d, 4);
    g_assert_cmphex(d.ptimer.pending_interrupts, ==, NV_PTIMER_INTR_0_ALARM);
    g_assert_false(irq_asserted);
    g_assert_false(timer_pending(&d.ptimer.timer));
    ptimer_write(&d, NV_PTIMER_INTR_EN_0, NV_PTIMER_INTR_EN_0_ALARM, 4);
    g_assert_true(irq_asserted);
    ptimer_reset(&d);
}

static void test_v4_queued_zero_restore(void)
{
    NV2AState d;
    arm_wrapped_zero(&d);
    d.ptimer.alarm_armed = false;
    ptimer_post_load(&d, 4);
    g_assert_true(d.ptimer.alarm_armed);
    g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer), ==, 1);
    ptimer_reset(&d);
}

static void test_v5_zero_restore_without_host_timer(void)
{
    NV2AState d;
    arm_wrapped_zero(&d);
    timer_del(&d.ptimer.timer);
    ptimer_post_load(&d, 5);
    g_assert_true(d.ptimer.alarm_armed);
    g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer), ==, 1);
    ptimer_reset(&d);
}

static void test_v5_disarmed_restore_stays_disarmed(void)
{
    NV2AState d;
    init_nv2a_ptimer(&d);
    d.ptimer.alarm_time = TEST_ALARM_LOW;
    d.ptimer.enabled_interrupts = NV_PTIMER_INTR_EN_0_ALARM;
    ptimer_post_load(&d, 5);
    g_assert_false(d.ptimer.alarm_armed);
    g_assert_false(timer_pending(&d.ptimer.timer));
    g_assert_false(irq_asserted);
    ptimer_reset(&d);
}

static void test_pre_v4_restore_discards_old_alarm(void)
{
    NV2AState d;
    arm_wrapped_zero(&d);
    d.ptimer.pending_interrupts = NV_PTIMER_INTR_0_ALARM;
    ptimer_post_load(&d, 3);
    g_assert_false(d.ptimer.alarm_armed);
    g_assert_cmphex(d.ptimer.alarm_time, ==, 0);
    g_assert_cmphex(d.ptimer.time_offset, ==, 0);
    g_assert_false(timer_pending(&d.ptimer.timer));
    g_assert_true(irq_asserted); /* The old stream does contain pending state. */
    ptimer_reset(&d);
}

static void test_deadline_saturates_signed_horizon(void)
{
    NV2AState d;
    init_nv2a_ptimer(&d);
    d.pramdac.core_clock_freq = 1;
    d.ptimer.numerator = UINT32_MAX;
    ptimer_write(&d, NV_PTIMER_INTR_EN_0, NV_PTIMER_INTR_EN_0_ALARM, 4);
    ptimer_write(&d, NV_PTIMER_ALARM_0, 0xffffffe0, 4);
    g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer), ==, INT64_MAX);
    g_assert_false(irq_asserted);
    ptimer_reset(&d);
}

static void test_source_counter_wrap_revalidates_alarm(void)
{
    NV2AState d;
    init_nv2a_ptimer(&d);
    d.pramdac.core_clock_freq = 4000000000ULL;
    int64_t start = (1LL << 62) - 1;
    ptimer_test_time_ns = start;
    ptimer_write(&d, NV_PTIMER_INTR_EN_0, NV_PTIMER_INTR_EN_0_ALARM, 4);
    ptimer_write(&d, NV_PTIMER_ALARM_0, 0x180, 4);
    g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer), ==, start + 1);
    expire_alarm(&d);
    g_assert_false(irq_asserted);
    g_assert_cmpint(timer_expire_time_ns(&d.ptimer.timer), ==, start + 4);
    expire_alarm(&d);
    g_assert_true(irq_asserted);
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

    g_test_add_func("/xbox/nv2a/ptimer/reconcile/masked-expiry-ack-before-unmask",
                    test_masked_expiry_ack_before_unmask);
    g_test_add_func("/xbox/nv2a/ptimer/reconcile/enabled-expiry-ack-before-callback",
                    test_enabled_expiry_ack_before_callback);
    g_test_add_func("/xbox/nv2a/ptimer/reconcile/masked-alarm-replacement-keeps-pending",
                    test_masked_alarm_replacement_keeps_pending);
    g_test_add_func("/xbox/nv2a/ptimer/reconcile/full-counter-wrap-zero-alarm",
                    test_full_counter_wrap_zero_alarm);
    g_test_add_func("/xbox/nv2a/ptimer/reconcile/phase-correct-first-tick",
                    test_phase_correct_first_tick);
    g_test_add_func("/xbox/nv2a/ptimer/reconcile/phase-deadline-forward-oracle",
                    test_phase_deadline_forward_oracle);
    g_test_add_func("/xbox/nv2a/ptimer/reconcile/ack-keeps-valid-schedule",
                    test_ack_keeps_valid_schedule);
    g_test_add_func("/xbox/nv2a/ptimer/reconcile/core-clock-change-rebuilds-deadline",
                    test_core_clock_change_rebuilds_deadline);
    g_test_add_func("/xbox/nv2a/ptimer/reconcile/stopped-equality-preserves-pending",
                    test_stopped_equality_preserves_pending);
    g_test_add_func("/xbox/nv2a/ptimer/reconcile/v4-masked-restore-preserves-alarm",
                    test_v4_masked_restore_preserves_alarm);
    g_test_add_func("/xbox/nv2a/ptimer/reconcile/v4-queued-zero-restore",
                    test_v4_queued_zero_restore);
    g_test_add_func("/xbox/nv2a/ptimer/reconcile/v5-zero-restore-without-host-timer",
                    test_v5_zero_restore_without_host_timer);
    g_test_add_func("/xbox/nv2a/ptimer/reconcile/v5-disarmed-restore-stays-disarmed",
                    test_v5_disarmed_restore_stays_disarmed);
    g_test_add_func("/xbox/nv2a/ptimer/reconcile/pre-v4-restore-discards-old-alarm",
                    test_pre_v4_restore_discards_old_alarm);
    g_test_add_func("/xbox/nv2a/ptimer/reconcile/deadline-saturates-signed-horizon",
                    test_deadline_saturates_signed_horizon);
    g_test_add_func("/xbox/nv2a/ptimer/reconcile/source-counter-wrap-revalidates-alarm",
                    test_source_counter_wrap_revalidates_alarm);

    return g_test_run();
}

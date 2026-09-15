/*
 * QEMU Geforce NV2A implementation
 * PTIMER - time measurement and time-based alarms
 *
 * Copyright (c) 2012 espes
 * Copyright (c) 2015 Jannik Vogel
 * Copyright (c) 2018-2025 Matt Borgerson
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "nv2a_int.h"

#define CLOCK_HIGH_MASK 0x1fffffffULL
#define ALARM_MASK 0xffffffe0ULL

#define PTIMER_REG_TIME_HIGH_MASK ((uint64_t)CLOCK_HIGH_MASK << 32)
#define PTIMER_REG_TIME_LOW_MASK 0xffffffffULL
#define PTIMER_REG_TIME_MASK (PTIMER_REG_TIME_HIGH_MASK | ALARM_MASK)

#define PTIMER_INTERNAL_TIME_MASK (PTIMER_REG_TIME_MASK >> 5)

#define PTIMER_INTERNAL_TO_REG_TIME(internal_ticks) \
    (((uint64_t)(internal_ticks) << 5) & PTIMER_REG_TIME_MASK)

#define PTIMER_REG_TO_INTERNAL_TIME(reg_time) \
    (((uint64_t)(reg_time) >> 5) & PTIMER_INTERNAL_TIME_MASK)

#define PTIMER_MAKE_REG_TIME(time_1, time_0)          \
    ((((uint64_t)(time_1) & CLOCK_HIGH_MASK) << 32) | \
     ((uint64_t)(time_0) & PTIMER_REG_TIME_LOW_MASK))

#define PTIMER_REG_TIME_GET_TIME_0(reg_time) \
    ((uint32_t)((reg_time) & PTIMER_REG_TIME_LOW_MASK))

#define PTIMER_REG_TIME_GET_TIME_1(reg_time) \
    ((uint32_t)(((reg_time) >> 32) & CLOCK_HIGH_MASK))

static void ptimer_alarm_fired(void *opaque);

static inline bool ptimer_clock_running(const NV2AState *d)
{
    return d->ptimer.numerator != 0 && d->ptimer.denominator != 0 &&
           d->pramdac.core_clock_freq != 0;
}

void ptimer_reset(NV2AState *d)
{
    d->ptimer.alarm_armed = false;
    d->ptimer.alarm_time = 0;
    d->ptimer.time_offset = 0;
    timer_del(&d->ptimer.timer);
    d->ptimer.host = (PtimerHostSchedule) {
        .state = PTIMER_HOST_ABSENT,
    };
}

void ptimer_init(NV2AState *d)
{
    timer_init_ns(&d->ptimer.timer, QEMU_CLOCK_VIRTUAL, ptimer_alarm_fired, d);
    ptimer_reset(d);
}

static uint64_t ptimer_get_absolute_clock(NV2AState *d)
{
    /*
     * The ratio registers reset to zero and are guest writable. Keep their
     * raw values guest-visible, but define a stopped clock until the complete
     * ratio and source clock are non-zero. This avoids guest-triggerable
     * division by zero without inventing a non-zero register value.
     */
    if (!ptimer_clock_running(d)) {
        return 0;
    }

    return muldiv64(muldiv64(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL),
                             d->pramdac.core_clock_freq,
                             NANOSECONDS_PER_SECOND),
                    d->ptimer.denominator, d->ptimer.numerator);
}

static inline uint64_t get_internal_clock(NV2AState *d, uint64_t absolute_clock)
{
    return (absolute_clock + d->ptimer.time_offset) & PTIMER_INTERNAL_TIME_MASK;
}

static inline uint64_t get_reg_time(NV2AState *d)
{
    uint64_t internal_clock =
        get_internal_clock(d, ptimer_get_absolute_clock(d));
    return PTIMER_INTERNAL_TO_REG_TIME(internal_clock);
}

static void ptimer_div_ceil(uint64_t *lo, uint64_t *hi, uint64_t divisor)
{
    if (divu128(lo, hi, divisor) && ++*lo == 0) {
        ++*hi;
    }
}

static uint64_t ptimer_sample_clock(NV2AState *d, int64_t now_ns,
                                   uint64_t *gpu_clock, uint64_t *gpu_phase,
                                   uint64_t *timer_phase)
{
    uint64_t lo, hi;

    /* Match both truncations in ptimer_get_absolute_clock(). */
    mulu64(&lo, &hi, now_ns, d->pramdac.core_clock_freq);
    *gpu_phase = divu128(&lo, &hi, NANOSECONDS_PER_SECOND);
    *gpu_clock = lo;
    mulu64(&lo, &hi, lo, d->ptimer.denominator);
    *timer_phase = divu128(&lo, &hi, d->ptimer.numerator);
    return PTIMER_INTERNAL_TO_REG_TIME(get_internal_clock(d, lo));
}

static uint64_t ptimer_ticks_to_ns(NV2AState *d, uint64_t internal_ticks,
                                  uint64_t gpu_clock, uint64_t gpu_phase,
                                  uint64_t timer_phase)
{
    uint64_t lo, hi;
    bool overflow;

    assert(ptimer_clock_running(d) && internal_ticks > 0);

    /* Invert each quantization using its phase at the same clock sample:
     * delta_gpu = ceil((ticks * numerator - timer_phase) / denominator)
     * delta_ns  = ceil((delta_gpu * 1e9 - gpu_phase) / core_clock_freq)
     * The products fit in 128 bits (56-bit ticks, 32-bit ratio, 30-bit 1e9).
     * Do not truncate a wide quotient before checking the source-clock wrap.
     */
    mulu64(&lo, &hi, internal_ticks, d->ptimer.numerator);
    hi -= lo < timer_phase;
    lo -= timer_phase;
    ptimer_div_ceil(&lo, &hi, d->ptimer.denominator);

    /* The existing forward model truncates GPU ticks to 64 bits. Reconcile at
     * that discontinuity rather than extrapolating the ratio through it.
     * For gpu_clock == 0, the distance to wrap is exactly 2^64.
     */
    uint64_t until_wrap = -gpu_clock;
    if (hi || (gpu_clock && lo > until_wrap)) {
        lo = until_wrap;
        hi = !gpu_clock;
    }
    overflow = mulu128(&lo, &hi, NANOSECONDS_PER_SECOND);
    assert(!overflow);
    hi -= lo < gpu_phase;
    lo -= gpu_phase;
    ptimer_div_ceil(&lo, &hi, d->pramdac.core_clock_freq);

    return hi ? UINT64_MAX : lo;
}

static inline uint64_t ptimer_alarm_distance(uint64_t reg_now,
                                             uint64_t alarm_time)
{
    uint64_t diff = (alarm_time - reg_now) & PTIMER_REG_TIME_MASK;
    if (diff > (PTIMER_REG_TIME_MASK >> 1)) {
        return 0;
    }
    return diff;
}

static inline bool is_alarm_reached(uint64_t reg_now, uint64_t alarm_time)
{
    return !ptimer_alarm_distance(reg_now, alarm_time);
}

static inline uint64_t advance_alarm_epoch(uint64_t reg_time)
{
    return (reg_time + (1ULL << 32)) & PTIMER_REG_TIME_MASK;
}

static uint64_t next_alarm_time(uint64_t reg_now, uint32_t alarm_low)
{
    uint32_t now_low = PTIMER_REG_TIME_GET_TIME_0(reg_now) & ALARM_MASK;
    uint64_t target =
        (reg_now & ~PTIMER_REG_TIME_LOW_MASK) | (alarm_low & ALARM_MASK);

    if ((alarm_low & ALARM_MASK) <= now_low) {
        target = advance_alarm_epoch(target);
    }
    return target & PTIMER_REG_TIME_MASK;
}

static bool ptimer_latch_overdue_alarm(NV2AState *d, uint64_t reg_now)
{
    if (!d->ptimer.alarm_armed || !ptimer_clock_running(d) ||
        !is_alarm_reached(reg_now, d->ptimer.alarm_time)) {
        return false;
    }

    d->ptimer.pending_interrupts |= NV_PTIMER_INTR_0_ALARM;
    d->ptimer.alarm_time = next_alarm_time(
        reg_now, PTIMER_REG_TIME_GET_TIME_0(d->ptimer.alarm_time));
    d->ptimer.host.dirty = true;
    return true;
}

static bool ptimer_needs_host_callback(const NV2AState *d)
{
    return d->ptimer.alarm_armed && ptimer_clock_running(d) &&
           (d->ptimer.enabled_interrupts & NV_PTIMER_INTR_EN_0_ALARM);
}

static PtimerDeadline ptimer_make_deadline(NV2AState *d)
{
    PtimerDeadline desired = { 0 };

    if (!ptimer_needs_host_callback(d)) {
        return desired;
    }

    int64_t now_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    uint64_t gpu_clock, gpu_phase, timer_phase;
    uint64_t reg_now = ptimer_sample_clock(d, now_ns, &gpu_clock,
                                         &gpu_phase, &timer_phase);
    uint64_t diff_reg_time =
        ptimer_alarm_distance(reg_now, d->ptimer.alarm_time);
    uint64_t diff_ns = 0;

    if (diff_reg_time > 0) {
        uint64_t internal_diff_ticks =
            PTIMER_REG_TO_INTERNAL_TIME(diff_reg_time);
        diff_ns = ptimer_ticks_to_ns(d, internal_diff_ticks,
                                     gpu_clock, gpu_phase, timer_phase);
    }

    /* A distant alarm must not wrap into an immediate signed deadline. */
    desired.queued = true;
    desired.deadline_ns =
        diff_ns > (uint64_t)INT64_MAX - (uint64_t)now_ns ?
        INT64_MAX : now_ns + diff_ns;
    return desired;
}

static void ptimer_apply_deadline(NV2AState *d, const PtimerDeadline *desired)
{
    assert(bql_locked());

    switch (ptimer_queue_action(&d->ptimer.host, desired)) {
    case PTIMER_QUEUE_KEEP:
        break;
    case PTIMER_QUEUE_CANCEL:
        timer_del(&d->ptimer.timer);
        break;
    case PTIMER_QUEUE_ARM:
        timer_mod(&d->ptimer.timer, desired->deadline_ns);
        break;
    }

    d->ptimer.host = (PtimerHostSchedule) {
        .state = desired->queued ? PTIMER_HOST_QUEUED : PTIMER_HOST_ABSENT,
        .deadline_ns = desired->queued ? desired->deadline_ns : 0,
    };
}

static void schedule_qemu_timer(NV2AState *d)
{
    PtimerDeadline desired = ptimer_make_deadline(d);

    ptimer_apply_deadline(d, &desired);
}

static bool ptimer_ack_keeps_schedule(NV2AState *d)
{
    bool needs_callback = ptimer_needs_host_callback(d);
    int64_t now_ns = needs_callback ?
                     qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) : 0;

    return ptimer_schedule_reusable(&d->ptimer.host,
                                    needs_callback, now_ns);
}

static void ptimer_alarm_fired(void *opaque)
{
    NV2AState *d = (NV2AState *)opaque;
    assert(bql_locked());

    /* QEMU removes the event before invoking its callback. */
    d->ptimer.host = (PtimerHostSchedule) {
        .state = PTIMER_HOST_ABSENT,
        .dirty = true,
    };
    uint64_t reg_now = get_reg_time(d);

    if (ptimer_latch_overdue_alarm(d, reg_now)) {
        nv2a_update_irq(d);
    }

    schedule_qemu_timer(d);
}

void ptimer_post_load(NV2AState *d, int version_id)
{
    assert(bql_locked());

    if (version_id < 5) {
        /* v4 stored armed state implicitly in the host timer. Earlier streams
         * stored no alarm state; pre-load clears the previous guest's timer. */
        d->ptimer.alarm_armed = version_id == 4 &&
                               timer_pending(&d->ptimer.timer);
    }
    /* VMState may replace the timer behind the old derived queue record. */
    d->ptimer.host = (PtimerHostSchedule) {
        .state = PTIMER_HOST_UNKNOWN,
        .dirty = true,
    };
    ptimer_latch_overdue_alarm(d, get_reg_time(d));
    schedule_qemu_timer(d);
    nv2a_update_irq(d);
}

void ptimer_set_core_clock(NV2AState *d, uint64_t frequency)
{
    assert(bql_locked());
    /* Preserve the existing absolute-time counter model on rate changes.
     * First materialize elapsed state using the old frequency. */
    ptimer_latch_overdue_alarm(d, get_reg_time(d));
    d->ptimer.host.dirty = true;
    d->pramdac.core_clock_freq = frequency;
    schedule_qemu_timer(d);
    nv2a_update_irq(d);
}

uint64_t ptimer_read(void *opaque, hwaddr addr, unsigned int size)
{
    NV2AState *d = opaque;

    uint64_t r = 0;
    switch (addr) {
    case NV_PTIMER_INTR_0:
        if (d->ptimer.alarm_armed) {
            uint64_t reg_now = get_reg_time(d);
            if (ptimer_latch_overdue_alarm(d, reg_now)) {
                nv2a_update_irq(d);
                schedule_qemu_timer(d);
            }
        }
        r = d->ptimer.pending_interrupts;
        break;
    case NV_PTIMER_INTR_EN_0:
        r = d->ptimer.enabled_interrupts;
        break;
    case NV_PTIMER_NUMERATOR:
        r = d->ptimer.numerator;
        break;
    case NV_PTIMER_DENOMINATOR:
        r = d->ptimer.denominator;
        break;
    case NV_PTIMER_TIME_0: {
        uint64_t reg_now = get_reg_time(d);
        r = PTIMER_REG_TIME_GET_TIME_0(reg_now);
    } break;
    case NV_PTIMER_TIME_1: {
        uint64_t reg_now = get_reg_time(d);
        r = PTIMER_REG_TIME_GET_TIME_1(reg_now);
    } break;
    case NV_PTIMER_ALARM_0:
        r = PTIMER_REG_TIME_GET_TIME_0(d->ptimer.alarm_time);
        break;
    default:
        break;
    }

    nv2a_reg_log_read(NV_PTIMER, addr, size, r);
    return r;
}

void ptimer_write(void *opaque, hwaddr addr, uint64_t val, unsigned int size)
{
    NV2AState *d = opaque;

    nv2a_reg_log_write(NV_PTIMER, addr, size, val);

    /* Reconcile under the old state before acknowledgment, reprogramming,
     * clock-ratio changes or guest time writes. Publish the final IRQ once. */
    switch (addr) {
    case NV_PTIMER_INTR_0:
    case NV_PTIMER_INTR_EN_0:
    case NV_PTIMER_NUMERATOR:
    case NV_PTIMER_DENOMINATOR:
    case NV_PTIMER_ALARM_0:
    case NV_PTIMER_TIME_0:
    case NV_PTIMER_TIME_1:
        ptimer_latch_overdue_alarm(d, get_reg_time(d));
        break;
    default:
        break;
    }

    switch (addr) {
    case NV_PTIMER_INTR_0:
        d->ptimer.pending_interrupts &= ~val;
        if (!ptimer_ack_keeps_schedule(d)) {
            schedule_qemu_timer(d);
        }
        break;
    case NV_PTIMER_INTR_EN_0:
        d->ptimer.host.dirty = true;
        d->ptimer.enabled_interrupts = val;
        schedule_qemu_timer(d);
        break;
    case NV_PTIMER_DENOMINATOR:
        d->ptimer.host.dirty = true;
        d->ptimer.denominator = val;
        if (d->ptimer.alarm_armed) {
            schedule_qemu_timer(d);
        }
        break;
    case NV_PTIMER_NUMERATOR:
        d->ptimer.host.dirty = true;
        d->ptimer.numerator = val;
        if (d->ptimer.alarm_armed) {
            schedule_qemu_timer(d);
        }
        break;
    case NV_PTIMER_ALARM_0: {
        uint64_t reg_now = get_reg_time(d);
        d->ptimer.host.dirty = true;
        d->ptimer.alarm_time = next_alarm_time(reg_now, val);
        d->ptimer.alarm_armed = true;
        schedule_qemu_timer(d);
    } break;
    case NV_PTIMER_TIME_0: {
        uint64_t current_reg_time = get_reg_time(d);
        uint64_t target_reg_time = PTIMER_MAKE_REG_TIME(
            PTIMER_REG_TIME_GET_TIME_1(current_reg_time), val & ALARM_MASK);
        uint64_t target_internal = PTIMER_REG_TO_INTERNAL_TIME(target_reg_time);
        d->ptimer.host.dirty = true;
        d->ptimer.time_offset = target_internal - ptimer_get_absolute_clock(d);
        if (d->ptimer.alarm_armed) {
            schedule_qemu_timer(d);
        }
    } break;
    case NV_PTIMER_TIME_1: {
        uint64_t current_reg_time = get_reg_time(d);
        uint64_t target_reg_time = PTIMER_MAKE_REG_TIME(
            val, PTIMER_REG_TIME_GET_TIME_0(current_reg_time));
        uint64_t target_internal = PTIMER_REG_TO_INTERNAL_TIME(target_reg_time);
        d->ptimer.host.dirty = true;
        d->ptimer.time_offset = target_internal - ptimer_get_absolute_clock(d);
        if (d->ptimer.alarm_armed) {
            schedule_qemu_timer(d);
        }
    } break;
    default:
        break;
    }
    nv2a_update_irq(d);
}

/*
 * Minimal NV2A definitions for deterministic PTIMER unit tests.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef XBOX_NV2A_PTIMER_TEST_SHIM_H
#define XBOX_NV2A_PTIMER_TEST_SHIM_H

#include "qemu/osdep.h"
#include "qemu/host-utils.h"
#include "qemu/timer.h"
#include "qemu/units.h"
#include "exec/hwaddr.h"

#include "hw/xbox/nv2a/nv2a_regs.h"

#ifndef NV_PMC_INTR_0_PTIMER
#define NV_PMC_INTR_0_PTIMER (1 << 20)
#endif

/* Keep ptimer.c's heavyweight target-specific internal header out of a unit. */
#define HW_NV2A_INT_H

typedef struct NV2AState {
    struct {
        uint32_t pending_interrupts;
        uint32_t enabled_interrupts;
    } pmc;

    struct {
        uint32_t pending_interrupts;
        uint32_t enabled_interrupts;
        uint32_t numerator;
        uint32_t denominator;
        uint64_t alarm_time;
        uint64_t time_offset;
        QEMUTimer timer;
    } ptimer;

    struct {
        uint64_t core_clock_freq;
    } pramdac;
} NV2AState;

void nv2a_update_irq(NV2AState *d);

static inline void nv2a_reg_log_read(int block, hwaddr addr,
                                     unsigned int size, uint64_t val)
{
    (void)block;
    (void)addr;
    (void)size;
    (void)val;
}

static inline void nv2a_reg_log_write(int block, hwaddr addr,
                                      unsigned int size, uint64_t val)
{
    (void)block;
    (void)addr;
    (void)size;
    (void)val;
}

uint64_t ptimer_read(void *opaque, hwaddr addr, unsigned int size);
void ptimer_write(void *opaque, hwaddr addr, uint64_t val,
                  unsigned int size);
void ptimer_init(NV2AState *d);
void ptimer_reset(NV2AState *d);
void ptimer_post_load(NV2AState *d);

#endif

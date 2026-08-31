/* SPDX-License-Identifier: LGPL-2.0-or-later */
#ifndef HW_XBOX_NV2A_PGRAPH_ATOMIC_STATE_H
#define HW_XBOX_NV2A_PGRAPH_ATOMIC_STATE_H

#include "qemu/atomic.h"
#include "qemu/bitmap.h"

static inline uint32_t pgraph_atomic_reg_read(uint32_t *regs,
                                              unsigned int offset)
{
    return qatomic_read(&regs[offset]);
}

static inline void pgraph_atomic_reg_write(uint32_t *regs,
                                           unsigned long *dirty,
                                           unsigned int offset,
                                           uint32_t value)
{
    if (qatomic_read(&regs[offset]) != value) {
        bitmap_set(dirty, offset / sizeof(uint32_t), 1);
    }
    qatomic_set(&regs[offset], value);
}

#endif

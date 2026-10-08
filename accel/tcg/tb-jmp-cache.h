/*
 * The per-CPU TranslationBlock jump cache.
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef ACCEL_TCG_TB_JMP_CACHE_H
#define ACCEL_TCG_TB_JMP_CACHE_H

#include "qemu/rcu.h"
#include "exec/cpu-common.h"

#define TB_JMP_CACHE_BITS 12
#define TB_JMP_CACHE_SIZE (1 << TB_JMP_CACHE_BITS)
#define TB_VICTIM_CACHE_SIZE 8

/*
 * Invalidated in parallel; all accesses to 'tb' must be atomic.
 * A valid entry is read/written by a single CPU, therefore there is
 * no need for qatomic_rcu_read() and pc is always consistent with a
 * non-NULL value of 'tb'.  Strictly speaking pc is only needed for
 * CF_PCREL, but it's used always for simplicity.
 */
typedef struct CPUJumpCache {
    struct rcu_head rcu;
    struct {
        TranslationBlock *tb;
        vaddr pc;
    } array[TB_JMP_CACHE_SIZE];
#if defined(CONFIG_XEMU_TCG_VICTIM_CACHE) || defined(TCG_VICTIM_CACHE_UNIT_TEST)
    /* Keep generated primary lookup offsets unchanged. */
    struct {
        /* Foreign invalidators touch only these two atomic fields. */
        uint64_t generation;
        unsigned invalidators;
        /* Everything below is private to the serialized dispatch owner. */
        uint64_t owner_generation;
        unsigned count;
        struct {
            TranslationBlock *tb;
            vaddr pc;
        } entries[TB_VICTIM_CACHE_SIZE];
    } victim;
#endif
} CPUJumpCache;

#endif /* ACCEL_TCG_TB_JMP_CACHE_H */

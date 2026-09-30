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
#include "exec/translation-block.h"
#include "accel/tcg/tb-cpu-state.h"
#ifdef CONFIG_XEMU_TCG_JUMP_CACHE_PROBE
#include "jump-cache-probe.h"
#endif

#define TB_JMP_CACHE_BITS 12
#define TB_JMP_CACHE_SIZE (1 << TB_JMP_CACHE_BITS)

/*
 * Invalidated in parallel; all accesses to 'tb' must be atomic.
 * A valid entry is read/written by a single CPU, therefore there is
 * no need for qatomic_rcu_read() and pc is always consistent with a
 * non-NULL value of 'tb'.  Strictly speaking pc is only needed for
 * CF_PCREL, but it's used always for simplicity.
 */
typedef struct CPUJumpCache {
    struct rcu_head rcu;
#ifdef CONFIG_XEMU_TCG_JUMP_CACHE_PROBE
    TCGJumpCacheProbeSlot array[TB_JMP_CACHE_SIZE];
    TCGJumpCacheProbe *probe;
#else
    struct {
        TranslationBlock *tb;
        vaddr pc;
    } array[TB_JMP_CACHE_SIZE];
#endif
} CPUJumpCache;

/* Keep complete cflags equality: CF_INVALID must never be masked here. */
static inline TranslationBlock *
tcg_jump_cache_lookup(CPUJumpCache *jc, unsigned hash, TCGTBCPUState s)
{
    TranslationBlock *tb = qatomic_read(&jc->array[hash].tb);

    if (likely(tb && jc->array[hash].pc == s.pc &&
               tb->cs_base == s.cs_base && tb->flags == s.flags &&
               tb_cflags(tb) == s.cflags)) {
        return tb;
    }
    return NULL;
}

static inline void tcg_jump_cache_clear_range(CPUJumpCache *jc, unsigned first,
                                              unsigned count)
{
    if (unlikely(!jc)) {
        return;
    }
    assert(first <= TB_JMP_CACHE_SIZE && count <= TB_JMP_CACHE_SIZE - first);
    for (unsigned i = 0; i < count; i++) {
        qatomic_set(&jc->array[first + i].tb, NULL);
    }
}

#endif /* ACCEL_TCG_TB_JMP_CACHE_H */

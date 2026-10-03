/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ACCEL_TCG_TB_VICTIM_CACHE_H
#define ACCEL_TCG_TB_VICTIM_CACHE_H

#include "accel/tcg/tb-cpu-state.h"
#include "exec/translation-block.h"
#include "tb-jmp-cache.h"
#include "tb-hash.h"

#if defined(CONFIG_XEMU_TCG_VICTIM_CACHE) || defined(TCG_VICTIM_CACHE_UNIT_TEST)
/* Capture before global lookup/code generation; both can fault or flush. */
uint64_t tcg_victim_cache_epoch(CPUJumpCache *jc);
TranslationBlock *tcg_victim_cache_lookup(CPUJumpCache *jc, unsigned hash,
                                          TCGTBCPUState state, uint64_t epoch);
bool tcg_victim_cache_fill(CPUJumpCache *jc, unsigned hash, vaddr pc,
                           TranslationBlock *tb, uint64_t epoch);
void tcg_victim_cache_invalidate_begin(CPUJumpCache *jc);
void tcg_victim_cache_invalidate_end(CPUJumpCache *jc);
#else
static inline void tcg_victim_cache_invalidate_begin(CPUJumpCache *jc)
{
}

static inline void tcg_victim_cache_invalidate_end(CPUJumpCache *jc)
{
}
#endif

/* Shared production clear paths; disabled builds inline the original writes. */
static inline void tcg_jump_cache_clear_all(CPUJumpCache *jc)
{
    tcg_victim_cache_invalidate_begin(jc);
    for (unsigned i = 0; i < TB_JMP_CACHE_SIZE; i++) {
        qatomic_set(&jc->array[i].tb, NULL);
    }
    tcg_victim_cache_invalidate_end(jc);
}

#ifdef CONFIG_SOFTMMU
static inline void tcg_jump_cache_clear_page(CPUJumpCache *jc, vaddr page_addr)
{
    unsigned first = tb_jmp_cache_hash_page(page_addr);

    tcg_victim_cache_invalidate_begin(jc);
    for (unsigned i = 0; i < TB_JMP_PAGE_SIZE; i++) {
        qatomic_set(&jc->array[first + i].tb, NULL);
    }
    tcg_victim_cache_invalidate_end(jc);
}
#endif

static inline void tcg_jump_cache_clear_tb(CPUJumpCache *jc, unsigned hash,
                                           TranslationBlock *tb)
{
    tcg_victim_cache_invalidate_begin(jc);
    if (qatomic_read(&jc->array[hash].tb) == tb) {
        qatomic_set(&jc->array[hash].tb, NULL);
    }
    tcg_victim_cache_invalidate_end(jc);
}

#endif

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "tb-victim-cache.h"

/* TB storage still requires the existing exclusive/serial flush lifetime. */
static bool epoch_valid(CPUJumpCache *jc, uint64_t epoch)
{
    smp_rmb();
    return qatomic_read(&jc->victim.generation) == epoch &&
           qatomic_read(&jc->victim.invalidators) == 0;
}

void tcg_victim_cache_invalidate_begin(CPUJumpCache *jc)
{
    if (jc) {
        qatomic_inc(&jc->victim.invalidators);
        qatomic_inc(&jc->victim.generation);
        smp_mb__after_rmw();
    }
}

void tcg_victim_cache_invalidate_end(CPUJumpCache *jc)
{
    if (jc) {
        smp_mb__before_rmw();
        qatomic_inc(&jc->victim.generation);
        qatomic_dec(&jc->victim.invalidators);
    }
}

uint64_t tcg_victim_cache_epoch(CPUJumpCache *jc)
{
    uint64_t epoch = qatomic_read(&jc->victim.generation);

    smp_rmb();
    if (jc->victim.owner_generation != epoch ||
        qatomic_read(&jc->victim.invalidators) != 0) {
        jc->victim.count = 0;
        jc->victim.owner_generation = epoch;
    }
    return epoch;
}

static void remove_entry(CPUJumpCache *jc, unsigned index)
{
    unsigned count = --jc->victim.count;

    memmove(&jc->victim.entries[index], &jc->victim.entries[index + 1],
            (count - index) * sizeof(jc->victim.entries[0]));
}

bool tcg_victim_cache_fill(CPUJumpCache *jc, unsigned hash, vaddr pc,
                           TranslationBlock *tb, uint64_t epoch)
{
    if (epoch_valid(jc, epoch)) {
        TranslationBlock *displaced = qatomic_read(&jc->array[hash].tb);
        vaddr displaced_pc = jc->array[hash].pc;

        /* Remove the promoted identity; virtual aliases remain distinct. */
        for (unsigned i = 0; i < jc->victim.count;) {
            if (jc->victim.entries[i].tb == tb &&
                jc->victim.entries[i].pc == pc) {
                remove_entry(jc, i);
            } else {
                i++;
            }
        }
        if (displaced && (displaced != tb || displaced_pc != pc) &&
            !(tb_cflags(displaced) & CF_INVALID)) {
            if (jc->victim.count == TB_VICTIM_CACHE_SIZE) {
                remove_entry(jc, 0);
            }
            unsigned i = jc->victim.count++;
            jc->victim.entries[i].tb = displaced;
            jc->victim.entries[i].pc = displaced_pc;
        }
    } else {
        jc->victim.count = 0;
    }

    jc->array[hash].pc = pc;
    qatomic_set_mb(&jc->array[hash].tb, tb);

    /* A late publisher must not resurrect a slot after a concurrent clear. */
    if (!epoch_valid(jc, epoch)) {
        jc->victim.count = 0;
        qatomic_set(&jc->array[hash].tb, NULL);
        return false;
    }
    return true;
}

TranslationBlock *tcg_victim_cache_lookup(CPUJumpCache *jc, unsigned hash,
                                          TCGTBCPUState state, uint64_t epoch)
{
    if (!epoch_valid(jc, epoch)) {
        jc->victim.count = 0;
        return NULL;
    }
    for (unsigned i = 0; i < jc->victim.count; i++) {
        TranslationBlock *tb = jc->victim.entries[i].tb;

        if (jc->victim.entries[i].pc == state.pc &&
            tb->cs_base == state.cs_base && tb->flags == state.flags &&
            tb_cflags(tb) == state.cflags) {
            return tcg_victim_cache_fill(jc, hash, state.pc, tb, epoch) ? tb :
                                                                          NULL;
        }
    }
    return NULL;
}

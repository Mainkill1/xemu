/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "accel/tcg/tb-jmp-cache.h"
#include "accel/tcg/tb-cpu-state.h"
#include "exec/translation-block.h"

#define TEST_SLOT 37

static TCGTBCPUState matching_state(void)
{
    return (TCGTBCPUState) {
        .pc = 0x12345000,
        .cs_base = 0x1000,
        .flags = 0x2345,
        .cflags = CF_PARALLEL | 17,
    };
}

int main(void)
{
    CPUJumpCache *cache = g_new0(CPUJumpCache, 1);
    TCGTBCPUState state = matching_state();
    TranslationBlock tb = {
        .pc = state.pc,
        .cs_base = state.cs_base,
        .flags = state.flags,
        .cflags = state.cflags,
    };

    cache->array[TEST_SLOT].pc = state.pc;
    qatomic_set(&cache->array[TEST_SLOT].tb, &tb);
    g_assert_true(tb_jmp_cache_lookup(cache, TEST_SLOT, state) == &tb);

    TCGTBCPUState mismatch = state;
    mismatch.pc++;
    g_assert_null(tb_jmp_cache_lookup(cache, TEST_SLOT, mismatch));

    mismatch = state;
    mismatch.cs_base++;
    g_assert_null(tb_jmp_cache_lookup(cache, TEST_SLOT, mismatch));

    mismatch = state;
    mismatch.flags++;
    g_assert_null(tb_jmp_cache_lookup(cache, TEST_SLOT, mismatch));

    mismatch = state;
    mismatch.cflags++;
    g_assert_null(tb_jmp_cache_lookup(cache, TEST_SLOT, mismatch));

    cache->array[TEST_SLOT].pc++;
    g_assert_null(tb_jmp_cache_lookup(cache, TEST_SLOT, state));
    cache->array[TEST_SLOT].pc = state.pc;

    qatomic_set(&cache->array[TEST_SLOT].tb, NULL);
    g_assert_null(tb_jmp_cache_lookup(cache, TEST_SLOT, state));

    g_free(cache);
    return 0;
}

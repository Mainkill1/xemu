/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "accel/tcg/tb-victim-cache.h"

/* Research control: retain fill/clear work, but never promote history. */
static void test_no_promotion(void)
{
    CPUJumpCache *jc = g_new0(CPUJumpCache, 1);
    TCGTBCPUState a = {
        .pc = 0x80000000,
        .cs_base = 0x12340000,
        .flags = 0x80000001,
        .cflags = CF_PCREL | 32,
    };
    TCGTBCPUState b = a;
    TranslationBlock ta = {
        .pc = a.pc,
        .cs_base = a.cs_base,
        .flags = a.flags,
        .cflags = a.cflags,
    };
    TranslationBlock tb = ta;
    unsigned hash = tb_jmp_cache_hash_func(a.pc);
    uint64_t epoch;

    b.pc = tb.pc = 0x81000000;
    g_assert_cmpuint(hash, ==, tb_jmp_cache_hash_func(b.pc));
    epoch = tcg_victim_cache_epoch(jc);
    g_assert_true(tcg_victim_cache_fill(jc, hash, a.pc, &ta, epoch));
    g_assert_true(tcg_victim_cache_fill(jc, hash, b.pc, &tb, epoch));
    g_assert_cmpuint(jc->victim.count, ==, 1);
    g_assert_null(tcg_victim_cache_lookup(jc, hash, a, epoch));
    g_assert_true(qatomic_read(&jc->array[hash].tb) == &tb);
    g_assert_cmpuint(jc->victim.count, ==, 1);
    g_assert_null(tcg_victim_cache_lookup(jc, hash, b, epoch));
    g_assert_true(qatomic_read(&jc->array[hash].tb) == &tb);

    tcg_jump_cache_clear_all(jc);
    epoch = tcg_victim_cache_epoch(jc);
    g_assert_cmpuint(jc->victim.count, ==, 0);
    g_assert_null(qatomic_read(&jc->array[hash].tb));
    g_assert_null(tcg_victim_cache_lookup(jc, hash, a, epoch));
    g_free(jc);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/tcg/victim8-bypass/no-promotion", test_no_promotion);
    return g_test_run();
}

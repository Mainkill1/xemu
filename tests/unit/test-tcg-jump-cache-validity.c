/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "accel/tcg/tb-cpu-state.h"
#include "accel/tcg/tb-jmp-cache.h"
#include "accel/tcg/tb-hash.h"

static TCGTBCPUState state_for(vaddr pc)
{
    return (TCGTBCPUState){
        .pc = pc,
        .cs_base = 0x12340000,
        .flags = 0x80000001,
        .cflags = CF_PCREL | 32,
    };
}

static TranslationBlock block_for(TCGTBCPUState s)
{
    return (TranslationBlock){
        .pc = s.pc,
        .cs_base = s.cs_base,
        .flags = s.flags,
        .cflags = s.cflags,
    };
}

static void publish(CPUJumpCache *jc, TCGTBCPUState s, TranslationBlock *tb)
{
    unsigned h = tb_jmp_cache_hash_func(s.pc);
    jc->array[h].pc = s.pc;
    qatomic_set(&jc->array[h].tb, tb);
}

static void test_valid_and_invalid(void)
{
    CPUJumpCache *jc = g_new0(CPUJumpCache, 1);
    TCGTBCPUState s = state_for(0x81234560);
    TranslationBlock tb = block_for(s);
    unsigned h = tb_jmp_cache_hash_func(s.pc);

    g_assert_null(tcg_jump_cache_lookup(jc, h, s));
    publish(jc, s, &tb);
    g_assert_true(tcg_jump_cache_lookup(jc, h, s) == &tb);
    qatomic_set(&tb.cflags, tb.cflags | CF_INVALID);
    g_assert_null(tcg_jump_cache_lookup(jc, h, s));
    /* The deliberately stale pointer remains in its slot for this check. */
    g_assert_true(qatomic_read(&jc->array[h].tb) == &tb);
    g_free(jc);
}

static void test_complete_identity(void)
{
    CPUJumpCache *jc = g_new0(CPUJumpCache, 1);
    TCGTBCPUState s = state_for(0x81234560), wanted;
    TranslationBlock tb = block_for(s);
    unsigned h = tb_jmp_cache_hash_func(s.pc);
    const uint32_t changed_cflags[] = {
        1,
        CF_NO_GOTO_TB,
        CF_NO_GOTO_PTR,
        CF_SINGLE_STEP,
        CF_MEMI_ONLY,
        CF_USE_ICOUNT,
        CF_PARALLEL,
        CF_NOIRQ,
        CF_PCREL,
        CF_BP_PAGE,
        1U << CF_CLUSTER_SHIFT,
    };

    publish(jc, s, &tb);
    wanted = s;
    wanted.pc += 4;
    g_assert_null(tcg_jump_cache_lookup(jc, h, wanted));
    wanted = s;
    wanted.cs_base ^= 0x1000;
    g_assert_null(tcg_jump_cache_lookup(jc, h, wanted));
    wanted = s;
    wanted.flags ^= 1;
    g_assert_null(tcg_jump_cache_lookup(jc, h, wanted));
    for (unsigned i = 0; i < G_N_ELEMENTS(changed_cflags); i++) {
        wanted = s;
        wanted.cflags ^= changed_cflags[i];
        g_assert_null(tcg_jump_cache_lookup(jc, h, wanted));
    }
    /* PC-relative code can have a different original translation PC. */
    tb.pc += 0x1000;
    g_assert_true(tcg_jump_cache_lookup(jc, h, s) == &tb);
    g_free(jc);
}

static void test_same_slot_replacement(void)
{
    CPUJumpCache *jc = g_new0(CPUJumpCache, 1);
    TCGTBCPUState first = state_for(0x81234560);
    TCGTBCPUState second = first;
    TranslationBlock invalid = block_for(first);
    unsigned h = tb_jmp_cache_hash_func(first.pc);

    for (second.pc += 4; tb_jmp_cache_hash_func(second.pc) != h;
         second.pc += 4) {
        g_assert_cmpuint(second.pc - first.pc, <, 0x100000);
    }
    second.cflags &= ~CF_PCREL;
    TranslationBlock valid = block_for(second);
    publish(jc, first, &invalid);
    qatomic_set(&invalid.cflags, invalid.cflags | CF_INVALID);
    publish(jc, second, &valid);
    g_assert_null(tcg_jump_cache_lookup(jc, h, first));
    g_assert_true(tcg_jump_cache_lookup(jc, h, second) == &valid);
    g_free(jc);
}

static void test_page_clear(void)
{
    CPUJumpCache *jc = g_new0(CPUJumpCache, 1);
    TranslationBlock block = {};
    vaddr page = 0x81234000;
    unsigned first = tb_jmp_cache_hash_page(page);

    for (unsigned i = 0; i < TB_JMP_CACHE_SIZE; i++) {
        jc->array[i].pc = 1000 + i;
        qatomic_set(&jc->array[i].tb, &block);
    }
    tcg_jump_cache_clear_range(jc, first, TB_JMP_PAGE_SIZE);
    for (unsigned i = 0; i < TB_JMP_CACHE_SIZE; i++) {
        if (i >= first && i < first + TB_JMP_PAGE_SIZE) {
            g_assert_null(qatomic_read(&jc->array[i].tb));
        } else {
            g_assert_true(qatomic_read(&jc->array[i].tb) == &block);
        }
        g_assert_cmpuint(jc->array[i].pc, ==, 1000 + i);
    }
    g_free(jc);
}

static void test_spanning_page_clear(void)
{
    CPUJumpCache *jc = g_new0(CPUJumpCache, 1);
    vaddr page = 0x81234000;
    TCGTBCPUState s = state_for(page - 2);
    TranslationBlock tb = block_for(s);
    unsigned h = tb_jmp_cache_hash_func(s.pc);

    publish(jc, s, &tb);
    /* The cputlb caller clears the changed page and its preceding page. */
    tcg_jump_cache_clear_range(jc, tb_jmp_cache_hash_page(page),
                               TB_JMP_PAGE_SIZE);
    g_assert_true(tcg_jump_cache_lookup(jc, h, s) == &tb);
    tcg_jump_cache_clear_range(
        jc, tb_jmp_cache_hash_page(page - TARGET_PAGE_SIZE), TB_JMP_PAGE_SIZE);
    g_assert_null(tcg_jump_cache_lookup(jc, h, s));
    publish(jc, s, &tb);
    tcg_jump_cache_clear_range(jc, tb_jmp_cache_hash_page(s.pc),
                               TB_JMP_PAGE_SIZE);
    g_assert_null(tcg_jump_cache_lookup(jc, h, s));
    g_free(jc);
}

static void test_clear_before_address_reuse(void)
{
    CPUJumpCache *jc = g_new0(CPUJumpCache, 1);
    TCGTBCPUState s = state_for(0x81234560);
    TranslationBlock tb = block_for(s);
    unsigned h = tb_jmp_cache_hash_func(s.pc);

    publish(jc, s, &tb);
    qatomic_set(&tb.cflags, tb.cflags | CF_INVALID);
    tcg_jump_cache_clear_range(jc, 0, TB_JMP_CACHE_SIZE);
    /* Reuse exactly the same object address and matching identity. */
    tb = block_for(s);
    g_assert_null(tcg_jump_cache_lookup(jc, h, s));
    publish(jc, s, &tb);
    g_assert_true(tcg_jump_cache_lookup(jc, h, s) == &tb);
    tcg_jump_cache_clear_range(NULL, 0, TB_JMP_CACHE_SIZE);
    g_free(jc);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/tcg/jump-cache/invalid", test_valid_and_invalid);
    g_test_add_func("/tcg/jump-cache/identity", test_complete_identity);
    g_test_add_func("/tcg/jump-cache/replacement", test_same_slot_replacement);
    g_test_add_func("/tcg/jump-cache/page-clear", test_page_clear);
    g_test_add_func("/tcg/jump-cache/spanning-clear", test_spanning_page_clear);
    g_test_add_func("/tcg/jump-cache/address-reuse",
                    test_clear_before_address_reuse);
    return g_test_run();
}

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "accel/tcg/tb-cpu-state.h"
#include "accel/tcg/tb-hash.h"
#ifndef _WIN32
#include <sys/mman.h>
#endif

#if defined(CONFIG_XEMU_TCG_VICTIM_CACHE) || defined(TCG_VICTIM_CACHE_UNIT_TEST)
#include "accel/tcg/tb-victim-cache.h"
#else
/* Original direct-map negative control, before the candidate is implemented. */
static uint64_t tcg_victim_cache_epoch(CPUJumpCache *jc)
{
    return 0;
}

static bool tcg_victim_cache_fill(CPUJumpCache *jc, unsigned hash, vaddr pc,
                                  TranslationBlock *tb, uint64_t epoch)
{
    jc->array[hash].pc = pc;
    qatomic_set(&jc->array[hash].tb, tb);
    return true;
}

static TranslationBlock *tcg_victim_cache_lookup(CPUJumpCache *jc,
                                                 unsigned hash,
                                                 TCGTBCPUState state,
                                                 uint64_t epoch)
{
    /* There is no secondary lookup in the original implementation. */
    return NULL;
}
#endif

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

static void fill(CPUJumpCache *jc, TCGTBCPUState s, TranslationBlock *tb)
{
    uint64_t epoch = tcg_victim_cache_epoch(jc);
    g_assert_true(tcg_victim_cache_fill(jc, tb_jmp_cache_hash_func(s.pc), s.pc,
                                        tb, epoch));
}

static TranslationBlock *recover(CPUJumpCache *jc, TCGTBCPUState s)
{
    uint64_t epoch = tcg_victim_cache_epoch(jc);
    return tcg_victim_cache_lookup(jc, tb_jmp_cache_hash_func(s.pc), s, epoch);
}

static void test_collision_recovery(void)
{
    CPUJumpCache *jc = g_new0(CPUJumpCache, 1);
    TCGTBCPUState a = state_for(0x80000000), b = state_for(0x81000000);
    TranslationBlock ta = block_for(a), tb = block_for(b);
    unsigned hash = tb_jmp_cache_hash_func(a.pc);

    g_assert_cmpuint(hash, ==, tb_jmp_cache_hash_func(b.pc));
    fill(jc, a, &ta);
    fill(jc, b, &tb);
    g_assert_true(qatomic_read(&jc->array[hash].tb) == &tb);
    g_assert_true(recover(jc, a) == &ta);
    g_assert_true(qatomic_read(&jc->array[hash].tb) == &ta);
    g_assert_true(recover(jc, b) == &tb);
    g_free(jc);
}

#if defined(CONFIG_XEMU_TCG_VICTIM_CACHE) || defined(TCG_VICTIM_CACHE_UNIT_TEST)
static void test_complete_key(void)
{
    CPUJumpCache *jc = g_new0(CPUJumpCache, 1);
    TCGTBCPUState a = state_for(0x80000000), b = state_for(0x81000000);
    TranslationBlock ta = block_for(a), tb = block_for(b);
    TCGTBCPUState wanted;

    fill(jc, a, &ta);
    fill(jc, b, &tb);
    wanted = a;
    wanted.pc += 0x02000000;
    g_assert_null(recover(jc, wanted));
    wanted = a;
    wanted.cs_base ^= 1;
    g_assert_null(recover(jc, wanted));
    wanted = a;
    wanted.flags ^= 1;
    g_assert_null(recover(jc, wanted));
    const uint32_t changed[] = {
        1, CF_NO_GOTO_TB, CF_NO_GOTO_PTR, CF_USE_ICOUNT, CF_SINGLE_STEP,
    };
    for (unsigned i = 0; i < ARRAY_SIZE(changed); i++) {
        wanted = a;
        wanted.cflags ^= changed[i];
        g_assert_null(recover(jc, wanted));
    }
    g_assert_true(recover(jc, a) == &ta);
    g_free(jc);
}

static void test_invalid_block(void)
{
    CPUJumpCache *jc = g_new0(CPUJumpCache, 1);
    TCGTBCPUState a = state_for(0x80000000), b = state_for(0x81000000);
    TranslationBlock ta = block_for(a), tb = block_for(b);

    fill(jc, a, &ta);
    fill(jc, b, &tb);
    qatomic_set(&ta.cflags, ta.cflags | CF_INVALID);
    g_assert_null(recover(jc, a));
    g_free(jc);
}

static void test_capacity_fifo(void)
{
    CPUJumpCache *jc = g_new0(CPUJumpCache, 1);
    TCGTBCPUState s[10];
    TranslationBlock blocks[10];

    for (unsigned i = 0; i < 10; i++) {
        s[i] = state_for(0x80000000 + i * 0x01000000);
        blocks[i] = block_for(s[i]);
        g_assert_cmpuint(tb_jmp_cache_hash_func(s[i].pc), ==,
                         tb_jmp_cache_hash_func(s[0].pc));
        fill(jc, s[i], &blocks[i]);
    }
    /* Nine displaced entries exceed the eight-entry FIFO by exactly one. */
    g_assert_null(recover(jc, s[0]));
    g_assert_true(recover(jc, s[1]) == &blocks[1]);
    g_assert_true(recover(jc, s[8]) == &blocks[8]);
    g_assert_true(recover(jc, s[9]) == &blocks[9]);
    g_free(jc);
}

static void test_distinct_primary(void)
{
    CPUJumpCache *jc = g_new0(CPUJumpCache, 1);
    TCGTBCPUState a = state_for(0x80000000), b = state_for(0x80000004);
    TranslationBlock ta = block_for(a), tb = block_for(b);

    g_assert_cmpuint(tb_jmp_cache_hash_func(a.pc), !=,
                     tb_jmp_cache_hash_func(b.pc));
    fill(jc, a, &ta);
    fill(jc, b, &tb);
    g_assert_true(qatomic_read(&jc->array[tb_jmp_cache_hash_func(a.pc)].tb) ==
                  &ta);
    g_assert_true(qatomic_read(&jc->array[tb_jmp_cache_hash_func(b.pc)].tb) ==
                  &tb);
    g_assert_null(recover(jc, a));
    g_assert_null(recover(jc, b));
    g_free(jc);
}

static void test_virtual_aliases(void)
{
    CPUJumpCache *jc = g_new0(CPUJumpCache, 1);
    TCGTBCPUState a = state_for(0x80000000), b = state_for(0x81000000);
    TranslationBlock tb = block_for(a);

    /* CF_PCREL allows one physical TB at distinct virtual PCs. */
    fill(jc, a, &tb);
    fill(jc, b, &tb);
    g_assert_true(recover(jc, a) == &tb);
    g_assert_cmpuint(jc->array[tb_jmp_cache_hash_func(a.pc)].pc, ==, a.pc);
    g_assert_true(recover(jc, b) == &tb);
    g_assert_cmpuint(jc->array[tb_jmp_cache_hash_func(b.pc)].pc, ==, b.pc);
    g_free(jc);
}

static void test_full_clear(void)
{
    CPUJumpCache *jc = g_new0(CPUJumpCache, 1);
    TCGTBCPUState a = state_for(0x80000000), b = state_for(0x81000000);
    TranslationBlock ta = block_for(a), tb = block_for(b);

    fill(jc, a, &ta);
    fill(jc, b, &tb);
    tcg_jump_cache_clear_all(jc);
    g_assert_null(recover(jc, a));
    g_assert_null(qatomic_read(&jc->array[tb_jmp_cache_hash_func(b.pc)].tb));
    /* Mapping invalidation matters even when the physical TB is still valid. */
    g_assert_cmpuint(tb_cflags(&ta), ==, a.cflags);
    g_free(jc);
}

static void test_page_clear(void)
{
    CPUJumpCache *jc = g_new0(CPUJumpCache, 1);
    TCGTBCPUState a = state_for(0x80000000), b = state_for(0x81000000);
    TCGTBCPUState outside = state_for(0x80002000);
    TranslationBlock ta = block_for(a), tb = block_for(b);
    TranslationBlock to = block_for(outside);

    fill(jc, a, &ta);
    fill(jc, b, &tb);
    fill(jc, outside, &to);
    tcg_jump_cache_clear_page(jc, a.pc);
    g_assert_null(recover(jc, a));
    g_assert_null(qatomic_read(&jc->array[tb_jmp_cache_hash_func(b.pc)].tb));
    g_assert_true(
        qatomic_read(&jc->array[tb_jmp_cache_hash_func(outside.pc)].tb) == &to);
    g_assert_cmpuint(tb_cflags(&ta), ==, a.cflags);
    g_free(jc);
}

static void test_spanning_page(void)
{
    CPUJumpCache *jc = g_new0(CPUJumpCache, 1);
    TCGTBCPUState a = state_for(0x80000ffe), b = state_for(0x81000ffe);
    TranslationBlock ta = block_for(a), tb = block_for(b);

    fill(jc, a, &ta);
    fill(jc, b, &tb);
    /* Same two production clear calls as the TLB page-flush path. */
    tcg_jump_cache_clear_page(jc, 0x80001000);
    g_assert_null(recover(jc, a));
    tcg_jump_cache_clear_page(jc, 0x80000000);
    g_assert_null(qatomic_read(&jc->array[tb_jmp_cache_hash_func(b.pc)].tb));
    g_assert_cmpuint(tb_cflags(&ta), ==, a.cflags);
    g_free(jc);
}

static void test_victim_only_targeted_clear(void)
{
    CPUJumpCache *jc = g_new0(CPUJumpCache, 1);
    TCGTBCPUState a = state_for(0x80000000), b = state_for(0x81000000);
    a.cflags &= ~CF_PCREL;
    b.cflags &= ~CF_PCREL;
    TranslationBlock ta = block_for(a), tb = block_for(b);
    unsigned hash = tb_jmp_cache_hash_func(a.pc);

    fill(jc, a, &ta);
    fill(jc, b, &tb);
    tcg_jump_cache_clear_tb(jc, hash, &ta);
    g_assert_true(qatomic_read(&jc->array[hash].tb) == &tb);
    g_assert_null(recover(jc, a));
    g_free(jc);
}

static void test_overlapping_invalidators(void)
{
    CPUJumpCache *jc = g_new0(CPUJumpCache, 1);
    TCGTBCPUState a = state_for(0x80000000);
    TranslationBlock ta = block_for(a);
    unsigned hash = tb_jmp_cache_hash_func(a.pc);

    tcg_victim_cache_invalidate_begin(jc);
    tcg_victim_cache_invalidate_begin(jc);
    tcg_victim_cache_invalidate_end(jc);
    uint64_t epoch = tcg_victim_cache_epoch(jc);
    g_assert_false(tcg_victim_cache_fill(jc, hash, a.pc, &ta, epoch));
    g_assert_null(qatomic_read(&jc->array[hash].tb));
    tcg_victim_cache_invalidate_end(jc);
    fill(jc, a, &ta);
    g_assert_true(qatomic_read(&jc->array[hash].tb) == &ta);
    tcg_victim_cache_invalidate_begin(NULL);
    tcg_victim_cache_invalidate_end(NULL);
    g_free(jc);
}

typedef struct ClearWorker {
    CPUJumpCache *jc;
    GMutex lock;
    GCond changed;
    unsigned phase;
} ClearWorker;

static void wait_phase(ClearWorker *worker, unsigned phase)
{
    g_mutex_lock(&worker->lock);
    while (worker->phase < phase) {
        g_cond_wait(&worker->changed, &worker->lock);
    }
    g_mutex_unlock(&worker->lock);
}

static void signal_phase(ClearWorker *worker, unsigned phase)
{
    g_mutex_lock(&worker->lock);
    worker->phase = phase;
    g_cond_broadcast(&worker->changed);
    g_mutex_unlock(&worker->lock);
}

static gpointer clear_worker(gpointer opaque)
{
    ClearWorker *worker = opaque;

    for (unsigned i = 0; i < 10000; i++) {
        wait_phase(worker, 2 * i + 1);
        tcg_jump_cache_clear_all(worker->jc);
        signal_phase(worker, 2 * i + 2);
    }
    return NULL;
}

static void test_delayed_publication(void)
{
    CPUJumpCache *jc = g_new0(CPUJumpCache, 1);
    TCGTBCPUState a = state_for(0x80000000), b = state_for(0x81000000);
    TranslationBlock ta = block_for(a), tb = block_for(b);
    ClearWorker worker = { .jc = jc };
    unsigned hash = tb_jmp_cache_hash_func(a.pc);
    GThread *thread = g_thread_new("victim-clear", clear_worker, &worker);

    for (unsigned i = 0; i < 10000; i++) {
        fill(jc, a, &ta);
        fill(jc, b, &tb);
        uint64_t epoch = tcg_victim_cache_epoch(jc);
        signal_phase(&worker, 2 * i + 1);
        wait_phase(&worker, 2 * i + 2);
        /* Controlled old global/generated result arrives after the clear. */
        g_assert_false(tcg_victim_cache_fill(jc, hash, a.pc, &ta, epoch));
        g_assert_null(qatomic_read(&jc->array[hash].tb));
        g_assert_null(recover(jc, b));
    }
    g_thread_join(thread);
    g_cond_clear(&worker.changed);
    g_mutex_clear(&worker.lock);
    g_free(jc);
}

#ifndef _WIN32
static void test_guarded_storage_reuse(void)
{
    CPUJumpCache *jc = g_new0(CPUJumpCache, 1);
    TCGTBCPUState a = state_for(0x80000000), b = state_for(0x81000000);
    size_t bytes = QEMU_ALIGN_UP(sizeof(TranslationBlock), getpagesize());
    TranslationBlock *ta = mmap(NULL, bytes, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    TranslationBlock tb = block_for(b);

    g_assert_true(ta != MAP_FAILED);
    *ta = block_for(a);
    fill(jc, a, ta);
    fill(jc, b, &tb);
    g_assert_cmpint(mprotect(ta, bytes, PROT_NONE), ==, 0);
    tcg_jump_cache_clear_all(jc);
    /* Old storage must be rejected before any remembered pointer is read. */
    g_assert_null(recover(jc, a));
    g_assert_cmpint(mprotect(ta, bytes, PROT_READ | PROT_WRITE), ==, 0);
    *ta = block_for(a); /* Same-address, same-key ABA reuse. */
    g_assert_null(recover(jc, a));
    fill(jc, a, ta);
    fill(jc, b, &tb);
    g_assert_true(recover(jc, a) == ta);
    g_free(jc);
    g_assert_cmpint(munmap(ta, bytes), ==, 0);
}
#endif
#endif

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_message(
        "CPUJumpCache bytes=%zu; primary offset=%zu; primary bytes=%zu",
        sizeof(CPUJumpCache), offsetof(CPUJumpCache, array),
        sizeof(((CPUJumpCache *)0)->array));
    g_test_add_func("/tcg/victim8/collision-recovery", test_collision_recovery);
#if defined(CONFIG_XEMU_TCG_VICTIM_CACHE) || defined(TCG_VICTIM_CACHE_UNIT_TEST)
    g_test_add_func("/tcg/victim8/complete-key", test_complete_key);
    g_test_add_func("/tcg/victim8/invalid-block", test_invalid_block);
    g_test_add_func("/tcg/victim8/capacity-fifo", test_capacity_fifo);
    g_test_add_func("/tcg/victim8/distinct-primary", test_distinct_primary);
    g_test_add_func("/tcg/victim8/virtual-aliases", test_virtual_aliases);
    g_test_add_func("/tcg/victim8/full-clear", test_full_clear);
    g_test_add_func("/tcg/victim8/page-clear", test_page_clear);
    g_test_add_func("/tcg/victim8/spanning-page", test_spanning_page);
    g_test_add_func("/tcg/victim8/victim-only-targeted-clear",
                    test_victim_only_targeted_clear);
    g_test_add_func("/tcg/victim8/overlapping-invalidators",
                    test_overlapping_invalidators);
    g_test_add_func("/tcg/victim8/delayed-publication-10000",
                    test_delayed_publication);
#ifndef _WIN32
    g_test_add_func("/tcg/victim8/guarded-storage-reuse",
                    test_guarded_storage_reuse);
#endif
#endif
    return g_test_run();
}

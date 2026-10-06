/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
/* Exercise the optional cache layout even in default-off builds. The override
 * must follow osdep.h, which imports the generated build configuration.
 */
#ifndef CONFIG_XEMU_TCG_JUMP_CACHE_PROBE
#define CONFIG_XEMU_TCG_JUMP_CACHE_PROBE
#endif
#include "accel/tcg/jump-cache-probe-lookup.h"
#include "accel/tcg/tb-jmp-cache.h"

/* The conflict model must be explicitly selected. Ordinary counters must not
 * quietly acquire shadow-cache work or sampled timers.
 */
static void test_conflict_mode(void)
{
    TCGJumpCacheProbe *probe = tcg_jump_cache_probe_new("conflicts");
    TCGJumpCacheProbeStats stats;

    g_assert_nonnull(probe);
    g_assert_false(probe->owner.timing);
    tcg_jump_cache_probe_snapshot(probe, &stats);
    g_assert_cmpuint(stats.mode, ==, 9);
    tcg_jump_cache_probe_free(probe);
}

static void test_miss_reasons(void)
{
    TCGJumpCacheProbe *probe = tcg_jump_cache_probe_new("conflicts");
    TCGJumpCacheProbeStats stats;
    char token;

    for (unsigned hit = 0; hit < 2; hit++) {
        tcg_jump_cache_probe_record_miss(probe, NULL, 99, 42, hit);
        tcg_jump_cache_probe_record_miss(probe, (void *)&token, 43, 42, hit);
        tcg_jump_cache_probe_record_miss(probe, (void *)&token, 42, 42, hit);
    }
    tcg_jump_cache_probe_publish_owner(probe);
    tcg_jump_cache_probe_snapshot(probe, &stats);
    for (unsigned reason = 0; reason < 3; reason++) {
        g_assert_cmpuint(stats.conflicts.misses[reason][0], ==, 1);
        g_assert_cmpuint(stats.conflicts.misses[reason][1], ==, 1);
    }
    tcg_jump_cache_probe_free(probe);
}

static void fill(TCGJumpCacheProbe *probe, uint64_t epoch, vaddr old_pc,
                 TranslationBlock *old_tb, vaddr pc, TranslationBlock *tb,
                 bool global_hit)
{
    tcg_jump_cache_probe_record_fill(probe, 0, epoch, old_pc, old_tb, pc, tb,
                                     global_hit);
}

static void test_recovered_displacement(void)
{
    TCGJumpCacheProbe *probe = tcg_jump_cache_probe_new("conflicts");
    TCGJumpCacheProbeStats stats;
    char a, b;

    /* Hand-checked A -> B -> A: A was evicted, not merely absent. */
    fill(probe, 0, 0, NULL, 0x1000, (void *)&a, false);
    fill(probe, 0, 0x1000, (void *)&a, 0x1040, (void *)&b, true);
    fill(probe, 0, 0x1040, (void *)&b, 0x1000, (void *)&a, true);
    tcg_jump_cache_probe_publish_owner(probe);
    tcg_jump_cache_probe_snapshot(probe, &stats);
    g_assert_cmpuint(stats.conflicts.recovered[0], ==, 1);
    g_assert_cmpuint(stats.conflicts.recovered[1], ==, 1);
    tcg_jump_cache_probe_free(probe);
}

static void test_identity_not_pc_only(void)
{
    TCGJumpCacheProbe *probe = tcg_jump_cache_probe_new("conflicts");
    TCGJumpCacheProbeStats stats;
    char a, b, c;

    fill(probe, 0, 0x1000, (void *)&a, 0x1040, (void *)&b, true);
    /* Same PC, different validated translation/state cannot recover A. */
    fill(probe, 0, 0, NULL, 0x1000, (void *)&c, true);
    /* Same physical PC-relative TB, different virtual PC is not A's key. */
    fill(probe, 0, 0, NULL, 0x2000, (void *)&a, true);
    tcg_jump_cache_probe_publish_owner(probe);
    tcg_jump_cache_probe_snapshot(probe, &stats);
    g_assert_cmpuint(stats.conflicts.recovered[0], ==, 0);
    g_assert_cmpuint(stats.conflicts.recovered[1], ==, 0);
    /* The real key still recovers; neither false request consumed it. */
    fill(probe, 0, 0, NULL, 0x1000, (void *)&a, true);
    tcg_jump_cache_probe_publish_owner(probe);
    tcg_jump_cache_probe_snapshot(probe, &stats);
    g_assert_cmpuint(stats.conflicts.recovered[0], ==, 1);
    g_assert_cmpuint(stats.conflicts.recovered[1], ==, 1);
    tcg_jump_cache_probe_free(probe);
}

static void test_capacity(void)
{
    TCGJumpCacheProbe *probe = tcg_jump_cache_probe_new("conflicts");
    TCGJumpCacheProbeStats stats;
    char token[10];

    /* Nine distinct displacements: the oldest fits 16 entries, not eight. */
    for (unsigned i = 0; i < 9; i++) {
        fill(probe, 0, 0x1000 + i * 64, (void *)&token[i], 0x1040 + i * 64,
             (void *)&token[i + 1], true);
    }
    fill(probe, 0, 0, NULL, 0x1000, (void *)&token[0], true);
    tcg_jump_cache_probe_publish_owner(probe);
    tcg_jump_cache_probe_snapshot(probe, &stats);
    g_assert_cmpuint(stats.conflicts.recovered[0], ==, 0);
    g_assert_cmpuint(stats.conflicts.recovered[1], ==, 1);
    tcg_jump_cache_probe_free(probe);
}

static void test_new_translation_not_recovery(void)
{
    TCGJumpCacheProbe *probe = tcg_jump_cache_probe_new("conflicts");
    TCGJumpCacheProbeStats stats;
    char a, b;

    fill(probe, 0, 0x1000, (void *)&a, 0x1040, (void *)&b, true);
    fill(probe, 0, 0, NULL, 0x1000, (void *)&a, false);
    tcg_jump_cache_probe_publish_owner(probe);
    tcg_jump_cache_probe_snapshot(probe, &stats);
    g_assert_cmpuint(stats.conflicts.recovered[0], ==, 0);
    g_assert_cmpuint(stats.conflicts.recovered[1], ==, 0);
    tcg_jump_cache_probe_free(probe);
}

static void test_invalidated_identity(void)
{
    TCGJumpCacheProbe *probe = tcg_jump_cache_probe_new("conflicts");
    TCGJumpCacheProbeStats stats;
    char a, b;

    fill(probe, 0, 0x1000, (void *)&a, 0x1040, (void *)&b, true);
    tcg_jump_cache_probe_invalidate_begin(probe);
    tcg_jump_cache_probe_invalidate_end(probe);
    /* Reusing the same pointer/PC after invalidation is not recovery. */
    uint64_t epoch = tcg_jump_cache_probe_conflict_epoch(probe);
    fill(probe, epoch, 0, NULL, 0x1000, (void *)&a, true);
    tcg_jump_cache_probe_publish_owner(probe);
    tcg_jump_cache_probe_snapshot(probe, &stats);
    g_assert_cmpuint(stats.conflicts.recovered[0], ==, 0);
    g_assert_cmpuint(stats.conflicts.recovered[1], ==, 0);
    g_assert_cmpuint(stats.conflicts.shadow_resets, ==, 1);
    tcg_jump_cache_probe_free(probe);
}

static void test_invalidation_during_lookup(void)
{
    TCGJumpCacheProbe *probe = tcg_jump_cache_probe_new("conflicts");
    TCGJumpCacheProbeStats stats;
    char a, b, c;

    uint64_t old_epoch = tcg_jump_cache_probe_conflict_epoch(probe);
    tcg_jump_cache_probe_invalidate_begin(probe);
    tcg_jump_cache_probe_invalidate_end(probe);
    /* A captured primary entry from the old generation must not reappear. */
    fill(probe, old_epoch, 0x1000, (void *)&a, 0x1040, (void *)&b, true);
    uint64_t epoch = tcg_jump_cache_probe_conflict_epoch(probe);
    fill(probe, epoch, 0x1040, (void *)&b, 0x1000, (void *)&a, true);
    /* Two overlapping clear writers: one ending does not make a fill safe. */
    tcg_jump_cache_probe_invalidate_begin(probe);
    tcg_jump_cache_probe_invalidate_begin(probe);
    tcg_jump_cache_probe_invalidate_end(probe);
    epoch = tcg_jump_cache_probe_conflict_epoch(probe);
    fill(probe, epoch, 0x1000, (void *)&a, 0x1080, (void *)&c, true);
    tcg_jump_cache_probe_invalidate_end(probe);
    epoch = tcg_jump_cache_probe_conflict_epoch(probe);
    fill(probe, epoch, 0, NULL, 0x1000, (void *)&a, true);
    tcg_jump_cache_probe_publish_owner(probe);
    tcg_jump_cache_probe_snapshot(probe, &stats);
    g_assert_cmpuint(stats.conflicts.recovered[0], ==, 0);
    g_assert_cmpuint(stats.conflicts.recovered[1], ==, 0);
    g_assert_cmpuint(stats.conflicts.overlapped_fills, ==, 2);
    tcg_jump_cache_probe_free(probe);
}

static void test_counters_do_not_model(void)
{
    TCGJumpCacheProbe *probe = tcg_jump_cache_probe_new("counters");
    TCGJumpCacheProbeStats stats;
    char a, b;

    tcg_jump_cache_probe_record_miss(probe, NULL, 0, 0x1000, true);
    fill(probe, 0, 0x1000, (void *)&a, 0x1040, (void *)&b, true);
    fill(probe, 0, 0x1040, (void *)&b, 0x1000, (void *)&a, true);
    tcg_jump_cache_probe_publish_owner(probe);
    tcg_jump_cache_probe_snapshot(probe, &stats);
    g_assert_cmpuint(stats.conflicts.misses[0][1], ==, 0);
    g_assert_cmpuint(stats.conflicts.recovered[0], ==, 0);
    g_assert_cmpuint(stats.conflicts.recovered[1], ==, 0);
    tcg_jump_cache_probe_free(probe);
}

static void late_primary_publication(bool global_hit)
{
    TCGJumpCacheProbe *probe = tcg_jump_cache_probe_new("conflicts");
    TCGJumpCacheProbeStats stats;
    char a, b, c;

    /* The global lookup has returned B, but a clear completes before the
     * original primary store. That late store must not seed victim history
     * when C subsequently replaces B. Reuse of B's pointer is not recovery.
     */
    uint64_t old_epoch = tcg_jump_cache_probe_conflict_epoch(probe);
    tcg_jump_cache_probe_invalidate_begin(probe);
    tcg_jump_cache_probe_invalidate_end(probe);
    fill(probe, old_epoch, 0x1000, (void *)&a, 0x1040, (void *)&b, global_hit);
    uint64_t epoch = tcg_jump_cache_probe_conflict_epoch(probe);
    fill(probe, epoch, 0x1040, (void *)&b, 0x1080, (void *)&c, true);
    fill(probe, epoch, 0x1080, (void *)&c, 0x1040, (void *)&b, true);
    tcg_jump_cache_probe_publish_owner(probe);
    tcg_jump_cache_probe_snapshot(probe, &stats);
    g_assert_cmpuint(stats.conflicts.recovered[0], ==, 0);
    g_assert_cmpuint(stats.conflicts.recovered[1], ==, 0);
    /* A current-generation, non-overlapping publication is still useful. */
    fill(probe, epoch, 0x1040, (void *)&b, 0x1080, (void *)&c, true);
    tcg_jump_cache_probe_publish_owner(probe);
    tcg_jump_cache_probe_snapshot(probe, &stats);
    g_assert_cmpuint(stats.conflicts.recovered[0], ==, 1);
    g_assert_cmpuint(stats.conflicts.recovered[1], ==, 1);
    tcg_jump_cache_probe_free(probe);
}

static void test_late_primary_publication(void)
{
    late_primary_publication(true);
}

static void test_late_generated_publication(void)
{
    late_primary_publication(false);
}

static void test_production_clear(bool partial)
{
    CPUJumpCache *jc = g_new0(CPUJumpCache, 1);
    TCGJumpCacheProbe *probe = tcg_jump_cache_probe_new("conflicts");
    TCGJumpCacheProbeStats stats;
    char a, b;

    jc->probe = probe;
    jc->array[0] = (TCGJumpCacheProbeSlot){ (void *)&b, 0x1040 };
    fill(probe, 0, 0x1000, (void *)&a, 0x1040, (void *)&b, true);
    if (partial) {
        tcg_jump_cache_clear_range(jc, 0, 1);
    } else {
        tcg_jump_cache_probe_clear(probe, jc->array, TB_JMP_CACHE_SIZE, true);
    }
    g_assert_null(jc->array[0].tb);
    uint64_t epoch = tcg_jump_cache_probe_conflict_epoch(probe);
    fill(probe, epoch, 0, NULL, 0x1000, (void *)&a, true);
    tcg_jump_cache_probe_publish_owner(probe);
    tcg_jump_cache_probe_snapshot(probe, &stats);
    g_assert_cmpuint(stats.conflicts.recovered[0], ==, 0);
    g_assert_cmpuint(stats.conflicts.recovered[1], ==, 0);
    g_assert_cmpuint(stats.conflicts.shadow_resets, ==, 1);
    tcg_jump_cache_probe_free(probe);
    g_free(jc);
}

static void test_full_clear(void)
{
    test_production_clear(false);
}

static void test_partial_clear(void)
{
    test_production_clear(true);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/tcg/jump-cache/conflicts/mode", test_conflict_mode);
    g_test_add_func("/tcg/jump-cache/conflicts/miss-reasons",
                    test_miss_reasons);
    g_test_add_func("/tcg/jump-cache/conflicts/recovery",
                    test_recovered_displacement);
    g_test_add_func("/tcg/jump-cache/conflicts/identity",
                    test_identity_not_pc_only);
    g_test_add_func("/tcg/jump-cache/conflicts/capacity", test_capacity);
    g_test_add_func("/tcg/jump-cache/conflicts/new-translation",
                    test_new_translation_not_recovery);
    g_test_add_func("/tcg/jump-cache/conflicts/invalidated-identity",
                    test_invalidated_identity);
    g_test_add_func("/tcg/jump-cache/conflicts/invalidation-during-lookup",
                    test_invalidation_during_lookup);
    g_test_add_func("/tcg/jump-cache/conflicts/counters-unchanged",
                    test_counters_do_not_model);
    g_test_add_func("/tcg/jump-cache/conflicts/late-primary-publication",
                    test_late_primary_publication);
    g_test_add_func("/tcg/jump-cache/conflicts/late-generated-publication",
                    test_late_generated_publication);
    g_test_add_func("/tcg/jump-cache/conflicts/full-clear", test_full_clear);
    g_test_add_func("/tcg/jump-cache/conflicts/partial-clear",
                    test_partial_clear);
    return g_test_run();
}

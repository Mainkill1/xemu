/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/atomic.h"
#include "accel/tcg/jump-cache-probe.h"

static void test_clear(void)
{
    TCGJumpCacheProbeSlot slots[4096] = {};
    TCGJumpCacheProbe *probe = tcg_jump_cache_probe_new(true);
    TCGJumpCacheProbeStats stats;
    char opaque_tb;

    for (unsigned i = 0; i < G_N_ELEMENTS(slots); i++) {
        slots[i].pc = 1000 + i;
    }
    slots[0].tb = slots[17].tb = slots[4095].tb = (void *)&opaque_tb;
    tcg_jump_cache_probe_clear(probe, slots, G_N_ELEMENTS(slots), true);
    for (unsigned i = 0; i < G_N_ELEMENTS(slots); i++) {
        g_assert_null(qatomic_read(&slots[i].tb));
        g_assert_cmpuint(slots[i].pc, ==, 1000 + i);
    }
    tcg_jump_cache_probe_snapshot(probe, &stats);
    g_assert_cmpuint(stats.flush[1].cost.calls, ==, 1);
    g_assert_cmpuint(stats.flush[1].slots, ==, 4096);
    g_assert_cmpuint(stats.flush[1].observed_nonnull, ==, 3);
    g_assert_cmpuint(stats.flush[1].maximum_nonnull, ==, 3);
    g_assert_cmpuint(stats.flush[1].occupancy[2], ==, 1);
    g_assert_cmpuint(stats.flush[0].cost.calls, ==, 0);

    tcg_jump_cache_probe_clear(probe, slots, G_N_ELEMENTS(slots), true);
    for (unsigned i = 0; i < G_N_ELEMENTS(slots); i++) {
        slots[i].tb = (void *)&opaque_tb;
    }
    tcg_jump_cache_probe_clear(probe, slots, G_N_ELEMENTS(slots), false);
    tcg_jump_cache_probe_snapshot(probe, &stats);
    g_assert_cmpuint(stats.flush[1].occupancy[0], ==, 1);
    g_assert_cmpuint(stats.flush[0].maximum_nonnull, ==, 4096);
    g_assert_cmpuint(stats.flush[0].occupancy[13], ==, 1);
    tcg_jump_cache_probe_free(probe);
}

static void test_disabled(void)
{
    char opaque_tb;
    TCGJumpCacheProbeSlot slot = { .tb = (void *)&opaque_tb, .pc = 42 };
    TCGJumpCacheProbeStats stats;
    g_assert_null(tcg_jump_cache_probe_new(false));
    tcg_jump_cache_probe_clear(NULL, &slot, 1, true);
    g_assert_null(slot.tb);
    g_assert_cmpuint(slot.pc, ==, 42);
    g_assert_cmpuint(tcg_jump_cache_probe_lookup_begin(NULL), ==, 0);
    tcg_jump_cache_probe_lookup_end(NULL, TCG_JUMP_CACHE_HIT, 0);
    tcg_jump_cache_probe_targeted(NULL, true);
    tcg_jump_cache_probe_codegen(NULL, false);
    tcg_jump_cache_probe_snapshot(NULL, &stats);
    g_assert_cmpuint(stats.lookup_sequence, ==, 0);
    tcg_jump_cache_probe_free(NULL);
}

static void test_counts_and_sampling(void)
{
    TCGJumpCacheProbe *probe = tcg_jump_cache_probe_new(true);
    TCGJumpCacheProbeStats stats;
    TCGJumpCacheProbeSlot slots[16] = {};
    GString *out = g_string_new(NULL);
    for (unsigned i = 0; i < 1024 * 1024; i++) {
        uint64_t start = tcg_jump_cache_probe_lookup_begin(probe);
        tcg_jump_cache_probe_lookup_end(probe, i % 3, start);
    }
    for (unsigned i = 0; i < 4096; i++) {
        tcg_jump_cache_probe_clear(probe, slots, G_N_ELEMENTS(slots), i % 2);
    }
    tcg_jump_cache_probe_targeted(probe, false);
    tcg_jump_cache_probe_targeted(probe, true);
    tcg_jump_cache_probe_codegen(probe, false);
    tcg_jump_cache_probe_codegen(probe, true);
    tcg_jump_cache_probe_snapshot(probe, &stats);
    g_assert_cmpuint(stats.lookup_sequence, ==, 1024 * 1024);
    for (unsigned i = 0; i < 3; i++) {
        g_assert_cmpuint(stats.lookup[i].calls, ==, (1024 * 1024 + 2 - i) / 3);
        g_assert_cmpuint(stats.lookup[i].samples, >, 100);
        g_assert_cmpuint(stats.lookup[i].samples, <, 600);
    }
    for (unsigned i = 0; i < 2; i++) {
        g_assert_cmpuint(stats.flush[i].cost.calls, ==, 2048);
        g_assert_cmpuint(stats.flush[i].cost.samples, >, 20);
        g_assert_cmpuint(stats.flush[i].cost.samples, <, 120);
        g_assert_cmpuint(stats.flush[i].observed_nonnull, ==, 0);
        g_assert_cmpuint(stats.flush[i].occupancy[0], ==, 2048);
    }
    g_assert_cmpuint(stats.targeted_invalidations, ==, 2);
    g_assert_cmpuint(stats.targeted_removals, ==, 1);
    g_assert_cmpuint(stats.generated, ==, 1);
    g_assert_cmpuint(stats.recycled, ==, 1);
    tcg_jump_cache_probe_format(probe, out, 7);
    g_assert_nonnull(strstr(out->str, "Jump-cache probe CPU 7"));
    g_assert_nonnull(strstr(out->str, "global_miss"));
    g_string_free(out, true);
    tcg_jump_cache_probe_free(probe);
}

static gpointer record_worker(gpointer opaque)
{
    TCGJumpCacheProbe *probe = opaque;
    for (unsigned i = 0; i < 30000; i++) {
        uint64_t start = tcg_jump_cache_probe_lookup_begin(probe);
        tcg_jump_cache_probe_lookup_end(probe, i % 3, start);
        tcg_jump_cache_probe_targeted(probe, true);
    }
    return NULL;
}

static void test_concurrent_writers(void)
{
    TCGJumpCacheProbe *probe = tcg_jump_cache_probe_new(true);
    TCGJumpCacheProbeStats stats;
    GThread *workers[4];
    for (unsigned i = 0; i < G_N_ELEMENTS(workers); i++) {
        workers[i] = g_thread_new("probe-test", record_worker, probe);
    }
    for (unsigned i = 0; i < G_N_ELEMENTS(workers); i++) {
        g_thread_join(workers[i]);
    }
    tcg_jump_cache_probe_snapshot(probe, &stats);
    g_assert_cmpuint(stats.lookup_sequence, ==, 120000);
    g_assert_cmpuint(stats.targeted_invalidations, ==, 120000);
    g_assert_cmpuint(stats.targeted_removals, ==, 120000);
    for (unsigned i = 0; i < 3; i++) {
        g_assert_cmpuint(stats.lookup[i].calls, ==, 40000);
    }
    tcg_jump_cache_probe_free(probe);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/tcg/jump-cache/clear", test_clear);
    g_test_add_func("/tcg/jump-cache/disabled", test_disabled);
    g_test_add_func("/tcg/jump-cache/counts-sampling",
                    test_counts_and_sampling);
    g_test_add_func("/tcg/jump-cache/concurrent-writers",
                    test_concurrent_writers);
    return g_test_run();
}

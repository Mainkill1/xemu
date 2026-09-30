/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/atomic.h"
#include "accel/tcg/jump-cache-probe-lookup.h"

static void test_deferred_lookup_publication(void)
{
    TCGJumpCacheProbe *probe = tcg_jump_cache_probe_new("counters");
    TCGJumpCacheProbeStats stats;

    for (unsigned i = 0; i < 7; i++) {
        uint64_t start = tcg_jump_cache_probe_lookup_begin(probe);
        tcg_jump_cache_probe_lookup_end(probe, TCG_JUMP_CACHE_HIT, start);
    }
    tcg_jump_cache_probe_snapshot(probe, &stats);
    g_assert_cmpuint(stats.lookup_sequence, ==, 0);
    g_assert_cmpuint(stats.lookup[TCG_JUMP_CACHE_HIT].calls, ==, 0);
    tcg_jump_cache_probe_publish_owner(probe);
    tcg_jump_cache_probe_snapshot(probe, &stats);
    g_assert_cmpuint(stats.lookup_sequence, ==, 7);
    g_assert_cmpuint(stats.lookup[TCG_JUMP_CACHE_HIT].calls, ==, 7);
    /* A fault can leave a started lookup without a completion. */
    tcg_jump_cache_probe_lookup_begin(probe);
    tcg_jump_cache_probe_publish_owner(probe);
    tcg_jump_cache_probe_snapshot(probe, &stats);
    g_assert_cmpuint(stats.lookup_sequence, ==, 8);
    g_assert_cmpuint(stats.lookup[TCG_JUMP_CACHE_HIT].calls, ==, 7);
    tcg_jump_cache_probe_free(probe);
}

static void test_periodic_lookup_publication(void)
{
    TCGJumpCacheProbe *probe = tcg_jump_cache_probe_new("counters");
    TCGJumpCacheProbeStats stats;
    for (unsigned i = 1; i < TCG_JUMP_CACHE_PROBE_PUBLICATION_INTERVAL; i++) {
        uint64_t start = tcg_jump_cache_probe_lookup_begin(probe);
        tcg_jump_cache_probe_lookup_end(probe, TCG_JUMP_CACHE_HIT, start);
    }
    tcg_jump_cache_probe_snapshot(probe, &stats);
    g_assert_cmpuint(stats.lookup_sequence, ==, 0);
    uint64_t start = tcg_jump_cache_probe_lookup_begin(probe);
    tcg_jump_cache_probe_lookup_end(probe, TCG_JUMP_CACHE_HIT, start);
    tcg_jump_cache_probe_snapshot(probe, &stats);
    g_assert_cmpuint(stats.lookup_sequence, ==,
                     TCG_JUMP_CACHE_PROBE_PUBLICATION_INTERVAL);
    g_assert_cmpuint(stats.lookup[TCG_JUMP_CACHE_HIT].calls, ==,
                     TCG_JUMP_CACHE_PROBE_PUBLICATION_INTERVAL - 1);
    tcg_jump_cache_probe_publish_owner(probe);
    tcg_jump_cache_probe_snapshot(probe, &stats);
    g_assert_cmpuint(stats.lookup[TCG_JUMP_CACHE_HIT].calls, ==,
                     TCG_JUMP_CACHE_PROBE_PUBLICATION_INTERVAL);
    tcg_jump_cache_probe_free(probe);
}

static void test_clear(void)
{
    TCGJumpCacheProbeSlot slots[4096] = {};
    TCGJumpCacheProbe *probe = tcg_jump_cache_probe_new("all");
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
    g_assert_cmpuint(stats.flush[1].observed_nonnull, ==, 0);
    g_assert_cmpuint(stats.flush[1].maximum_nonnull, ==, 0);
    g_assert_cmpuint(stats.flush[1].observed_clears, ==, 0);
    g_assert_cmpuint(stats.flush[0].cost.calls, ==, 0);

    tcg_jump_cache_probe_clear(probe, slots, G_N_ELEMENTS(slots), true);
    for (unsigned i = 0; i < G_N_ELEMENTS(slots); i++) {
        slots[i].tb = (void *)&opaque_tb;
    }
    tcg_jump_cache_probe_clear(probe, slots, G_N_ELEMENTS(slots), false);
    tcg_jump_cache_probe_snapshot(probe, &stats);
    g_assert_cmpuint(stats.flush[1].observed_clears, ==, 0);
    g_assert_cmpuint(stats.flush[0].observed_clears, ==, 0);
    g_assert_cmpuint(stats.flush[0].maximum_nonnull, ==, 0);
    tcg_jump_cache_probe_free(probe);
}

static void test_disabled(void)
{
    char opaque_tb;
    TCGJumpCacheProbeSlot slot = { .tb = (void *)&opaque_tb, .pc = 42 };
    TCGJumpCacheProbeStats stats;
    g_assert_null(tcg_jump_cache_probe_new(NULL));
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

static void test_sampled_occupancy(void)
{
    TCGJumpCacheProbe *probe = tcg_jump_cache_probe_new("all");
    TCGJumpCacheProbeSlot slots[16] = {};
    TCGJumpCacheProbeStats stats;
    char opaque_tb;
    uint64_t observed_clears = 0;

    for (unsigned n = 0; n < 1024; n++) {
        for (unsigned i = 0; i < G_N_ELEMENTS(slots); i++) {
            slots[i].tb = (void *)&opaque_tb;
            slots[i].pc = i;
        }
        tcg_jump_cache_probe_clear(probe, slots, G_N_ELEMENTS(slots), true);
        for (unsigned i = 0; i < G_N_ELEMENTS(slots); i++) {
            g_assert_null(slots[i].tb);
            g_assert_cmpuint(slots[i].pc, ==, i);
        }
    }
    tcg_jump_cache_probe_snapshot(probe, &stats);
    for (unsigned i = 0; i < TCG_JUMP_CACHE_PROBE_BINS; i++) {
        observed_clears += stats.flush[1].occupancy[i];
    }
    g_assert_cmpuint(stats.flush[1].cost.calls, ==, 1024);
    g_assert_cmpuint(stats.flush[1].slots, ==, 1024 * 16);
    g_assert_cmpuint(observed_clears, >, 5);
    g_assert_cmpuint(observed_clears, <, 80);
    g_assert_cmpuint(stats.flush[1].observed_clears, ==, observed_clears);
    g_assert_cmpuint(stats.flush[1].observed_slots, ==, observed_clears * 16);
    g_assert_cmpuint(stats.flush[1].observed_nonnull, ==, observed_clears * 16);
    tcg_jump_cache_probe_free(probe);
}

static void test_modes(void)
{
    const char *modes[] = { "counters", "occupancy", "timing", "all", "1" };
    const unsigned expected[] = {
        TCG_JUMP_CACHE_PROBE_COUNTERS,
        TCG_JUMP_CACHE_PROBE_COUNTERS | TCG_JUMP_CACHE_PROBE_OCCUPANCY,
        TCG_JUMP_CACHE_PROBE_COUNTERS | TCG_JUMP_CACHE_PROBE_TIMING,
        TCG_JUMP_CACHE_PROBE_COUNTERS | TCG_JUMP_CACHE_PROBE_OCCUPANCY |
            TCG_JUMP_CACHE_PROBE_TIMING,
        TCG_JUMP_CACHE_PROBE_COUNTERS | TCG_JUMP_CACHE_PROBE_OCCUPANCY |
            TCG_JUMP_CACHE_PROBE_TIMING,
    };

    g_assert_null(tcg_jump_cache_probe_new("off"));
    g_assert_null(tcg_jump_cache_probe_new("0"));
    g_assert_null(tcg_jump_cache_probe_new("unrecognized"));
    for (unsigned m = 0; m < G_N_ELEMENTS(modes); m++) {
        TCGJumpCacheProbe *probe = tcg_jump_cache_probe_new(modes[m]);
        TCGJumpCacheProbeStats stats;
        TCGJumpCacheProbeSlot slots[16] = {};
        char opaque_tb;

        for (unsigned i = 0; i < 65536; i++) {
            uint64_t start = tcg_jump_cache_probe_lookup_begin(probe);
            tcg_jump_cache_probe_lookup_end(probe, i % 3, start);
        }
        for (unsigned n = 0; n < 1024; n++) {
            for (unsigned i = 0; i < G_N_ELEMENTS(slots); i++) {
                slots[i].tb = (void *)&opaque_tb;
            }
            tcg_jump_cache_probe_clear(probe, slots, G_N_ELEMENTS(slots), true);
            for (unsigned i = 0; i < G_N_ELEMENTS(slots); i++) {
                g_assert_null(slots[i].tb);
            }
        }
        tcg_jump_cache_probe_publish_owner(probe);
        tcg_jump_cache_probe_snapshot(probe, &stats);
        g_assert_cmpuint(stats.mode, ==, expected[m]);
        g_assert_cmpuint(stats.lookup_sequence, ==, 65536);
        for (unsigned i = 0; i < 3; i++) {
            g_assert_cmpuint(stats.lookup[i].calls, ==, (65536 + 2 - i) / 3);
            if (stats.mode & TCG_JUMP_CACHE_PROBE_TIMING) {
                g_assert_cmpuint(stats.lookup[i].samples, >, 5);
            } else {
                g_assert_cmpuint(stats.lookup[i].samples, ==, 0);
                g_assert_cmpuint(stats.lookup[i].sample_ns, ==, 0);
            }
        }
        g_assert_cmpuint(stats.flush[1].slots, ==, 1024 * 16);
        if (stats.mode & TCG_JUMP_CACHE_PROBE_OCCUPANCY) {
            g_assert_cmpuint(stats.flush[1].observed_clears, >, 5);
            g_assert_cmpuint(stats.flush[1].observed_clears, <, 80);
        } else {
            g_assert_cmpuint(stats.flush[1].observed_clears, ==, 0);
            g_assert_cmpuint(stats.flush[1].observed_slots, ==, 0);
            g_assert_cmpuint(stats.flush[1].observed_nonnull, ==, 0);
        }
        g_assert_cmpuint(stats.flush[1].observed_nonnull, ==,
                         stats.flush[1].observed_slots);
        if (stats.mode & TCG_JUMP_CACHE_PROBE_TIMING) {
            g_assert_cmpuint(stats.flush[1].cost.samples, >, 5);
        } else {
            g_assert_cmpuint(stats.flush[1].cost.samples, ==, 0);
            g_assert_cmpuint(stats.flush[1].cost.sample_ns, ==, 0);
        }
        tcg_jump_cache_probe_free(probe);
    }
}

typedef struct OwnerReader {
    TCGJumpCacheProbe *probe;
    unsigned done;
} OwnerReader;

static gpointer owner_worker(gpointer opaque)
{
    OwnerReader *state = opaque;
    for (unsigned i = 0; i < 300000; i++) {
        uint64_t start = tcg_jump_cache_probe_lookup_begin(state->probe);
        tcg_jump_cache_probe_lookup_end(state->probe, i % 3, start);
    }
    tcg_jump_cache_probe_publish_owner(state->probe);
    qatomic_set(&state->done, 1);
    return NULL;
}

static void test_owner_and_reader(void)
{
    OwnerReader state = { .probe = tcg_jump_cache_probe_new("timing") };
    TCGJumpCacheProbeStats stats;
    uint64_t last[3] = {};
    GThread *owner = g_thread_new("lookup-owner", owner_worker, &state);

    do {
        tcg_jump_cache_probe_snapshot(state.probe, &stats);
        for (unsigned i = 0; i < 3; i++) {
            g_assert_cmpuint(stats.lookup[i].calls, >=, last[i]);
            last[i] = stats.lookup[i].calls;
        }
    } while (!qatomic_read(&state.done));
    g_thread_join(owner);
    tcg_jump_cache_probe_snapshot(state.probe, &stats);
    g_assert_cmpuint(stats.lookup_sequence, ==, 300000);
    for (unsigned i = 0; i < 3; i++) {
        g_assert_cmpuint(stats.lookup[i].calls, ==, 100000);
    }
    tcg_jump_cache_probe_free(state.probe);
}

static void test_counts_and_sampling(void)
{
    TCGJumpCacheProbe *probe = tcg_jump_cache_probe_new("all");
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
    tcg_jump_cache_probe_publish_owner(probe);
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
        g_assert_cmpuint(stats.flush[i].occupancy[0], ==,
                         stats.flush[i].observed_clears);
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
    TCGJumpCacheProbeSlot slots[16] = {};
    for (unsigned i = 0; i < 30000; i++) {
        tcg_jump_cache_probe_targeted(probe, true);
        tcg_jump_cache_probe_clear(probe, slots, G_N_ELEMENTS(slots), true);
    }
    return NULL;
}

static void test_concurrent_writers(void)
{
    TCGJumpCacheProbe *probe = tcg_jump_cache_probe_new("all");
    TCGJumpCacheProbeStats stats;
    GThread *workers[4];
    for (unsigned i = 0; i < G_N_ELEMENTS(workers); i++) {
        workers[i] = g_thread_new("probe-test", record_worker, probe);
    }
    for (unsigned i = 0; i < G_N_ELEMENTS(workers); i++) {
        g_thread_join(workers[i]);
    }
    tcg_jump_cache_probe_snapshot(probe, &stats);
    g_assert_cmpuint(stats.lookup_sequence, ==, 0);
    g_assert_cmpuint(stats.targeted_invalidations, ==, 120000);
    g_assert_cmpuint(stats.targeted_removals, ==, 120000);
    g_assert_cmpuint(stats.flush[1].cost.calls, ==, 120000);
    g_assert_cmpuint(stats.flush[1].slots, ==, 120000 * 16);
    g_assert_cmpuint(stats.flush[1].observed_clears, >, 3000);
    g_assert_cmpuint(stats.flush[1].observed_clears, <, 4500);
    g_assert_cmpuint(stats.flush[1].occupancy[0], ==,
                     stats.flush[1].observed_clears);
    for (unsigned i = 0; i < 3; i++) {
        g_assert_cmpuint(stats.lookup[i].calls, ==, 0);
    }
    tcg_jump_cache_probe_free(probe);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/tcg/jump-cache/deferred-publication",
                    test_deferred_lookup_publication);
    g_test_add_func("/tcg/jump-cache/periodic-publication",
                    test_periodic_lookup_publication);
    g_test_add_func("/tcg/jump-cache/clear", test_clear);
    g_test_add_func("/tcg/jump-cache/disabled", test_disabled);
    g_test_add_func("/tcg/jump-cache/sampled-occupancy",
                    test_sampled_occupancy);
    g_test_add_func("/tcg/jump-cache/modes", test_modes);
    g_test_add_func("/tcg/jump-cache/owner-reader", test_owner_and_reader);
    g_test_add_func("/tcg/jump-cache/counts-sampling",
                    test_counts_and_sampling);
    g_test_add_func("/tcg/jump-cache/concurrent-writers",
                    test_concurrent_writers);
    return g_test_run();
}

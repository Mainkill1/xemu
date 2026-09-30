/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/atomic.h"
#include "jump-cache-probe.h"

static uint64_t clock_ns(clockid_t id)
{
    struct timespec t;
    g_assert_cmpint(clock_gettime(id, &t), ==, 0);
    return (uint64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}

int main(int argc, char **argv)
{
    g_assert_cmpint(argc, ==, 4);
    const char *mode = argv[1];
    uint64_t lookups = strtoull(argv[2], NULL, 10);
    uint64_t clears = strtoull(argv[3], NULL, 10);
#ifdef LEGACY_PROBE
    g_assert_true(!strcmp(mode, "all") || !strcmp(mode, "off"));
    TCGJumpCacheProbe *probe =
        tcg_jump_cache_probe_new(strcmp(mode, "off") != 0);
#else
    TCGJumpCacheProbe *probe = tcg_jump_cache_probe_new(mode);
#endif
    TCGJumpCacheProbeSlot *slots = g_new0(TCGJumpCacheProbeSlot, 4096);
    TCGJumpCacheProbeStats stats;
    uint64_t wall_start = clock_ns(CLOCK_MONOTONIC);
    uint64_t cpu_start = clock_ns(CLOCK_PROCESS_CPUTIME_ID);
    for (uint64_t i = 0; i < lookups; i++) {
        uint64_t start = tcg_jump_cache_probe_lookup_begin(probe);
        tcg_jump_cache_probe_lookup_end(probe, i % 3, start);
    }
    for (uint64_t i = 0; i < clears; i++) {
        tcg_jump_cache_probe_clear(probe, slots, 4096, true);
    }
    uint64_t cpu_ns = clock_ns(CLOCK_PROCESS_CPUTIME_ID) - cpu_start;
    uint64_t wall_ns = clock_ns(CLOCK_MONOTONIC) - wall_start;
    tcg_jump_cache_probe_snapshot(probe, &stats);
    uint64_t observed_clears = 0;
    for (unsigned i = 0; i < TCG_JUMP_CACHE_PROBE_BINS; i++) {
        observed_clears += stats.flush[1].occupancy[i];
    }
    if (probe) {
        g_assert_cmpuint(stats.lookup_sequence, ==, lookups);
        g_assert_cmpuint(stats.flush[1].cost.calls, ==, clears);
        g_assert_cmpuint(stats.flush[1].slots, ==, clears * 4096);
        for (unsigned i = 0; i < 3; i++) {
            g_assert_cmpuint(stats.lookup[i].calls, ==, (lookups + 2 - i) / 3);
        }
    }
    for (unsigned i = 0; i < 4096; i++) {
        g_assert_null(qatomic_read(&slots[i].tb));
    }
    printf("{\"mode\":\"%s\",\"lookups\":%" PRIu64 ",\"clears\":%" PRIu64
           ",\"cpu_ns\":%" PRIu64 ",\"wall_ns\":%" PRIu64
           ",\"occupancy_observed_clears\":%" PRIu64 ",\"checks\":\"pass\"}\n",
           mode, lookups, clears, cpu_ns, wall_ns, observed_clears);
    tcg_jump_cache_probe_free(probe);
    g_free(slots);
    return 0;
}

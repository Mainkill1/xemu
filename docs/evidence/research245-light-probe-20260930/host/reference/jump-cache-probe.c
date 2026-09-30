/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/atomic.h"
#include "qemu/timer.h"
#include "jump-cache-probe.h"

struct TCGJumpCacheProbe {
    TCGJumpCacheProbeStats stats;
};

/* Mix the event ordinal so periodic guest behavior does not alias the sample.
 */
static bool sample_ordinal(uint64_t ordinal, unsigned mask)
{
    ordinal ^= ordinal >> 30;
    ordinal *= UINT64_C(0xbf58476d1ce4e5b9);
    ordinal ^= ordinal >> 27;
    ordinal *= UINT64_C(0x94d049bb133111eb);
    ordinal ^= ordinal >> 31;
    return (ordinal & mask) == 0;
}

TCGJumpCacheProbe *tcg_jump_cache_probe_new(bool enabled)
{
    return enabled ? g_new0(TCGJumpCacheProbe, 1) : NULL;
}

void tcg_jump_cache_probe_free(TCGJumpCacheProbe *probe)
{
    g_free(probe);
}

uint64_t tcg_jump_cache_probe_lookup_begin(TCGJumpCacheProbe *probe)
{
    if (!probe) {
        return 0;
    }
    uint64_t ordinal = qatomic_fetch_add(&probe->stats.lookup_sequence, 1) + 1;
    return sample_ordinal(ordinal, 1023) ? get_clock() : 0;
}

static void record_cost(TCGJumpCacheProbeCost *cost, uint64_t start)
{
    if (start) {
        uint64_t end = get_clock();
        if (end >= start) {
            qatomic_inc(&cost->samples);
            qatomic_add(&cost->sample_ns, end - start);
        }
    }
}

void tcg_jump_cache_probe_lookup_end(TCGJumpCacheProbe *probe,
                                     TCGJumpCacheProbeLookup result,
                                     uint64_t sample_start_ns)
{
    if (probe) {
        assert(result < TCG_JUMP_CACHE_LOOKUP_CLASSES);
        TCGJumpCacheProbeCost *cost = &probe->stats.lookup[result];
        record_cost(cost, sample_start_ns);
        qatomic_inc(&cost->calls);
    }
}

void tcg_jump_cache_probe_clear(TCGJumpCacheProbe *probe,
                                TCGJumpCacheProbeSlot *slots, unsigned count,
                                bool pcrel)
{
    uint64_t observed = 0, start = 0;
    TCGJumpCacheProbeFlush *flush = probe ? &probe->stats.flush[pcrel] : NULL;
    if (flush) {
        uint64_t ordinal = qatomic_fetch_add(&flush->cost.calls, 1) + 1;
        if (sample_ordinal(ordinal, 31)) {
            start = get_clock();
        }
    }
    for (unsigned i = 0; i < count; i++) {
        if (probe) {
            /* Observation and clear are separate; never dereference the TB. */
            observed += qatomic_read(&slots[i].tb) != NULL;
        }
        /* Preserve the existing unconditional atomic clear, including NULL. */
        qatomic_set(&slots[i].tb, NULL);
    }
    if (flush) {
        record_cost(&flush->cost, start);
        qatomic_add(&flush->slots, count);
        qatomic_add(&flush->observed_nonnull, observed);
        uint64_t maximum = qatomic_read(&flush->maximum_nonnull);
        while (maximum < observed) {
            uint64_t previous =
                qatomic_cmpxchg(&flush->maximum_nonnull, maximum, observed);
            if (previous == maximum) {
                break;
            }
            maximum = previous;
        }
        unsigned bin = 0;
        for (uint64_t n = observed; n && bin < TCG_JUMP_CACHE_PROBE_BINS - 1;
             n >>= 1) {
            bin++;
        }
        qatomic_inc(&flush->occupancy[bin]);
    }
}

void tcg_jump_cache_probe_targeted(TCGJumpCacheProbe *probe, bool removed)
{
    if (probe) {
        qatomic_inc(&probe->stats.targeted_invalidations);
        if (removed) {
            qatomic_inc(&probe->stats.targeted_removals);
        }
    }
}

void tcg_jump_cache_probe_codegen(TCGJumpCacheProbe *probe, bool recycled)
{
    if (probe) {
        if (recycled) {
            qatomic_inc(&probe->stats.recycled);
        } else {
            qatomic_inc(&probe->stats.generated);
        }
    }
}

static void snapshot_cost(TCGJumpCacheProbeCost *from,
                          TCGJumpCacheProbeCost *to)
{
    to->calls = qatomic_read(&from->calls);
    to->samples = qatomic_read(&from->samples);
    to->sample_ns = qatomic_read(&from->sample_ns);
}

void tcg_jump_cache_probe_snapshot(TCGJumpCacheProbe *probe,
                                   TCGJumpCacheProbeStats *stats)
{
    memset(stats, 0, sizeof(*stats));
    if (!probe) {
        return;
    }
    TCGJumpCacheProbeStats *from = &probe->stats;
    stats->lookup_sequence = qatomic_read(&from->lookup_sequence);
    for (unsigned i = 0; i < TCG_JUMP_CACHE_LOOKUP_CLASSES; i++) {
        snapshot_cost(&from->lookup[i], &stats->lookup[i]);
    }
    for (unsigned i = 0; i < 2; i++) {
        TCGJumpCacheProbeFlush *f = &from->flush[i], *to = &stats->flush[i];
        snapshot_cost(&f->cost, &to->cost);
        to->slots = qatomic_read(&f->slots);
        to->observed_nonnull = qatomic_read(&f->observed_nonnull);
        to->maximum_nonnull = qatomic_read(&f->maximum_nonnull);
        for (unsigned b = 0; b < TCG_JUMP_CACHE_PROBE_BINS; b++) {
            to->occupancy[b] = qatomic_read(&f->occupancy[b]);
        }
    }
    stats->targeted_invalidations = qatomic_read(&from->targeted_invalidations);
    stats->targeted_removals = qatomic_read(&from->targeted_removals);
    stats->generated = qatomic_read(&from->generated);
    stats->recycled = qatomic_read(&from->recycled);
}

static void format_cost(GString *out, const char *name,
                        TCGJumpCacheProbeCost *cost)
{
    g_string_append_printf(out,
                           "jc %s calls=%" PRIu64 " samples=%" PRIu64
                           " sample_ns=%" PRIu64 "\n",
                           name, cost->calls, cost->samples, cost->sample_ns);
}

void tcg_jump_cache_probe_format(TCGJumpCacheProbe *probe, GString *out,
                                 int cpu_index)
{
    if (!probe) {
        return;
    }
    TCGJumpCacheProbeStats stats;
    tcg_jump_cache_probe_snapshot(probe, &stats);
    g_string_append_printf(out, "\nJump-cache probe CPU %d\n", cpu_index);
    g_string_append(
        out, "jc diagnostic: independent atomic fields; timings include "
             "instrumentation and occupancy reads; whole-cache clears only\n");
    g_string_append_printf(out,
                           "jc lookup_started=%" PRIu64 " generated=%" PRIu64
                           " recycled=%" PRIu64 "\n",
                           stats.lookup_sequence, stats.generated,
                           stats.recycled);
    format_cost(out, "hit", &stats.lookup[TCG_JUMP_CACHE_HIT]);
    format_cost(out, "global_hit", &stats.lookup[TCG_JUMP_CACHE_GLOBAL_HIT]);
    format_cost(out, "global_miss", &stats.lookup[TCG_JUMP_CACHE_GLOBAL_MISS]);
    for (unsigned i = 0; i < 2; i++) {
        TCGJumpCacheProbeFlush *f = &stats.flush[i];
        const char *name = i ? "pcrel_flush" : "other_flush";
        format_cost(out, name, &f->cost);
        g_string_append_printf(
            out,
            "jc %s slots=%" PRIu64 " observed_nonnull=%" PRIu64
            " maximum_nonnull=%" PRIu64 " occupancy=",
            name, f->slots, f->observed_nonnull, f->maximum_nonnull);
        for (unsigned b = 0; b < TCG_JUMP_CACHE_PROBE_BINS; b++) {
            g_string_append_printf(out, "%s%" PRIu64, b ? "," : "",
                                   f->occupancy[b]);
        }
        g_string_append_c(out, '\n');
    }
    g_string_append_printf(
        out,
        "jc targeted_invalidations=%" PRIu64 " targeted_removals=%" PRIu64 "\n",
        stats.targeted_invalidations, stats.targeted_removals);
}

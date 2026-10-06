/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/atomic.h"
#include "qemu/timer.h"
#include "jump-cache-probe-lookup.h"

struct TCGJumpCacheConflicts {
    TCGJumpCacheProbeSlot victim[2][16];
    unsigned count[2];
    uint64_t epoch;
    TCGJumpCacheConflictStats local;
    /* A primary store may follow a concurrent clear. Such a displaced token
     * cannot seed the model later, even after the model's epoch has advanced.
     * Dispatch owns this array; invalidators never touch it.
     */
    bool tainted[TCG_JUMP_CACHE_PROBE_PRIMARY_SLOTS];
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

TCGJumpCacheProbe *tcg_jump_cache_probe_new(const char *mode)
{
    unsigned selected;

    if (!mode || !strcmp(mode, "off") || !strcmp(mode, "0")) {
        return NULL;
    } else if (!strcmp(mode, "counters")) {
        selected = TCG_JUMP_CACHE_PROBE_COUNTERS;
    } else if (!strcmp(mode, "conflicts")) {
        selected =
            TCG_JUMP_CACHE_PROBE_COUNTERS | TCG_JUMP_CACHE_PROBE_CONFLICTS;
    } else if (!strcmp(mode, "occupancy")) {
        selected =
            TCG_JUMP_CACHE_PROBE_COUNTERS | TCG_JUMP_CACHE_PROBE_OCCUPANCY;
    } else if (!strcmp(mode, "timing")) {
        selected = TCG_JUMP_CACHE_PROBE_COUNTERS | TCG_JUMP_CACHE_PROBE_TIMING;
    } else if (!strcmp(mode, "all") || !strcmp(mode, "1")) {
        selected = TCG_JUMP_CACHE_PROBE_COUNTERS |
                   TCG_JUMP_CACHE_PROBE_OCCUPANCY | TCG_JUMP_CACHE_PROBE_TIMING;
    } else {
        return NULL;
    }
    TCGJumpCacheProbe *probe = g_new0(TCGJumpCacheProbe, 1);
    probe->mode = selected;
    probe->owner.timing = (selected & TCG_JUMP_CACHE_PROBE_TIMING) != 0;
    probe->owner.random = UINT32_C(0x9e3779b9);
    if (selected & TCG_JUMP_CACHE_PROBE_CONFLICTS) {
        probe->owner.conflicts = g_new0(TCGJumpCacheConflicts, 1);
    }
    return probe;
}

void tcg_jump_cache_probe_free(TCGJumpCacheProbe *probe)
{
    if (probe) {
        g_free(probe->owner.conflicts);
    }
    g_free(probe);
}

void tcg_jump_cache_probe_record_miss(TCGJumpCacheProbe *probe,
                                      TranslationBlock *old_tb, vaddr old_pc,
                                      vaddr pc, bool global_hit)
{
    if (probe && probe->owner.conflicts) {
        unsigned reason = !old_tb ? 0 : old_pc != pc ? 1 : 2;
        probe->owner.conflicts->local.misses[reason][global_hit]++;
    }
}

static void reset_victims(TCGJumpCacheConflicts *model, uint64_t epoch)
{
    memset(model->victim, 0, sizeof(model->victim));
    memset(model->count, 0, sizeof(model->count));
    model->epoch = epoch;
    model->local.shadow_resets++;
}

void tcg_jump_cache_probe_record_fill(TCGJumpCacheProbe *probe, unsigned hash,
                                      uint64_t epoch, vaddr old_pc,
                                      TranslationBlock *old_tb, vaddr pc,
                                      TranslationBlock *tb, bool global_hit)
{
    if (!probe || !probe->owner.conflicts) {
        return;
    }
    TCGJumpCacheConflicts *model = probe->owner.conflicts;
    assert(hash < TCG_JUMP_CACHE_PROBE_PRIMARY_SLOTS);
    bool old_tainted = model->tainted[hash];
    /* Order the real primary store before the publication-overlap check.
     * This is diagnostic-only and never alters the production cache.
     */
    smp_mb();
    uint64_t current = qatomic_load_acquire(&probe->conflict_epoch);
    bool recovered[2] = {};

    if (model->epoch != current) {
        reset_victims(model, current);
    }
    model->tainted[hash] = true;
    if (qatomic_load_acquire(&probe->conflict_invalidators) ||
        current != epoch) {
        model->local.overlapped_fills++;
        return;
    }
    /* Tokens are compared only after the real global lookup validates the
     * requested complete TB key. They are never dereferenced or executed.
     */
    for (unsigned v = 0; v < 2; v++) {
        TCGJumpCacheProbeSlot *slots = model->victim[v];
        unsigned count = model->count[v];
        unsigned capacity = v ? 16 : 8;

        for (unsigned i = 0; i < count; i++) {
            if (slots[i].pc == pc && slots[i].tb == tb) {
                recovered[v] = global_hit;
                memmove(&slots[i], &slots[i + 1],
                        (count - i - 1) * sizeof(*slots));
                count--;
                break;
            }
        }
        if (old_tb && !old_tainted && (old_pc != pc || old_tb != tb)) {
            bool present = false;
            for (unsigned i = 0; i < count; i++) {
                present |= slots[i].pc == old_pc && slots[i].tb == old_tb;
            }
            if (!present) {
                if (count == capacity) {
                    memmove(slots, slots + 1, (count - 1) * sizeof(*slots));
                    count--;
                }
                slots[count++] = (TCGJumpCacheProbeSlot){ old_tb, old_pc };
            }
        }
        model->count[v] = count;
    }
    /* Prevent the model's reads/updates from moving after the final check. */
    smp_mb();
    uint64_t after = qatomic_load_acquire(&probe->conflict_epoch);
    if (qatomic_load_acquire(&probe->conflict_invalidators) ||
        current != after) {
        reset_victims(model, after);
        model->local.overlapped_fills++;
        return;
    }
    model->tainted[hash] = false;
    for (unsigned v = 0; v < 2; v++) {
        model->local.recovered[v] += recovered[v];
    }
}

void tcg_jump_cache_probe_invalidate_begin(TCGJumpCacheProbe *probe)
{
    if (probe && (probe->mode & TCG_JUMP_CACHE_PROBE_CONFLICTS)) {
        qatomic_inc(&probe->conflict_invalidators);
        qatomic_inc(&probe->conflict_epoch);
    }
}

void tcg_jump_cache_probe_invalidate_end(TCGJumpCacheProbe *probe)
{
    if (probe && (probe->mode & TCG_JUMP_CACHE_PROBE_CONFLICTS)) {
        qatomic_inc(&probe->conflict_epoch);
        qatomic_dec(&probe->conflict_invalidators);
    }
}

void tcg_jump_cache_probe_publish_owner(TCGJumpCacheProbe *probe)
{
    if (!probe) {
        return;
    }
    qatomic_set(&probe->stats.lookup_sequence, probe->owner.sequence);
    for (unsigned i = 0; i < TCG_JUMP_CACHE_LOOKUP_CLASSES; i++) {
        TCGJumpCacheProbeCost *local = &probe->owner.cost[i];
        TCGJumpCacheProbeCost *published = &probe->stats.lookup[i];
        qatomic_set(&published->calls, local->calls);
        qatomic_set(&published->samples, local->samples);
        qatomic_set(&published->sample_ns, local->sample_ns);
    }
    if (probe->owner.conflicts) {
        TCGJumpCacheConflictStats *local = &probe->owner.conflicts->local;
        TCGJumpCacheConflictStats *published = &probe->stats.conflicts;
        for (unsigned i = 0; i < 3; i++) {
            for (unsigned j = 0; j < 2; j++) {
                qatomic_set(&published->misses[i][j], local->misses[i][j]);
            }
        }
        for (unsigned i = 0; i < 2; i++) {
            qatomic_set(&published->recovered[i], local->recovered[i]);
        }
        qatomic_set(&published->shadow_resets, local->shadow_resets);
        qatomic_set(&published->overlapped_fills, local->overlapped_fills);
    }
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

void tcg_jump_cache_probe_clear(TCGJumpCacheProbe *probe,
                                TCGJumpCacheProbeSlot *slots, unsigned count,
                                bool pcrel)
{
    uint64_t observed = 0, start = 0;
    tcg_jump_cache_probe_invalidate_begin(probe);
    bool observe = false;
    TCGJumpCacheProbeFlush *flush = probe ? &probe->stats.flush[pcrel] : NULL;
    if (flush) {
        uint64_t ordinal = qatomic_fetch_add(&flush->cost.calls, 1) + 1;
        bool sample = (probe->mode & (TCG_JUMP_CACHE_PROBE_OCCUPANCY |
                                      TCG_JUMP_CACHE_PROBE_TIMING)) &&
                      sample_ordinal(ordinal, 31);
        observe = sample && (probe->mode & TCG_JUMP_CACHE_PROBE_OCCUPANCY);
        if (sample && (probe->mode & TCG_JUMP_CACHE_PROBE_TIMING)) {
            start = get_clock();
        }
    }
    if (observe) {
        for (unsigned i = 0; i < count; i++) {
            /* Observation and clear are separate; never dereference the TB. */
            observed += qatomic_read(&slots[i].tb) != NULL;
            qatomic_set(&slots[i].tb, NULL);
        }
    } else {
        for (unsigned i = 0; i < count; i++) {
            /* Preserve every existing write, including NULL slots. */
            qatomic_set(&slots[i].tb, NULL);
        }
    }
    if (flush) {
        record_cost(&flush->cost, start);
        qatomic_add(&flush->slots, count);
    }
    if (observe) {
        qatomic_inc(&flush->observed_clears);
        qatomic_add(&flush->observed_slots, count);
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
    tcg_jump_cache_probe_invalidate_end(probe);
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
    stats->mode = probe->mode;
    stats->lookup_sequence = qatomic_read(&from->lookup_sequence);
    for (unsigned i = 0; i < TCG_JUMP_CACHE_LOOKUP_CLASSES; i++) {
        snapshot_cost(&from->lookup[i], &stats->lookup[i]);
    }
    for (unsigned i = 0; i < 2; i++) {
        TCGJumpCacheProbeFlush *f = &from->flush[i], *to = &stats->flush[i];
        snapshot_cost(&f->cost, &to->cost);
        to->slots = qatomic_read(&f->slots);
        to->observed_clears = qatomic_read(&f->observed_clears);
        to->observed_slots = qatomic_read(&f->observed_slots);
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
    for (unsigned i = 0; i < 3; i++) {
        for (unsigned j = 0; j < 2; j++) {
            stats->conflicts.misses[i][j] =
                qatomic_read(&from->conflicts.misses[i][j]);
        }
    }
    for (unsigned i = 0; i < 2; i++) {
        stats->conflicts.recovered[i] =
            qatomic_read(&from->conflicts.recovered[i]);
    }
    stats->conflicts.shadow_resets =
        qatomic_read(&from->conflicts.shadow_resets);
    stats->conflicts.overlapped_fills =
        qatomic_read(&from->conflicts.overlapped_fills);
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
    const char *mode;
    switch (stats.mode) {
    case TCG_JUMP_CACHE_PROBE_COUNTERS:
        mode = "counters";
        break;
    case TCG_JUMP_CACHE_PROBE_COUNTERS | TCG_JUMP_CACHE_PROBE_OCCUPANCY:
        mode = "occupancy";
        break;
    case TCG_JUMP_CACHE_PROBE_COUNTERS | TCG_JUMP_CACHE_PROBE_TIMING:
        mode = "timing";
        break;
    case TCG_JUMP_CACHE_PROBE_COUNTERS | TCG_JUMP_CACHE_PROBE_CONFLICTS:
        mode = "conflicts";
        break;
    default:
        mode = "all";
        break;
    }
    g_string_append_printf(out, "\nJump-cache probe CPU %d\n", cpu_index);
    g_string_append_printf(
        out,
        "jc diagnostic: mode=%s mask=%u independent atomic fields; lookup "
        "publication 1/65536 starts and execution yields; "
        "lookup timing nominal 1/1024; occupancy/clear timing nominal "
        "1/32 when selected; sampled maxima; whole-cache clears only\n",
        mode, stats.mode);
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
            "jc %s slots=%" PRIu64 " observed_clears=%" PRIu64
            " observed_slots=%" PRIu64 " observed_nonnull=%" PRIu64
            " maximum_nonnull=%" PRIu64 " occupancy=",
            name, f->slots, f->observed_clears, f->observed_slots,
            f->observed_nonnull, f->maximum_nonnull);
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
    if (stats.mode & TCG_JUMP_CACHE_PROBE_CONFLICTS) {
        const char *reason[] = { "empty", "pc_conflict", "translation_state" };
        for (unsigned i = 0; i < 3; i++) {
            g_string_append_printf(out,
                                   "jc miss_%s global_hit=%" PRIu64
                                   " global_miss=%" PRIu64 "\n",
                                   reason[i], stats.conflicts.misses[i][1],
                                   stats.conflicts.misses[i][0]);
        }
        g_string_append_printf(
            out,
            "jc victim8_recovered=%" PRIu64 " victim16_recovered=%" PRIu64
            " shadow_resets=%" PRIu64 " overlapped_fills=%" PRIu64 "\n",
            stats.conflicts.recovered[0], stats.conflicts.recovered[1],
            stats.conflicts.shadow_resets, stats.conflicts.overlapped_fills);
        g_string_append(
            out, "jc victim models: virtual-PC + validated-TB identity; FIFO; "
                 "all invalidations clear shadows; no cached execution; "
                 "diagnostic counts, not speedup\n");
    }
}

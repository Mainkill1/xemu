/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "jump-cache-probe-lookup.h"
#include "jump-cache-sites.h"

bool tcg_site_enabled(const TCGJumpCacheProbe *probe)
{
    return probe && (probe->mode & TCG_JUMP_CACHE_PROBE_SITES);
}

static bool key_equal(TCGSiteKey a, TCGSiteKey b)
{
    return a.pc == b.pc && a.cs_base == b.cs_base && a.flags == b.flags &&
           a.cflags == b.cflags;
}

TCGSiteObservation tcg_site_observe_begin(TCGJumpCacheProbe *probe,
                                          TCGSiteKey source, unsigned kind)
{
    TCGSiteObservation observation = { 0 };
    if (!probe || !probe->owner.sites) {
        return observation;
    }
    assert(kind < TCG_SITE_KIND_COUNT);
    TCGJumpCacheSites *sites = probe->owner.sites;
    uint64_t hash = (source.pc >> 1) ^ (source.pc >> 13) ^ source.cs_base ^
                    source.flags ^ source.cflags ^ kind;
    sites->attempts[kind]++;
    if (kind == TCG_SITE_OTHER) {
        sites->untracked[kind]++;
        return observation;
    }
    for (unsigned i = 0; i < TCG_SITE_PROFILE_PROBES; i++) {
        TCGSiteEntry *entry =
            &sites->entries[(hash + i) % TCG_SITE_PROFILE_CAPACITY];
        if (!entry->used) {
            entry->used = true;
            entry->kind = kind;
            entry->source = source;
            sites->used++;
        } else if (entry->kind != kind || !key_equal(entry->source, source)) {
            sites->collisions++;
            continue;
        }
        entry->attempts++;
        observation.entry = entry;
        observation.epoch = qatomic_load_acquire(&probe->conflict_epoch);
        observation.invalidating =
            qatomic_read(&probe->conflict_invalidators) != 0;
        return observation;
    }
    sites->untracked[kind]++;
    return observation;
}

void tcg_site_observe_end(TCGJumpCacheProbe *probe,
                          const TCGSiteObservation *observation,
                          TCGSiteKey target, TCGJumpCacheProbeLookup result)
{
    TCGSiteEntry *entry = observation->entry;
    if (!entry) {
        return;
    }
    assert(result < TCG_JUMP_CACHE_LOOKUP_CLASSES);
    entry->completed[result]++;
    uint64_t epoch = qatomic_load_acquire(&probe->conflict_epoch);
    if (observation->invalidating || observation->epoch != epoch ||
        qatomic_read(&probe->conflict_invalidators)) {
        entry->excluded++;
        entry->history_count = 0;
        return;
    }
    if (entry->epoch != epoch) {
        entry->resets++;
        entry->history_count = 0;
        entry->epoch = epoch;
    }
    if (result == TCG_JUMP_CACHE_GLOBAL_MISS) {
        /* The epilogue is not a resolved executable target. */
        return;
    }
    bool first =
        entry->history_count > 0 && key_equal(entry->recent[0], target);
    bool second =
        entry->history_count > 1 && key_equal(entry->recent[1], target);
    entry->recent_hits[0] += first;
    entry->recent_hits[1] += first || second;
    if (result == TCG_JUMP_CACHE_GLOBAL_HIT) {
        entry->global_recent_hits[0] += first;
        entry->global_recent_hits[1] += first || second;
    }
    if (!first) {
        entry->recent[1] = entry->recent[0];
        entry->recent[0] = target;
        entry->history_count = MIN(entry->history_count + 1, 2);
    }
}

void tcg_site_format_owner(TCGJumpCacheProbe *probe, GString *out, int cpu)
{
    if (!probe || !probe->owner.sites) {
        return;
    }
    TCGJumpCacheSites *sites = probe->owner.sites;
    g_string_append_printf(
        out,
        "sites cpu=%d capacity=%u probes=%u used=%u collisions=%" PRIu64 "\n",
        cpu, TCG_SITE_PROFILE_CAPACITY, TCG_SITE_PROFILE_PROBES, sites->used,
        sites->collisions);
    g_string_append(
        out,
        "sites diagnostic: owner snapshot; kinds=other,call,jump,return; "
        "full-key temporal recurrence, not executable-cache hits; "
        "attempts minus completed includes post-observation nonlocal exits; "
        "other transfers are unattributed; "
        "untracked events must not be extrapolated\n");
    for (unsigned kind = 0; kind < TCG_SITE_KIND_COUNT; kind++) {
        g_string_append_printf(
            out, "sites kind=%u attempts=%" PRIu64 " untracked=%" PRIu64 "\n",
            kind, sites->attempts[kind], sites->untracked[kind]);
    }
    for (unsigned i = 0; i < TCG_SITE_PROFILE_CAPACITY; i++) {
        TCGSiteEntry *e = &sites->entries[i];
        if (!e->used) {
            continue;
        }
        g_string_append_printf(
            out,
            "site slot=%u kind=%u pc=%" PRIx64 " cs=%" PRIx64
            " flags=%x cflags=%x attempts=%" PRIu64 " primary=%" PRIu64
            " global=%" PRIu64 " unresolved=%" PRIu64 " excluded=%" PRIu64
            " resets=%" PRIu64 " recent1=%" PRIu64 " recent2=%" PRIu64
            " global_recent1=%" PRIu64 " global_recent2=%" PRIu64 "\n",
            i, e->kind, e->source.pc, e->source.cs_base, e->source.flags,
            e->source.cflags, e->attempts, e->completed[0], e->completed[1],
            e->completed[2], e->excluded, e->resets, e->recent_hits[0],
            e->recent_hits[1], e->global_recent_hits[0],
            e->global_recent_hits[1]);
    }
}

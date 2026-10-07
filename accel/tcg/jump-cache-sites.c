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

void tcg_return_observe(TCGJumpCacheProbe *probe, TCGSiteKey target,
                        uint32_t stack_slot, unsigned event)
{
    if (!probe || !(probe->mode & TCG_JUMP_CACHE_PROBE_RETURNS)) {
        return;
    }
    assert(event <= TCG_RETURN_POP);
    TCGReturnProfile *r = &probe->owner.sites->returns;
    uint64_t epoch = qatomic_load_acquire(&probe->conflict_epoch);
    bool invalidating = qatomic_read(&probe->conflict_invalidators) != 0;
    if (event != TCG_RETURN_POP) {
        r->calls[event]++;
        r->entries[r->next] =
            (TCGReturnEntry){ target, epoch, stack_slot, invalidating };
        r->next = (r->next + 1) % TCG_RETURN_PROFILE_DEPTH;
        if (r->count == TCG_RETURN_PROFILE_DEPTH) {
            r->overflow++;
        } else {
            r->count++;
        }
        r->peak = MAX(r->peak, r->count);
        return;
    }
    r->pops++;
    if (!r->count) {
        r->underflow++;
        return;
    }
    r->next =
        (r->next + TCG_RETURN_PROFILE_DEPTH - 1) % TCG_RETURN_PROFILE_DEPTH;
    TCGReturnEntry *entry = &r->entries[r->next];
    r->count--;
    if (entry->target.pc != target.pc || entry->stack_slot != stack_slot) {
        r->mismatch++;
        /* Unknown unwinds/context switches must not invent correspondence. */
        r->count = 0;
        return;
    }
    r->matched++;
    if (!key_equal(entry->target, target)) {
        return;
    }
    r->context_matched++;
    /* Observed epoch stability is not executable-pointer lifetime proof. */
    r->epoch_stable +=
        !invalidating && !entry->invalidating && entry->epoch == epoch;
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
    if (probe->mode & TCG_JUMP_CACHE_PROBE_RETURNS) {
        TCGReturnProfile *r = &sites->returns;
        g_string_append_printf(
            out,
            "returns depth=%u count=%u peak=%u direct=%" PRIu64
            " indirect=%" PRIu64 " pops=%" PRIu64 " underflow=%" PRIu64
            " overflow=%" PRIu64 " mismatch=%" PRIu64 " matched=%" PRIu64
            " context_matched=%" PRIu64 " epoch_stable=%" PRIu64 "\n",
            TCG_RETURN_PROFILE_DEPTH, r->count, r->peak, r->calls[0],
            r->calls[1], r->pops, r->underflow, r->overflow, r->mismatch,
            r->matched, r->context_matched, r->epoch_stable);
        g_string_append(
            out, "returns diagnostic: successful near CALL/RET only; "
                 "i386 code32/stack32/operand32; direct calls included; "
                 "address+stack matches, then context, then observed epoch; "
                 "not executable-target availability or a speedup; "
                 "event totals differ from lookup-reaching sites\n");
    }

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

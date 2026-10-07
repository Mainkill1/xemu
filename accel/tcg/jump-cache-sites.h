/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ACCEL_TCG_JUMP_CACHE_SITES_H
#define ACCEL_TCG_JUMP_CACHE_SITES_H

#include "jump-cache-probe.h"

#define TCG_SITE_PROFILE_CAPACITY 65536
#define TCG_SITE_PROFILE_PROBES 8

enum TCGSiteKind {
    TCG_SITE_OTHER,
    TCG_SITE_CALL,
    TCG_SITE_JUMP,
    TCG_SITE_RETURN,
    TCG_SITE_KIND_COUNT,
};

typedef struct TCGSiteKey {
    uint64_t pc;
    uint64_t cs_base;
    uint32_t flags;
    uint32_t cflags;
} TCGSiteKey;

typedef struct TCGSiteEntry {
    bool used;
    unsigned kind;
    TCGSiteKey source;
    uint64_t attempts;
    uint64_t completed[TCG_JUMP_CACHE_LOOKUP_CLASSES];
    uint64_t excluded;
    uint64_t resets;
    uint64_t recent_hits[2];
    uint64_t global_recent_hits[2];
    uint64_t epoch;
    unsigned history_count;
    TCGSiteKey recent[2];
} TCGSiteEntry;

#define TCG_RETURN_PROFILE_DEPTH 64

enum TCGReturnEvent {
    TCG_RETURN_DIRECT_CALL,
    TCG_RETURN_INDIRECT_CALL,
    TCG_RETURN_POP,
};

typedef struct TCGReturnEntry {
    TCGSiteKey target;
    uint64_t epoch;
    uint32_t stack_slot;
    bool invalidating;
} TCGReturnEntry;

typedef struct TCGReturnProfile {
    uint64_t calls[2];
    uint64_t pops;
    uint64_t underflow;
    uint64_t overflow;
    uint64_t mismatch;
    uint64_t matched;
    uint64_t context_matched;
    uint64_t epoch_stable;
    unsigned next;
    unsigned count;
    unsigned peak;
    TCGReturnEntry entries[TCG_RETURN_PROFILE_DEPTH];
} TCGReturnProfile;

typedef struct TCGJumpCacheSites {
    uint64_t attempts[TCG_SITE_KIND_COUNT];
    uint64_t untracked[TCG_SITE_KIND_COUNT];
    TCGReturnProfile returns;
    uint64_t collisions;
    unsigned used;
    TCGSiteEntry entries[TCG_SITE_PROFILE_CAPACITY];
} TCGJumpCacheSites;

typedef struct TCGSiteObservation {
    TCGSiteEntry *entry;
    uint64_t epoch;
    bool invalidating;
} TCGSiteObservation;

/* Owner-only diagnostic. No host pointers or additional target lookup. */
void tcg_return_observe(TCGJumpCacheProbe *probe, TCGSiteKey target,
                        uint32_t stack_slot, unsigned event);

/* Dispatch owner only. Entries are never evicted or used for execution. */
TCGSiteObservation tcg_site_observe_begin(TCGJumpCacheProbe *probe,
                                          TCGSiteKey source, unsigned kind);
void tcg_site_observe_end(TCGJumpCacheProbe *probe,
                          const TCGSiteObservation *observation,
                          TCGSiteKey target, TCGJumpCacheProbeLookup result);
/* Immutable mode query; safe away from the dispatch owner. */
bool tcg_site_enabled(const TCGJumpCacheProbe *probe);
/* Caller must execute on the owning vCPU or after it is quiesced. */
void tcg_site_format_owner(TCGJumpCacheProbe *probe, GString *out, int cpu);

#endif

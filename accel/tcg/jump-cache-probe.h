/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ACCEL_TCG_JUMP_CACHE_PROBE_H
#define ACCEL_TCG_JUMP_CACHE_PROBE_H

#include "exec/vaddr.h"
#include "qemu/typedefs.h"

#define TCG_JUMP_CACHE_PROBE_BINS 14
#define TCG_JUMP_CACHE_PROBE_PRIMARY_SLOTS 4096

typedef struct TCGJumpCacheProbe TCGJumpCacheProbe;
typedef struct TCGJumpCacheConflicts TCGJumpCacheConflicts;
typedef struct TCGJumpCacheSites TCGJumpCacheSites;

typedef enum TCGJumpCacheProbeMode {
    TCG_JUMP_CACHE_PROBE_COUNTERS = 1,
    TCG_JUMP_CACHE_PROBE_OCCUPANCY = 2,
    TCG_JUMP_CACHE_PROBE_TIMING = 4,
    TCG_JUMP_CACHE_PROBE_CONFLICTS = 8,
    TCG_JUMP_CACHE_PROBE_SITES = 16,
    TCG_JUMP_CACHE_PROBE_RETURNS = 32,
} TCGJumpCacheProbeMode;

typedef struct TCGJumpCacheProbeSlot {
    TranslationBlock *tb;
    vaddr pc;
} TCGJumpCacheProbeSlot;

typedef enum TCGJumpCacheProbeLookup {
    TCG_JUMP_CACHE_HIT,
    TCG_JUMP_CACHE_GLOBAL_HIT,
    TCG_JUMP_CACHE_GLOBAL_MISS,
    TCG_JUMP_CACHE_LOOKUP_CLASSES,
} TCGJumpCacheProbeLookup;

typedef struct TCGJumpCacheProbeCost {
    uint64_t calls;
    uint64_t samples;
    uint64_t sample_ns;
} TCGJumpCacheProbeCost;

typedef struct TCGJumpCacheProbeFlush {
    TCGJumpCacheProbeCost cost;
    uint64_t slots;
    uint64_t observed_clears;
    uint64_t observed_slots;
    uint64_t observed_nonnull;
    uint64_t maximum_nonnull;
    uint64_t occupancy[TCG_JUMP_CACHE_PROBE_BINS];
} TCGJumpCacheProbeFlush;

typedef struct TCGJumpCacheConflictStats {
    uint64_t misses[3][2]; /* empty/PC/state, global miss/hit */
    uint64_t recovered[2]; /* eight/sixteen-entry FIFO victim models */
    uint64_t shadow_resets;
    uint64_t overlapped_fills;
} TCGJumpCacheConflictStats;

typedef struct TCGJumpCacheProbeStats {
    unsigned mode;
    uint64_t lookup_sequence;
    TCGJumpCacheProbeCost lookup[TCG_JUMP_CACHE_LOOKUP_CLASSES];
    TCGJumpCacheProbeFlush flush[2]; /* other, individual CF_PCREL */
    uint64_t targeted_invalidations;
    uint64_t targeted_removals;
    uint64_t generated;
    uint64_t recycled;
    TCGJumpCacheConflictStats conflicts;
} TCGJumpCacheProbeStats;

/* NULL/off disables; conflicts separately selects miss/victim attribution. */
TCGJumpCacheProbe *tcg_jump_cache_probe_new(const char *mode);
void tcg_jump_cache_probe_free(TCGJumpCacheProbe *probe);
/* Publish only on the dispatch owner or after its execution has quiesced.
 * Live readers use snapshot(), which never reads private owner state.
 * Shared clear/invalidation recording may run concurrently.
 */
void tcg_jump_cache_probe_publish_owner(TCGJumpCacheProbe *probe);
void tcg_jump_cache_probe_clear(TCGJumpCacheProbe *probe,
                                TCGJumpCacheProbeSlot *slots, unsigned count,
                                bool pcrel);
void tcg_jump_cache_probe_targeted(TCGJumpCacheProbe *probe, bool removed);
void tcg_jump_cache_probe_codegen(TCGJumpCacheProbe *probe, bool recycled);
void tcg_jump_cache_probe_record_miss(TCGJumpCacheProbe *probe,
                                      TranslationBlock *old_tb, vaddr old_pc,
                                      vaddr pc, bool global_hit);
/* Owner only, after the original primary-slot store. Capture epoch and the
 * displaced key before looking up/publishing the replacement.
 */
void tcg_jump_cache_probe_record_fill(TCGJumpCacheProbe *probe, unsigned hash,
                                      uint64_t epoch, vaddr old_pc,
                                      TranslationBlock *old_tb, vaddr pc,
                                      TranslationBlock *tb, bool global_hit);
void tcg_jump_cache_probe_invalidate_begin(TCGJumpCacheProbe *probe);
void tcg_jump_cache_probe_invalidate_end(TCGJumpCacheProbe *probe);
void tcg_jump_cache_probe_snapshot(TCGJumpCacheProbe *probe,
                                   TCGJumpCacheProbeStats *stats);
void tcg_jump_cache_probe_format(TCGJumpCacheProbe *probe, GString *out,
                                 int cpu_index);

#endif

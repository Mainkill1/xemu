/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ACCEL_TCG_JUMP_CACHE_PROBE_H
#define ACCEL_TCG_JUMP_CACHE_PROBE_H

#include "exec/vaddr.h"
#include "qemu/typedefs.h"

#define TCG_JUMP_CACHE_PROBE_BINS 14

typedef struct TCGJumpCacheProbe TCGJumpCacheProbe;

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
    uint64_t observed_nonnull;
    uint64_t maximum_nonnull;
    uint64_t occupancy[TCG_JUMP_CACHE_PROBE_BINS];
} TCGJumpCacheProbeFlush;

typedef struct TCGJumpCacheProbeStats {
    uint64_t lookup_sequence;
    TCGJumpCacheProbeCost lookup[TCG_JUMP_CACHE_LOOKUP_CLASSES];
    TCGJumpCacheProbeFlush flush[2]; /* other, individual CF_PCREL */
    uint64_t targeted_invalidations;
    uint64_t targeted_removals;
    uint64_t generated;
    uint64_t recycled;
} TCGJumpCacheProbeStats;

TCGJumpCacheProbe *tcg_jump_cache_probe_new(bool enabled);
void tcg_jump_cache_probe_free(TCGJumpCacheProbe *probe);
uint64_t tcg_jump_cache_probe_lookup_begin(TCGJumpCacheProbe *probe);
void tcg_jump_cache_probe_lookup_end(TCGJumpCacheProbe *probe,
                                     TCGJumpCacheProbeLookup result,
                                     uint64_t sample_start_ns);
void tcg_jump_cache_probe_clear(TCGJumpCacheProbe *probe,
                                TCGJumpCacheProbeSlot *slots, unsigned count,
                                bool pcrel);
void tcg_jump_cache_probe_targeted(TCGJumpCacheProbe *probe, bool removed);
void tcg_jump_cache_probe_codegen(TCGJumpCacheProbe *probe, bool recycled);
void tcg_jump_cache_probe_snapshot(TCGJumpCacheProbe *probe,
                                   TCGJumpCacheProbeStats *stats);
void tcg_jump_cache_probe_format(TCGJumpCacheProbe *probe, GString *out,
                                 int cpu_index);

#endif

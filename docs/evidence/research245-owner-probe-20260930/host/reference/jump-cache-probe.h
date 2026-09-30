/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ACCEL_TCG_JUMP_CACHE_PROBE_H
#define ACCEL_TCG_JUMP_CACHE_PROBE_H

#include "exec/vaddr.h"
#include "qemu/typedefs.h"

#define TCG_JUMP_CACHE_PROBE_BINS 14

typedef struct TCGJumpCacheProbe TCGJumpCacheProbe;

typedef enum TCGJumpCacheProbeMode {
    TCG_JUMP_CACHE_PROBE_COUNTERS = 1,
    TCG_JUMP_CACHE_PROBE_OCCUPANCY = 2,
    TCG_JUMP_CACHE_PROBE_TIMING = 4,
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

typedef struct TCGJumpCacheProbeStats {
    unsigned mode;
    uint64_t lookup_sequence;
    TCGJumpCacheProbeCost lookup[TCG_JUMP_CACHE_LOOKUP_CLASSES];
    TCGJumpCacheProbeFlush flush[2]; /* other, individual CF_PCREL */
    uint64_t targeted_invalidations;
    uint64_t targeted_removals;
    uint64_t generated;
    uint64_t recycled;
} TCGJumpCacheProbeStats;

/* NULL/off disables; counters, occupancy, timing, all/1 select collection. */
TCGJumpCacheProbe *tcg_jump_cache_probe_new(const char *mode);
void tcg_jump_cache_probe_free(TCGJumpCacheProbe *probe);
/* Begin/end have exactly one serialized dispatch writer per vCPU probe.
 * Private lookup state is never read by snapshot/clear/invalidation callers.
 * Other recording methods and snapshot readers may run concurrently.
 */
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

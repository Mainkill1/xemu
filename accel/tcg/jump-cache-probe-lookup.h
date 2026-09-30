/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ACCEL_TCG_JUMP_CACHE_PROBE_LOOKUP_H
#define ACCEL_TCG_JUMP_CACHE_PROBE_LOOKUP_H

#include "qemu/timer.h"
#include "jump-cache-probe.h"

#define TCG_JUMP_CACHE_PROBE_PUBLICATION_INTERVAL 65536

struct TCGJumpCacheProbe {
    struct {
        uint64_t sequence;
        TCGJumpCacheProbeCost cost[TCG_JUMP_CACHE_LOOKUP_CLASSES];
        uint32_t random;
        bool timing;
    } owner; /* Only the serialized vCPU dispatch thread may access this. */
    /* Keep shared clear statistics off the owner's 64-byte cache lines without
     * requiring a stronger allocation alignment than g_malloc provides.
     */
    uint8_t separation[64];
    unsigned mode; /* Immutable; shared writers read only this cold copy. */
    TCGJumpCacheProbeStats stats;
};

static inline uint64_t
tcg_jump_cache_probe_lookup_begin(TCGJumpCacheProbe *probe)
{
    if (!probe) {
        return 0;
    }
    uint64_t sequence = ++probe->owner.sequence;
    if (unlikely((sequence & (TCG_JUMP_CACHE_PROBE_PUBLICATION_INTERVAL - 1)) ==
                 0)) {
        tcg_jump_cache_probe_publish_owner(probe);
    }
    if (!probe->owner.timing) {
        return 0;
    }
    /* Cheap owner-local decision; avoid fixed-stride/class aliasing. */
    uint32_t random = probe->owner.random;
    random ^= random << 13;
    random ^= random >> 17;
    random ^= random << 5;
    probe->owner.random = random;
    return (random & 1023) == 0 ? get_clock() : 0;
}

static inline void
tcg_jump_cache_probe_lookup_end(TCGJumpCacheProbe *probe,
                                TCGJumpCacheProbeLookup result,
                                uint64_t sample_start_ns)
{
    if (probe) {
        assert(result < TCG_JUMP_CACHE_LOOKUP_CLASSES);
        TCGJumpCacheProbeCost *local = &probe->owner.cost[result];
        if (sample_start_ns) {
            uint64_t end = get_clock();
            if (end >= sample_start_ns) {
                local->samples++;
                local->sample_ns += end - sample_start_ns;
            }
        }
        local->calls++;
    }
}

#endif

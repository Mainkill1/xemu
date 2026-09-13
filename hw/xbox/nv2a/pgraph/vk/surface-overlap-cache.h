/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Exact range answers for a stable active-surface set.
 */
#ifndef HW_XBOX_NV2A_PGRAPH_VK_SURFACE_OVERLAP_CACHE_H
#define HW_XBOX_NV2A_PGRAPH_VK_SURFACE_OVERLAP_CACHE_H

#include <stdbool.h>
#include <stdint.h>

#define PGRAPH_VK_SURFACE_OVERLAP_CACHE_SIZE 64

typedef struct PGRAPHVkSurfaceOverlapCache {
    uint64_t generation;
    struct {
        uint64_t start;
        uint64_t size;
        uint64_t generation;
        bool overlap;
        bool valid;
    } entries[PGRAPH_VK_SURFACE_OVERLAP_CACHE_SIZE];
} PGRAPHVkSurfaceOverlapCache;

static inline unsigned int pgraph_vk_surface_overlap_cache_index(
    uint64_t start, uint64_t size)
{
    uint64_t key = (start >> 4) ^ (size * UINT64_C(0x9e3779b97f4a7c15));
    key ^= key >> 33;
    key *= UINT64_C(0xc2b2ae3d27d4eb4f);
    key ^= key >> 29;
    return key & (PGRAPH_VK_SURFACE_OVERLAP_CACHE_SIZE - 1);
}

static inline void pgraph_vk_surface_overlap_cache_invalidate(
    PGRAPHVkSurfaceOverlapCache *cache)
{
    cache->generation++;
    if (cache->generation == 0) {
        for (unsigned int i = 0; i < PGRAPH_VK_SURFACE_OVERLAP_CACHE_SIZE;
             i++) {
            cache->entries[i].valid = false;
        }
        cache->generation = 1;
    }
}

static inline bool pgraph_vk_surface_overlap_cache_lookup(
    PGRAPHVkSurfaceOverlapCache *cache, uint64_t start, uint64_t size,
    bool *overlap)
{
    unsigned int i = pgraph_vk_surface_overlap_cache_index(start, size);
    if (!cache->entries[i].valid ||
        cache->entries[i].generation != cache->generation ||
        cache->entries[i].start != start ||
        cache->entries[i].size != size) {
        return false;
    }
    *overlap = cache->entries[i].overlap;
    return true;
}

static inline void pgraph_vk_surface_overlap_cache_store(
    PGRAPHVkSurfaceOverlapCache *cache, uint64_t start, uint64_t size,
    bool overlap)
{
    unsigned int i = pgraph_vk_surface_overlap_cache_index(start, size);
    cache->entries[i].start = start;
    cache->entries[i].size = size;
    cache->entries[i].generation = cache->generation;
    cache->entries[i].overlap = overlap;
    cache->entries[i].valid = true;
}

#endif

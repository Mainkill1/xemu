/*
 * Immutable graphics descriptor reuse within one completed-fence lifetime.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef HW_XBOX_NV2A_PGRAPH_VK_DESCRIPTOR_REUSE_H
#define HW_XBOX_NV2A_PGRAPH_VK_DESCRIPTOR_REUSE_H

#include <stdint.h>
#include <string.h>

/*
 * All descriptors use the same set layout and shader-read-only image layout.
 * Handles, UBO offsets and ranges must still match exactly. The uber control
 * dynamic offset is supplied at bind time and is deliberately not a key.
 */
typedef struct PGRAPHVkDescriptorKey {
    uint64_t uniforms[2][3]; /* buffer, offset, range */
    uint64_t textures[4][2]; /* image view, sampler */
    uint64_t control_buffer;
    uint64_t control_range;
} PGRAPHVkDescriptorKey;

typedef struct PGRAPHVkDescriptorCache {
    struct {
        PGRAPHVkDescriptorKey key;
        unsigned int set_plus_one;
    } entries[256];
} PGRAPHVkDescriptorCache;

static inline int
pgraph_vk_descriptor_cache_find(const PGRAPHVkDescriptorCache *cache,
                                const PGRAPHVkDescriptorKey *key, uint64_t hash)
{
    unsigned int slot = hash & 255;
    if (cache->entries[slot].set_plus_one &&
        !memcmp(&cache->entries[slot].key, key, sizeof(*key))) {
        return cache->entries[slot].set_plus_one - 1;
    }
    return -1;
}

static inline void
pgraph_vk_descriptor_cache_insert(PGRAPHVkDescriptorCache *cache,
                                  const PGRAPHVkDescriptorKey *key,
                                  uint64_t hash, unsigned int set)
{
    unsigned int slot = hash & 255;
    cache->entries[slot].key = *key;
    cache->entries[slot].set_plus_one = set + 1;
}

static inline void
pgraph_vk_descriptor_cache_reset(PGRAPHVkDescriptorCache *cache)
{
    memset(cache, 0, sizeof(*cache));
}

#endif

/* SPDX-License-Identifier: LGPL-2.0-or-later */
#ifndef MCPX_APU_VP_VOICE_MEMORY_H
#define MCPX_APU_VP_VOICE_MEMORY_H

#include "qemu/rcu.h"
#include "system/address-spaces.h"
#include "system/memory.h"
#include "hw/xbox/mcpx/apu/apu_regs.h"

/* A call-local RAM mapping, never a copy of guest voice state. */
typedef struct MCPXAPUVoiceReadCache {
    MemoryRegionCache mapping;
    hwaddr voice;
} MCPXAPUVoiceReadCache;

static inline void mcpx_apu_voice_read_cache_clear(MCPXAPUVoiceReadCache *cache)
{
    address_space_cache_destroy(&cache->mapping);
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(MCPXAPUVoiceReadCache,
                                 mcpx_apu_voice_read_cache_clear)

static inline bool voice_mapping_contains(MCPXAPUVoiceReadCache *cache,
                                          hwaddr voice, hwaddr offset)
{
    MemoryRegionCache *mapping = &cache->mapping;

    return mapping->mrs.mr && mapping->ptr && cache->voice == voice &&
           offset <= mapping->len &&
           sizeof(uint32_t) <= mapping->len - offset &&
           mapping->fv == address_space_to_flatview(&address_space_memory);
}

static uint32_t mcpx_apu_voice_read_word_slow(MCPXAPUVoiceReadCache *cache,
                                              hwaddr voice, hwaddr offset)
{
    RCU_READ_LOCK_GUARD();

    mcpx_apu_voice_read_cache_clear(cache);
    address_space_cache_init(&cache->mapping, &address_space_memory, voice,
                             NV_PAVS_SIZE, false);
    cache->voice = voice;
    if (!voice_mapping_contains(cache, voice, offset)) {
        return ldl_le_phys(&address_space_memory, voice + offset);
    }
    return ldl_le_phys_cached(&cache->mapping, offset);
}

static inline uint32_t mcpx_apu_voice_read_word(MCPXAPUVoiceReadCache *cache,
                                                hwaddr voice, hwaddr offset)
{
    if (likely(voice_mapping_contains(cache, voice, offset))) {
        return ldl_le_phys_cached(&cache->mapping, offset);
    }
    return mcpx_apu_voice_read_word_slow(cache, voice, offset);
}

#endif

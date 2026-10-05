/* SPDX-License-Identifier: LGPL-2.0-or-later */

#ifndef MCPX_APU_VP_SAMPLE_MEMORY_H
#define MCPX_APU_VP_SAMPLE_MEMORY_H

#include "exec/target_page.h"
#include "system/address-spaces.h"
#include "sge.h"

/*
 * Own mappings for one sample callback, never guest descriptor/payload values.
 */
typedef struct MCPXAPUSampleReadCache {
    MCPXAPUSGETranslationCache sge;
    MemoryRegionCache payload;
    hwaddr payload_physical;
} MCPXAPUSampleReadCache;

static inline void mcpx_apu_sample_cache_clear(MCPXAPUSampleReadCache *cache)
{
    if (cache->payload.mrs.mr) {
        address_space_cache_destroy(&cache->payload);
    }
    mcpx_apu_sge_cache_clear(&cache->sge);
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(MCPXAPUSampleReadCache,
                                 mcpx_apu_sample_cache_clear)

static inline hwaddr mcpx_apu_sample_word_address(MCPXAPUSampleReadCache *cache,
                                                  hwaddr sge_base,
                                                  uint32_t linear)
{
    hwaddr descriptor = sge_base + (linear / TARGET_PAGE_SIZE) * 8;
    uint32_t page = mcpx_apu_sge_read_descriptor(&cache->sge, descriptor);

    /* Preserve the existing reader's 32-bit payload-address arithmetic. */
    return page + linear % TARGET_PAGE_SIZE;
}

static inline bool mcpx_apu_payload_contains(MCPXAPUSampleReadCache *cache,
                                             hwaddr physical, size_t size)
{
    MemoryRegionCache *mapping = &cache->payload;

    /* Check cheap address/size failures before consulting the current view. */
    return mapping->mrs.mr && mapping->ptr &&
           physical >= cache->payload_physical &&
           physical - cache->payload_physical <= mapping->len &&
           size <= mapping->len - (physical - cache->payload_physical) &&
           mapping->fv == address_space_to_flatview(&address_space_memory);
}

#ifndef MCPX_APU_CACHE_MIN_WORDS
/* Avoid setting up a cold mapping for one trailing word. */
#define MCPX_APU_CACHE_MIN_WORDS 2
#endif

static inline uint32_t mcpx_apu_sample_read_word(MCPXAPUSampleReadCache *cache,
                                                 hwaddr physical,
                                                 uint32_t linear,
                                                 unsigned remaining_words)
{
    size_t page_remaining = TARGET_PAGE_SIZE - linear % TARGET_PAGE_SIZE;

    if (!mcpx_apu_payload_contains(cache, physical, sizeof(uint32_t))) {
        address_space_cache_destroy(&cache->payload);
        if (remaining_words >= MCPX_APU_CACHE_MIN_WORDS &&
            page_remaining >= sizeof(uint32_t)) {
            RCU_READ_LOCK_GUARD();

            address_space_cache_init(&cache->payload, &address_space_memory,
                                     physical, page_remaining, false);
            cache->payload_physical = physical;
        }
        if (!mcpx_apu_payload_contains(cache, physical, sizeof(uint32_t))) {
            return ldl_le_phys(&address_space_memory, physical);
        }
    }
    return ldl_le_phys_cached(&cache->payload,
                              physical - cache->payload_physical);
}

static inline void mcpx_apu_read_adpcm_block(MCPXAPUSampleReadCache *cache,
                                             const uint32_t *sge_base_reg,
                                             uint32_t linear, uint32_t *words,
                                             unsigned word_count)
{
    for (unsigned i = 0; i < word_count; i++, linear += sizeof(*words)) {
        /* Keep the existing per-word observation of table base/descriptor. */
        hwaddr physical =
            mcpx_apu_sample_word_address(cache, *sge_base_reg, linear);
        words[i] =
            mcpx_apu_sample_read_word(cache, physical, linear, word_count - i);
    }
}

#endif

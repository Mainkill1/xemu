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
    int64_t start = diag_start();
    if (cache->payload.mrs.mr) {
        DIAG_COUNT(payload_destroy);
        address_space_cache_destroy(&cache->payload);
    }
    mcpx_apu_sge_cache_clear(&cache->sge);
    diag_stop(DIAG_cleanup, start);
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
           (DIAG_COUNT(payload_flatview),
            mapping->fv == address_space_to_flatview(&address_space_memory));
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
    DIAG_COUNT(payload_reads);
    int64_t start = diag_start();
    if (diag_reader_mode < 2) {
        DIAG_COUNT(payload_phys);
        uint32_t value = ldl_le_phys(&address_space_memory, physical);
        diag_stop(DIAG_payload, start);
        return value;
    }
    size_t page_remaining = TARGET_PAGE_SIZE - linear % TARGET_PAGE_SIZE;

    if (!mcpx_apu_payload_contains(cache, physical, sizeof(uint32_t))) {
        DIAG_COUNT(payload_misses);
        int64_t refill_start = diag_start();
        if (cache->payload.mrs.mr) {
            DIAG_COUNT(payload_destroy);
            if (!cache->payload.ptr) { DIAG_COUNT(payload_non_direct); }
            if (cache->payload.fv != address_space_to_flatview(&address_space_memory)) {
                DIAG_COUNT(payload_remap);
            } else { DIAG_COUNT(payload_range_miss); }
        }
        address_space_cache_destroy(&cache->payload);
        if (remaining_words >= MCPX_APU_CACHE_MIN_WORDS &&
            page_remaining >= sizeof(uint32_t)) {
            RCU_READ_LOCK_GUARD();

            DIAG_COUNT(payload_init);
            address_space_cache_init(&cache->payload, &address_space_memory,
                                     physical, page_remaining, false);
            if (cache->payload.ptr) {
                DIAG_ADD(payload_mapped_bytes, cache->payload.len);
            }
            cache->payload_physical = physical;
        }
        diag_stop(DIAG_payload_refill, refill_start);
        if (!mcpx_apu_payload_contains(cache, physical, sizeof(uint32_t))) {
            DIAG_COUNT(payload_phys);
            uint32_t value = ldl_le_phys(&address_space_memory, physical);
            diag_stop(DIAG_payload, start);
            return value;
        }
    } else { DIAG_COUNT(payload_hits); }
    uint32_t value = ldl_le_phys_cached(&cache->payload,
                              physical - cache->payload_physical);
    diag_stop(DIAG_payload, start);
    return value;
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

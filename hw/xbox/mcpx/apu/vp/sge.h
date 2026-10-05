/* SPDX-License-Identifier: LGPL-2.0-or-later */

#ifndef HW_XBOX_MCPX_APU_VP_SGE_H
#define HW_XBOX_MCPX_APU_VP_SGE_H

#include "sample-diagnostic.h"
#include "qemu/rcu.h"
#include "system/memory.h"

typedef struct MCPXAPUSGETranslationCache {
    hwaddr descriptor;
    MemoryRegionCache mapping;
} MCPXAPUSGETranslationCache;

static inline void mcpx_apu_sge_cache_clear(MCPXAPUSGETranslationCache *cache)
{
    int64_t start = diag_start(DIAG_descriptor_cleanup);
    if (cache->mapping.mrs.mr) { DIAG_COUNT(descriptor_destroy); }
    address_space_cache_destroy(&cache->mapping);
    diag_stop(DIAG_descriptor_cleanup, start);
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(MCPXAPUSGETranslationCache,
                                 mcpx_apu_sge_cache_clear)

static uint32_t
mcpx_apu_sge_read_descriptor_slow(MCPXAPUSGETranslationCache *cache,
                                  hwaddr descriptor)
{
    int64_t start = diag_start(DIAG_descriptor_refill);
    DIAG_COUNT(descriptor_init);
    RCU_READ_LOCK_GUARD();

    mcpx_apu_sge_cache_clear(cache);
    int64_t len =
        address_space_cache_init(&cache->mapping, &address_space_memory,
                                 descriptor, sizeof(uint32_t), false);
    if (len != sizeof(uint32_t) || !cache->mapping.ptr) {
        mcpx_apu_sge_cache_clear(cache);
        DIAG_COUNT(descriptor_phys);
        diag_stop(DIAG_descriptor_refill, start);
        return ldl_le_phys(&address_space_memory, descriptor);
    }
    diag_stop(DIAG_descriptor_refill, start);
    cache->descriptor = descriptor;
    return ldl_le_phys_cached(&cache->mapping, 0);
}

static inline uint32_t
mcpx_apu_sge_read_descriptor(MCPXAPUSGETranslationCache *cache,
                             hwaddr descriptor)
{
    DIAG_COUNT(descriptor_reads);
    int64_t start = diag_start(DIAG_descriptor);
    if (diag_reader_mode == 0) {
        DIAG_COUNT(descriptor_phys);
        uint32_t value = ldl_le_phys(&address_space_memory, descriptor);
        diag_stop(DIAG_descriptor, start);
        return value;
    }
    MemoryRegionCache *mapping = &cache->mapping;

    /* Retain a RAM mapping, never a guest-written descriptor value. */
    if (likely(mapping->mrs.mr && mapping->ptr &&
               cache->descriptor == descriptor &&
               (DIAG_COUNT(descriptor_flatview), mapping->fv ==
                   address_space_to_flatview(&address_space_memory)))) {
        DIAG_COUNT(descriptor_hits);
        uint32_t value = ldl_le_phys_cached(mapping, 0);
        diag_stop(DIAG_descriptor, start);
        return value;
    }

    DIAG_COUNT(descriptor_misses);
    if (mapping->mrs.mr && mapping->fv !=
        address_space_to_flatview(&address_space_memory)) {
        DIAG_COUNT(descriptor_remap);
    }
    uint32_t value = mcpx_apu_sge_read_descriptor_slow(cache, descriptor);
    diag_stop(DIAG_descriptor, start);
    return value;
}

#endif

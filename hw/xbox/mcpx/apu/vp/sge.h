/*
 * QEMU MCPX Audio Processing Unit implementation
 *
 * Copyright (c) 2026 James Rowe
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef HW_XBOX_MCPX_APU_VP_SGE_H
#define HW_XBOX_MCPX_APU_VP_SGE_H

#include "exec/hwaddr.h"
#include "exec/target_page.h"

typedef struct MCPXAPUSGETranslationCache {
    unsigned int entry;
    hwaddr page_base;
    bool valid;
} MCPXAPUSGETranslationCache;

static inline bool mcpx_apu_sge_cache_translate(
    const MCPXAPUSGETranslationCache *cache, uint32_t address,
    hwaddr *physical)
{
    unsigned int entry = address / TARGET_PAGE_SIZE;

    if (!cache->valid || cache->entry != entry) {
        return false;
    }

    *physical = cache->page_base + address % TARGET_PAGE_SIZE;
    return true;
}

static inline hwaddr mcpx_apu_sge_cache_fill(
    MCPXAPUSGETranslationCache *cache, uint32_t address, hwaddr page_base)
{
    cache->entry = address / TARGET_PAGE_SIZE;
    cache->page_base = page_base;
    cache->valid = true;

    return page_base + address % TARGET_PAGE_SIZE;
}

#endif

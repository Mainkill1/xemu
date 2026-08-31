/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef HW_XBOX_NV2A_PGRAPH_LAZY_CACHE_H
#define HW_XBOX_NV2A_PGRAPH_LAZY_CACHE_H

#include <stdbool.h>
#include <stddef.h>
#include "qemu/lru.h"

static inline size_t pgraph_lazy_cache_growth_count(
    size_t num_entries, size_t max_entries, size_t block_entries,
    size_t num_free, bool key_present)
{
    if (num_free || key_present || num_entries >= max_entries ||
        !block_entries) {
        return 0;
    }

    size_t remaining = max_entries - num_entries;
    return remaining < block_entries ? remaining : block_entries;
}

static inline size_t pgraph_lazy_cache_growth_for_lookup(
    Lru *lru, size_t num_entries, size_t max_entries, size_t block_entries,
    uint64_t hash, const void *key)
{
    bool key_present = false;
    if (!lru->num_free && num_entries < max_entries) {
        key_present = lru_contains_key(lru, hash, key);
    }
    return pgraph_lazy_cache_growth_count(
        num_entries, max_entries, block_entries, lru->num_free, key_present);
}

#endif

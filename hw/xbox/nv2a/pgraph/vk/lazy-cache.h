/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_LAZY_CACHE_H
#define HW_XBOX_NV2A_PGRAPH_VK_LAZY_CACHE_H

#include <stdbool.h>
#include <stddef.h>

static inline size_t pgraph_vk_lazy_cache_growth_count(
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

#endif

/*
 * Small, overflow-safe contracts for ordered vertex staging.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_XBOX_NV2A_PGRAPH_VK_VERTEX_STAGING_H
#define HW_XBOX_NV2A_PGRAPH_VK_VERTEX_STAGING_H

#include "qemu/osdep.h"

#define PGRAPH_VK_VERTEX_RAM_STAGING_MAX_SIZE (UINT64_C(16) * 1024 * 1024)

/* Ranges use an exclusive end, so the last valid byte is capacity - 1. */
static inline bool pgraph_vk_vertex_staging_range_valid(uint64_t offset,
                                                         uint64_t size,
                                                         uint64_t capacity)
{
    return offset <= capacity && size <= capacity - offset;
}

/* Reserve one aligned range without wrapping either the alignment or size. */
static inline bool pgraph_vk_vertex_staging_reserve(uint64_t used,
                                                     uint64_t size,
                                                     uint64_t capacity,
                                                     uint64_t alignment,
                                                     uint64_t *offset)
{
    uint64_t aligned;

    if (!offset || !alignment || (alignment & (alignment - 1)) ||
        used > capacity || used > UINT64_MAX - (alignment - 1)) {
        return false;
    }
    aligned = (used + alignment - 1) & ~(alignment - 1);
    if (!pgraph_vk_vertex_staging_range_valid(aligned, size, capacity)) {
        return false;
    }
    *offset = aligned;
    return true;
}

#endif

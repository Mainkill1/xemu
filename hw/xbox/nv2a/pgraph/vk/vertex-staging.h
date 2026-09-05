/*
 * Small, overflow-safe contracts for ordered vertex staging.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_XBOX_NV2A_PGRAPH_VK_VERTEX_STAGING_H
#define HW_XBOX_NV2A_PGRAPH_VK_VERTEX_STAGING_H

#include "qemu/osdep.h"

#define PGRAPH_VK_VERTEX_RAM_STAGING_MAX_SIZE (UINT64_C(16) * 1024 * 1024)

typedef enum PgraphVkVertexUpdatePlan {
    PGRAPH_VK_VERTEX_UPDATE_REJECT,
    PGRAPH_VK_VERTEX_UPDATE_DIRECT,
    PGRAPH_VK_VERTEX_UPDATE_STAGE,
    PGRAPH_VK_VERTEX_UPDATE_FINISH_RETRY,
    PGRAPH_VK_VERTEX_UPDATE_FINISH_DIRECT,
} PgraphVkVertexUpdatePlan;

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

/* Plan an append while accounting for alignment before every element. */
static inline bool pgraph_vk_vertex_staging_plan_append(
    uint64_t used, const uint64_t *sizes, size_t count, uint64_t capacity,
    uint64_t alignment, uint64_t *start, uint64_t *end)
{
    uint64_t cursor = used;
    bool have_element = false;

    if (!start || !end || (count && !sizes) || used > capacity ||
        !alignment || (alignment & (alignment - 1))) {
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        uint64_t item_offset;

        if (!pgraph_vk_vertex_staging_reserve(cursor, sizes[i], capacity,
                                              alignment, &item_offset)) {
            return false;
        }
        if (!have_element) {
            *start = item_offset;
            have_element = true;
        }
        cursor = item_offset;
        if (sizes[i] > capacity - cursor) {
            return false;
        }
        cursor += sizes[i];
    }
    *end = cursor;
    if (!have_element) {
        *start = cursor;
    }
    return true;
}

static inline bool pgraph_vk_vertex_staging_growth_size(
    uint64_t current, uint64_t required, uint64_t *grown)
{
    uint64_t doubled;

    if (!grown || required > PGRAPH_VK_VERTEX_RAM_STAGING_MAX_SIZE ||
        current >= PGRAPH_VK_VERTEX_RAM_STAGING_MAX_SIZE) {
        return false;
    }
    doubled = current > PGRAPH_VK_VERTEX_RAM_STAGING_MAX_SIZE / 2 ?
                  PGRAPH_VK_VERTEX_RAM_STAGING_MAX_SIZE : current * 2;
    *grown = MAX(doubled, required);
    return *grown <= PGRAPH_VK_VERTEX_RAM_STAGING_MAX_SIZE;
}

static inline PgraphVkVertexUpdatePlan pgraph_vk_vertex_update_plan(
    bool in_command_buffer, bool copy_compatible, uint64_t offset,
    uint64_t size, uint64_t vertex_capacity, uint64_t staging_used,
    uint64_t staging_capacity)
{
    if (!pgraph_vk_vertex_staging_range_valid(offset, size,
                                              vertex_capacity)) {
        return PGRAPH_VK_VERTEX_UPDATE_REJECT;
    }
    if (!in_command_buffer) {
        return PGRAPH_VK_VERTEX_UPDATE_DIRECT;
    }
    if (!copy_compatible || size > PGRAPH_VK_VERTEX_RAM_STAGING_MAX_SIZE) {
        return PGRAPH_VK_VERTEX_UPDATE_FINISH_DIRECT;
    }
    if (pgraph_vk_vertex_staging_reserve(staging_used, size,
                                         staging_capacity, 4, &offset)) {
        return PGRAPH_VK_VERTEX_UPDATE_STAGE;
    }
    return PGRAPH_VK_VERTEX_UPDATE_FINISH_RETRY;
}

#endif

/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Bounds used before copying a vertex attribute into an immutable draw slice.
 */
#ifndef HW_XBOX_NV2A_PGRAPH_VK_VERTEX_VERSION_POLICY_H
#define HW_XBOX_NV2A_PGRAPH_VK_VERTEX_VERSION_POLICY_H

#include <stdbool.h>
#include <stdint.h>

static inline bool pgraph_vk_vertex_version_span_fits(
    uint64_t vertices, uint64_t stride, uint64_t element_bytes,
    uint64_t bytes_available)
{
    if (!vertices || !element_bytes || stride < element_bytes ||
        element_bytes > bytes_available) {
        return false;
    }
    return vertices - 1 <= (bytes_available - element_bytes) / stride;
}

static inline bool pgraph_vk_vertex_version_copy_fits(
    uint64_t vertices, uint64_t element_bytes, uint64_t remaining_budget)
{
    return vertices && element_bytes &&
           vertices <= remaining_budget / element_bytes;
}

#endif

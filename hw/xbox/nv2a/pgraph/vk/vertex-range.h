/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_VERTEX_RANGE_H
#define HW_XBOX_NV2A_PGRAPH_VK_VERTEX_RANGE_H

#include <stdbool.h>
#include <stdint.h>

static inline bool pgraph_vk_vertex_base_offset(uint64_t offset,
                                                uint64_t stride,
                                                uint32_t base_vertex,
                                                uint64_t *base_offset)
{
    if (stride && base_vertex > (UINT64_MAX - offset) / stride) {
        return false;
    }

    *base_offset = offset + stride * base_vertex;
    return true;
}

static inline uint32_t pgraph_vk_indexed_base_vertex(uint32_t min_vertex)
{
    return min_vertex <= INT32_MAX ? min_vertex : 0;
}

static inline int32_t pgraph_vk_indexed_vertex_offset(uint32_t base_vertex)
{
    return -(int32_t)base_vertex;
}

#endif

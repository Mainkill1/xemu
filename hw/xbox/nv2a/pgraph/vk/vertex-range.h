/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_VERTEX_RANGE_H
#define HW_XBOX_NV2A_PGRAPH_VK_VERTEX_RANGE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct PGRAPHVkVertexPageRange {
    uint64_t start_page;
    uint64_t num_pages;
} PGRAPHVkVertexPageRange;

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

static inline bool pgraph_vk_vertex_attribute_read_span(
    uint64_t num_elements, uint64_t stride, uint64_t element_size,
    uint64_t *span)
{
    if (!span || !num_elements || !element_size) {
        return false;
    }

    uint64_t last_element_offset;
    if (stride && num_elements - 1 > UINT64_MAX / stride) {
        return false;
    }
    last_element_offset = (num_elements - 1) * stride;
    if (element_size > UINT64_MAX - last_element_offset) {
        return false;
    }

    *span = last_element_offset + element_size;
    return true;
}

static inline bool pgraph_vk_vertex_page_range_for_bytes(
    uint64_t offset, uint64_t size, uint64_t limit, uint64_t page_size,
    PGRAPHVkVertexPageRange *range)
{
    if (!range || !size || !page_size || offset >= limit ||
        size > limit - offset) {
        return false;
    }

    uint64_t end = offset + size;
    uint64_t start_page = offset / page_size;
    uint64_t end_page = ((end - 1) / page_size) + 1;

    if (end_page < start_page) {
        return false;
    }

    *range = (PGRAPHVkVertexPageRange) {
        .start_page = start_page,
        .num_pages = end_page - start_page,
    };
    return true;
}

static inline bool pgraph_vk_vertex_page_range_to_bytes(
    const PGRAPHVkVertexPageRange *range, uint64_t page_size,
    uint64_t *offset, uint64_t *size)
{
    if (!range || !page_size || range->start_page > UINT64_MAX / page_size ||
        range->num_pages > (UINT64_MAX / page_size) - range->start_page) {
        return false;
    }

    *offset = range->start_page * page_size;
    *size = range->num_pages * page_size;
    return true;
}

static inline bool pgraph_vk_vertex_page_range_to_bitmap(
    const PGRAPHVkVertexPageRange *range, uint64_t bitmap_size,
    size_t *start_bit, size_t *nbits)
{
    if (!range || !start_bit || !nbits ||
        range->start_page > SIZE_MAX ||
        range->num_pages > SIZE_MAX ||
        range->num_pages > SIZE_MAX - range->start_page ||
        range->start_page + range->num_pages > bitmap_size) {
        return false;
    }

    *start_bit = range->start_page;
    *nbits = range->num_pages;
    return true;
}

static inline bool pgraph_vk_vertex_page_ranges_overlap(
    PGRAPHVkVertexPageRange a, PGRAPHVkVertexPageRange b)
{
    uint64_t a_end = a.num_pages > UINT64_MAX - a.start_page ?
                         UINT64_MAX :
                         a.start_page + a.num_pages;
    uint64_t b_end = b.num_pages > UINT64_MAX - b.start_page ?
                         UINT64_MAX :
                         b.start_page + b.num_pages;

    return a.num_pages && b.num_pages && a.start_page < b_end &&
           b.start_page < a_end;
}

#endif

/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Checked source ranges for strided NV2A vertex attributes.
 */
#ifndef HW_XBOX_NV2A_PGRAPH_VK_VERTEX_FETCH_SPAN_H
#define HW_XBOX_NV2A_PGRAPH_VK_VERTEX_FETCH_SPAN_H

#include <stdbool.h>
#include <stdint.h>

static inline bool pgraph_vk_vertex_fetch_span(uint64_t first_vertex,
                                                uint64_t last_vertex,
                                                uint64_t stride,
                                                uint64_t element_bytes,
                                                uint64_t *relative_start,
                                                uint64_t *span_bytes)
{
    if (last_vertex < first_vertex || !element_bytes ||
        (stride && first_vertex > UINT64_MAX / stride)) {
        return false;
    }

    uint64_t vertices_after_first = last_vertex - first_vertex;
    if (stride && vertices_after_first >
                      (UINT64_MAX - element_bytes) / stride) {
        return false;
    }

    *relative_start = first_vertex * stride;
    *span_bytes = vertices_after_first * stride + element_bytes;
    return true;
}

/* DMA object limits are inclusive; VRAM sizes are exclusive. */
static inline bool pgraph_vk_vertex_span_fits_dma(uint64_t start,
                                                   uint64_t span_bytes,
                                                   uint64_t inclusive_limit)
{
    return span_bytes && start <= inclusive_limit &&
           span_bytes - 1 <= inclusive_limit - start;
}

static inline bool pgraph_vk_vertex_span_fits_vram(uint64_t start,
                                                    uint64_t span_bytes,
                                                    uint64_t vram_size)
{
    return span_bytes && start <= vram_size &&
           span_bytes <= vram_size - start;
}

#endif

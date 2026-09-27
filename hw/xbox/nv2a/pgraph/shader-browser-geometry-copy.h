/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_XBOX_NV2A_PGRAPH_SHADER_BROWSER_GEOMETRY_COPY_H
#define HW_XBOX_NV2A_PGRAPH_SHADER_BROWSER_GEOMETRY_COPY_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Copy float3/4 positions from a validated guest vertex byte span. This is
 * deliberately limited to 4096 vertices and does not interpret other NV2A
 * attribute formats as positions. */
static inline bool xemu_shader_draw_copy_float_positions(
    const uint8_t *source, size_t source_bytes, size_t first_vertex,
    size_t vertex_count, size_t stride, uint32_t components,
    float *output, size_t output_float_capacity)
{
    const size_t element_bytes = (size_t)components * sizeof(float);
    if (!source || !output || !vertex_count || vertex_count > 4096 ||
        (components != 3 && components != 4) || stride < element_bytes ||
        output_float_capacity / 4 < vertex_count ||
        first_vertex > (SIZE_MAX - element_bytes) / stride ||
        vertex_count - 1 > (SIZE_MAX - element_bytes) / stride - first_vertex) {
        return false;
    }
    const size_t last_end =
        (first_vertex + vertex_count - 1) * stride + element_bytes;
    if (last_end > source_bytes) return false;
    for (size_t i = 0; i < vertex_count; ++i) {
        memcpy(output + 4 * i, source + (first_vertex + i) * stride,
               element_bytes);
        if (components == 3) output[4 * i + 3] = 1.0f;
    }
    return true;
}

#endif

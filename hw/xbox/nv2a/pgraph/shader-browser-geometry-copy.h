/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_XBOX_NV2A_PGRAPH_SHADER_BROWSER_GEOMETRY_COPY_H
#define HW_XBOX_NV2A_PGRAPH_SHADER_BROWSER_GEOMETRY_COPY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include "ui/xui/shader-browser-draw-request.h"
#include <stdint.h>
#include <string.h>

/* Copy float3/4 positions from a resolved backend vertex byte span. This is
 * deliberately limited to 4096 vertices and does not interpret other NV2A
 * attribute formats as positions. */
static inline bool xemu_shader_draw_copy_float_positions(
    const uint8_t *source, size_t source_bytes, size_t first_vertex,
    size_t vertex_count, size_t stride, uint32_t components, float *output,
    size_t output_float_capacity)
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
    if (last_end > source_bytes)
        return false;
    for (size_t i = 0; i < vertex_count; ++i) {
        memcpy(output + 4 * i, source + (first_vertex + i) * stride,
               element_bytes);
        if (components == 3)
            output[4 * i + 3] = 1.0f;
    }
    return true;
}


/* Triangle-list subdraws never share their incomplete trailing vertices. */
typedef struct XemuShaderDrawLayout {
    uint32_t first_vertex;
    uint32_t vertex_count;
    uint32_t index_count;
    uint32_t indices[12288];
} XemuShaderDrawLayout;

static inline bool
xemu_shader_draw_layout_normalize(XemuShaderDrawLayout *layout)
{
    if (!layout->index_count)
        return false;
    uint32_t first = UINT32_MAX, last = 0;
    for (size_t i = 0; i < layout->index_count; ++i) {
        uint32_t index = layout->indices[i];
        if (index < first)
            first = index;
        if (index > last)
            last = index;
    }
    if ((uint64_t)last - first + 1 > 4096)
        return false;
    layout->first_vertex = first;
    layout->vertex_count = last - first + 1;
    for (size_t i = 0; i < layout->index_count; ++i)
        layout->indices[i] -= first;
    return true;
}

static inline bool xemu_shader_draw_layout_arrays(XemuShaderDrawLayout *layout,
                                                  const int32_t *starts,
                                                  const int32_t *counts,
                                                  size_t ranges)
{
    if (!layout || !starts || !counts || !ranges || ranges > 12288)
        return false;
    layout->index_count = 0;
    for (size_t i = 0; i < ranges; ++i) {
        if (starts[i] < 0 || counts[i] < 0 ||
            (uint64_t)starts[i] + counts[i] > UINT32_MAX)
            return false;
        const uint32_t complete = counts[i] / 3 * 3;
        if (complete > 12288 - layout->index_count)
            return false;
        for (uint32_t j = 0; j < complete; ++j)
            layout->indices[layout->index_count++] = starts[i] + j;
    }
    return xemu_shader_draw_layout_normalize(layout);
}

static inline bool
xemu_shader_draw_layout_elements(XemuShaderDrawLayout *layout,
                                 const void *indices, size_t count)
{
    if (!layout || !indices || count > 12288)
        return false;
    layout->index_count = count / 3 * 3;
    memcpy(layout->indices, indices, layout->index_count * sizeof(uint32_t));
    return xemu_shader_draw_layout_normalize(layout);
}

/* The bridge owns the snapshot before the caller emits its command. No guest
 * pointers or inline-populated flags participate in this source contract. */
static inline bool xemu_shader_draw_stage_source(
    uint64_t token, const XemuShaderDrawLayout *layout, const uint8_t *source,
    size_t source_bytes, uint32_t source_first_vertex, size_t stride,
    uint32_t components)
{
    if (!token || !layout || layout->first_vertex < source_first_vertex ||
        !layout->vertex_count || layout->vertex_count > 4096 ||
        !layout->index_count || layout->index_count > 12288)
        return false;
    float *positions =
        (float *)malloc(layout->vertex_count * 4 * sizeof(float));
    if (!positions)
        return false;
    bool result = xemu_shader_draw_copy_float_positions(
        source, source_bytes, layout->first_vertex - source_first_vertex,
        layout->vertex_count, stride, components, positions,
        layout->vertex_count * 4);
    if (result) {
        XemuShaderDrawGeometry geometry = {
            .positions = positions,
            .position_count = layout->vertex_count,
            .indices = layout->indices,
            .index_count = layout->index_count,
        };
        result = xemu_shader_draw_request_stage_geometry(token, &geometry);
    }
    free(positions);
    return result;
}

#endif

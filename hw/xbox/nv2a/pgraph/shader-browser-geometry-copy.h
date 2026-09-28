/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_XBOX_NV2A_PGRAPH_SHADER_BROWSER_GEOMETRY_COPY_H
#define HW_XBOX_NV2A_PGRAPH_SHADER_BROWSER_GEOMETRY_COPY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include "hw/xbox/nv2a/nv2a_regs.h"
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


/* Diagnostic triangles only. Original command topology and streams are retained
 * separately, including degenerate indices and incomplete subdraw tails. */
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

static inline bool xemu_shader_draw_topology_supported(uint32_t primitive)
{
    return primitive >= NV097_SET_BEGIN_END_OP_TRIANGLES &&
           primitive <= NV097_SET_BEGIN_END_OP_POLYGON;
}

static inline bool xemu_shader_draw_layout_append(XemuShaderDrawLayout *layout,
                                                  uint32_t primitive,
                                                  const void *indices,
                                                  size_t count, uint32_t first)
{
    if (!xemu_shader_draw_topology_supported(primitive) || count > 12288)
        return false;
    size_t triangles = 0;
    switch (primitive) {
    case NV097_SET_BEGIN_END_OP_TRIANGLES:
        triangles = count / 3;
        break;
    case NV097_SET_BEGIN_END_OP_TRIANGLE_STRIP:
    case NV097_SET_BEGIN_END_OP_TRIANGLE_FAN:
    case NV097_SET_BEGIN_END_OP_POLYGON:
        triangles = count >= 3 ? count - 2 : 0;
        break;
    case NV097_SET_BEGIN_END_OP_QUADS:
        triangles = count / 4 * 2;
        break;
    case NV097_SET_BEGIN_END_OP_QUAD_STRIP:
        triangles = count >= 4 ? (count / 2 - 1) * 2 : 0;
        break;
    }
    if (triangles > (12288 - layout->index_count) / 3)
        return false;
    for (size_t triangle = 0; triangle < triangles; ++triangle) {
        size_t vertex[3];
        switch (primitive) {
        case NV097_SET_BEGIN_END_OP_TRIANGLES:
            vertex[0] = triangle * 3;
            vertex[1] = vertex[0] + 1;
            vertex[2] = vertex[0] + 2;
            break;
        case NV097_SET_BEGIN_END_OP_TRIANGLE_STRIP:
            vertex[0] = triangle + (triangle & 1);
            vertex[1] = triangle + 1 - (triangle & 1);
            vertex[2] = triangle + 2;
            break;
        case NV097_SET_BEGIN_END_OP_TRIANGLE_FAN:
        case NV097_SET_BEGIN_END_OP_POLYGON:
            vertex[0] = 0;
            vertex[1] = triangle + 1;
            vertex[2] = triangle + 2;
            break;
        case NV097_SET_BEGIN_END_OP_QUADS: {
            // Matches filled quad emission in glsl/geom.c.
            const size_t base = triangle / 2 * 4;
            vertex[0] = base + (triangle & 1 ? 2 : 1);
            vertex[1] = base + (triangle & 1 ? 3 : 2);
            vertex[2] = base;
            break;
        }
        case NV097_SET_BEGIN_END_OP_QUAD_STRIP: {
            const size_t base = triangle / 2 * 2;
            vertex[0] = base + (triangle & 1 ? 2 : 0);
            vertex[1] = base + 1;
            vertex[2] = base + (triangle & 1 ? 3 : 2);
            break;
        }
        default:
            return false;
        }
        for (size_t i = 0; i < 3; ++i) {
            uint32_t index = first + vertex[i];
            if (indices)
                memcpy(&index, (const uint8_t *)indices + vertex[i] * 4, 4);
            layout->indices[layout->index_count++] = index;
        }
    }
    return true;
}

static inline bool xemu_shader_draw_layout_arrays_topology(
    XemuShaderDrawLayout *layout, uint32_t primitive, const int32_t *starts,
    const int32_t *counts, size_t ranges)
{
    if (!layout || !starts || !counts || !ranges || ranges > 12288)
        return false;
    layout->first_vertex = layout->vertex_count = layout->index_count = 0;
    for (size_t i = 0; i < ranges; ++i) {
        if (starts[i] < 0 || counts[i] < 0 ||
            (uint64_t)starts[i] + counts[i] > UINT32_MAX ||
            !xemu_shader_draw_layout_append(layout, primitive, NULL, counts[i],
                                            starts[i])) {
            layout->index_count = 0;
            return false;
        }
    }
    if (!xemu_shader_draw_layout_normalize(layout)) {
        layout->index_count = 0;
        return false;
    }
    return true;
}

static inline bool
xemu_shader_draw_layout_elements_topology(XemuShaderDrawLayout *layout,
                                          uint32_t primitive,
                                          const void *indices, size_t count)
{
    if (!layout || !indices)
        return false;
    layout->first_vertex = layout->vertex_count = layout->index_count = 0;
    if (!xemu_shader_draw_layout_append(layout, primitive, indices, count, 0) ||
        !xemu_shader_draw_layout_normalize(layout)) {
        layout->index_count = 0;
        return false;
    }
    return true;
}

static inline bool xemu_shader_draw_layout_arrays(XemuShaderDrawLayout *layout,
                                                  const int32_t *starts,
                                                  const int32_t *counts,
                                                  size_t ranges)
{
    return xemu_shader_draw_layout_arrays_topology(
        layout, NV097_SET_BEGIN_END_OP_TRIANGLES, starts, counts, ranges);
}

static inline bool
xemu_shader_draw_layout_elements(XemuShaderDrawLayout *layout,
                                 const void *indices, size_t count)
{
    return xemu_shader_draw_layout_elements_topology(
        layout, NV097_SET_BEGIN_END_OP_TRIANGLES, indices, count);
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
        !layout->index_count || layout->index_count > 12288) {
        xemu_shader_draw_request_note_rejection(
            token, XEMU_SHADER_CAPTURE_REJECT_POSITION_UNAVAILABLE);
        return false;
    }
    float *positions =
        (float *)malloc(layout->vertex_count * 4 * sizeof(float));
    if (!positions) {
        xemu_shader_draw_request_note_rejection(
            token, XEMU_SHADER_CAPTURE_REJECT_BUDGET);
        return false;
    }
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
    } else {
        xemu_shader_draw_request_note_rejection(
            token, XEMU_SHADER_CAPTURE_REJECT_POSITION_UNAVAILABLE);
    }
    free(positions);
    return result;
}

#endif

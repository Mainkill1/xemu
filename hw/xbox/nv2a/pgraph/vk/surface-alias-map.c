/*
 * Vulkan depth surface alias layout
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/xbox/nv2a/pgraph/vk/surface-alias-map.h"

static bool is_power_of_two(uint32_t value)
{
    return value && !(value & (value - 1));
}

bool pgraph_vk_alias_morton_index(uint32_t x, uint32_t y, uint32_t width,
                                 uint32_t height, uint32_t *index)
{
    if (!index || !is_power_of_two(width) || !is_power_of_two(height) ||
        x >= width || y >= height ||
        (uint64_t)width * height > UINT32_MAX) {
        return false;
    }

    uint32_t offset = 0;
    uint32_t mask = 1;

    for (uint32_t bit = 1; bit < width || bit < height; bit <<= 1) {
        if (bit < width) {
            if (x & bit) {
                offset |= mask;
            }
            mask <<= 1;
        }
        if (bit < height) {
            if (y & bit) {
                offset |= mask;
            }
            mask <<= 1;
        }
    }

    *index = offset;
    return true;
}

bool pgraph_vk_depth_alias_read_only_eligible(
    const PGRAPHVkDepthAliasView *producer,
    const PGRAPHVkDepthAliasView *view,
    bool depth_or_stencil_writes, bool clearing, uint32_t scale,
    bool antialiasing)
{
    uint32_t unused_index;

    if (!producer || !view || depth_or_stencil_writes || clearing ||
        scale != 1 || antialiasing || producer->color || view->color ||
        producer->swizzled || !view->swizzled || !producer->initialized ||
        producer->upload_pending || producer->download_pending ||
        producer->superseded_by_guest || !producer->guest_z24s8 ||
        !view->guest_z24s8 || !producer->host_supported ||
        !view->host_supported || !producer->host_format ||
        producer->host_format != view->host_format ||
        producer->address != view->address ||
        producer->dma_address != view->dma_address ||
        producer->dma_length != view->dma_length ||
        producer->pitch != view->pitch ||
        (uint64_t)producer->width * sizeof(uint32_t) != producer->pitch ||
        producer->extent < view->extent ||
        producer->width < view->width ||
        producer->height < view->height ||
        ((uint64_t)producer->width * producer->height <
         (uint64_t)view->width * view->height) ||
        !pgraph_vk_alias_morton_index(0, 0, view->width, view->height,
                                      &unused_index)) {
        return false;
    }

    uint64_t producer_bytes =
        (uint64_t)producer->width * producer->height * sizeof(uint32_t);
    uint64_t view_bytes =
        (uint64_t)view->width * view->height * sizeof(uint32_t);

    return producer_bytes <= 2 * 1024 * 1024 &&
           producer->extent >= producer_bytes &&
           view->extent >= view_bytes &&
           (producer->width != view->width ||
            producer->height != view->height);
}

bool pgraph_vk_depth_alias_plan(uint32_t producer_width,
                               uint32_t producer_height, uint32_t view_width,
                               uint32_t view_height, uint64_t alignment,
                               uint64_t max_storage_range,
                               PGRAPHVkDepthAliasPlan *plan)
{
    uint32_t unused_index;
    uint64_t producer_pixels = (uint64_t)producer_width * producer_height;
    uint64_t view_pixels = (uint64_t)view_width * view_height;

    if (!plan || !producer_pixels || producer_pixels > INT_MAX ||
        !pgraph_vk_alias_morton_index(0, 0, view_width, view_height,
                                      &unused_index) ||
        view_pixels > producer_pixels ||
        alignment > UINT32_MAX || !is_power_of_two(alignment)) {
        return false;
    }

    /*
     * Morton addresses fill [0, view_pixels). Copy only the complete
     * linear source rows covering that prefix, including its partial tail.
     * Stencil is read as uint words, so retain enough complete rows to
     * keep the stencil descriptor range a multiple of four bytes.
     */
    uint32_t row_alignment = (producer_width & 1) ? 4 :
                             (producer_width & 2) ? 2 : 1;
    uint64_t rows = ROUND_UP(DIV_ROUND_UP(view_pixels, producer_width),
                             row_alignment);
    if (rows > producer_height) {
        return false;
    }
    producer_pixels = rows * producer_width;

    uint64_t producer_depth = producer_pixels * sizeof(uint32_t);
    uint64_t view_depth = view_pixels * sizeof(uint32_t);
    uint64_t producer_stencil_offset =
        (producer_depth + alignment - 1) & ~(alignment - 1);
    uint64_t view_stencil_offset =
        (view_depth + alignment - 1) & ~(alignment - 1);
    uint64_t compute_dst = MAX(producer_stencil_offset + producer_pixels,
                               view_depth);
    uint64_t compute_src = MAX(producer_depth,
                               view_stencil_offset + view_pixels);

    if (compute_dst > max_storage_range ||
        compute_src > max_storage_range) {
        return false;
    }

    *plan = (PGRAPHVkDepthAliasPlan){
        .producer_pixels = producer_pixels,
        .view_pixels = view_pixels,
        .producer_stencil_offset = producer_stencil_offset,
        .view_stencil_offset = view_stencil_offset,
        .compute_dst_bytes = compute_dst,
        .compute_src_bytes = compute_src,
    };
    return true;
}

char *pgraph_vk_alias_unswizzle_glsl(unsigned int workgroup_size)
{
    g_return_val_if_fail(workgroup_size > 0, NULL);

    return g_strdup_printf(
        "#version 450\n"
        "layout(local_size_x = %u, local_size_y = 1, local_size_z = 1) in;\n"
        "layout(push_constant) uniform PushConstants { uint width, height; };\n"
        "layout(set = 0, binding = 0) readonly buffer Source { uint src[]; };\n"
        "layout(set = 0, binding = 2) writeonly buffer Destination { uint dst[]; };\n"
        "uint guest_index(uint x, uint y) {\n"
        "    uint offset = 0u;\n"
        "    uint mask = 1u;\n"
        "    for (uint bit = 1u; bit < width || bit < height; bit <<= 1u) {\n"
        "        if (bit < width) {\n"
        "            if ((x & bit) != 0u) offset |= mask;\n"
        "            mask <<= 1u;\n"
        "        }\n"
        "        if (bit < height) {\n"
        "            if ((y & bit) != 0u) offset |= mask;\n"
        "            mask <<= 1u;\n"
        "        }\n"
        "    }\n"
        "    return offset;\n"
        "}\n"
        "void main() {\n"
        "    uint index = gl_GlobalInvocationID.x;\n"
        "    if (index >= width * height) return;\n"
        "    dst[index] = src[guest_index(index %% width, index / width)];\n"
        "}\n", workgroup_size);
}

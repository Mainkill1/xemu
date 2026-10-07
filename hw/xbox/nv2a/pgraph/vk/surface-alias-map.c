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

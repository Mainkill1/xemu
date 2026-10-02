/*
 * Vulkan depth surface alias layout
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_SURFACE_ALIAS_MAP_H
#define HW_XBOX_NV2A_PGRAPH_VK_SURFACE_ALIAS_MAP_H

#include <stdbool.h>
#include <stdint.h>

bool pgraph_vk_alias_morton_index(uint32_t x, uint32_t y, uint32_t width,
                                 uint32_t height, uint32_t *index);
/*
 * The caller must validate nonzero power-of-two dimensions, width * height
 * within UINT32_MAX, source/destination byte extents and descriptor ranges
 * before dispatch. Workgroup and dispatch sizes must obey the selected device
 * limits. Shader generation does not validate the later push constants.
 */
char *pgraph_vk_alias_unswizzle_glsl(unsigned int workgroup_size);

typedef struct PGRAPHVkDepthAliasView {
    uint64_t address;
    uint64_t dma_address;
    uint64_t dma_length;
    uint64_t extent;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t host_format;
    bool guest_z24s8;
    bool host_supported;
    bool color;
    bool swizzled;
    bool initialized;
    bool upload_pending;
    bool download_pending;
    bool superseded_by_guest;
} PGRAPHVkDepthAliasView;

bool pgraph_vk_depth_alias_read_only_eligible(
    const PGRAPHVkDepthAliasView *producer,
    const PGRAPHVkDepthAliasView *view,
    bool depth_or_stencil_writes, bool clearing, uint32_t scale,
    bool antialiasing);

typedef struct PGRAPHVkDepthAliasPlan {
    uint64_t producer_pixels;
    uint64_t view_pixels;
    uint64_t producer_stencil_offset;
    uint64_t view_stencil_offset;
    uint64_t compute_dst_bytes;
    uint64_t compute_src_bytes;
} PGRAPHVkDepthAliasPlan;

bool pgraph_vk_depth_alias_plan(uint32_t producer_width,
                               uint32_t producer_height, uint32_t view_width,
                               uint32_t view_height, uint64_t alignment,
                               uint64_t max_storage_range,
                               PGRAPHVkDepthAliasPlan *plan);

#endif

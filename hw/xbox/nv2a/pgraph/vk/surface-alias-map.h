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

#endif

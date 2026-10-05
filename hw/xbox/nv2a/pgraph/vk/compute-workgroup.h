/*
 * Vulkan compute workgroup selection
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#ifndef HW_XBOX_NV2A_PGRAPH_VK_COMPUTE_WORKGROUP_H
#define HW_XBOX_NV2A_PGRAPH_VK_COMPUTE_WORKGROUP_H

#include <stdint.h>

/*
 * Return the largest power-of-two workgroup that is legal for the device and
 * evenly divides the requested output. Vulkan requires both the X-dimension
 * limit and the total invocation limit to be respected.
 */
static inline uint32_t pgraph_vk_compute_workgroup_size(
    uint64_t output_units, uint32_t max_size_x, uint32_t max_invocations)
{
    uint32_t limit = max_size_x < max_invocations ?
                         max_size_x : max_invocations;

    if (limit > 1024u) {
        limit = 1024u;
    }
    if (!output_units || !limit) {
        return 0;
    }

    uint32_t group_size = 1;
    while (group_size <= limit / 2) {
        group_size *= 2;
    }
    while (group_size > 1 && output_units % group_size != 0) {
        group_size /= 2;
    }
    return group_size;
}

#endif

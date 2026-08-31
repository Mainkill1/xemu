/* SPDX-License-Identifier: LGPL-2.0-or-later */
#ifndef HW_XBOX_NV2A_PGRAPH_VK_SURFACE_COMPUTE_H
#define HW_XBOX_NV2A_PGRAPH_VK_SURFACE_COMPUTE_H

#include <stdint.h>

static inline uint32_t pgraph_vk_compute_workgroup_size(
    uint32_t output_units, uint32_t max_workgroup_size,
    uint32_t max_workgroup_invocations)
{
    uint32_t limit = max_workgroup_size < max_workgroup_invocations ?
                         max_workgroup_size : max_workgroup_invocations;
    uint32_t group_size = 1024;

    while (group_size > 1 &&
           (group_size > limit || output_units % group_size != 0)) {
        group_size /= 2;
    }
    return group_size;
}

#endif

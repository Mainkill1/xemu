/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_DESCRIPTOR_UPDATE_H
#define HW_XBOX_NV2A_PGRAPH_VK_DESCRIPTOR_UPDATE_H

#include <stdbool.h>
#include <stdint.h>

static inline bool pgraph_vk_descriptor_set_needs_write(
    uint32_t descriptor_set_count, bool shader_binding_changed,
    bool texture_bindings_changed)
{
    return descriptor_set_count == 0 || shader_binding_changed ||
           texture_bindings_changed;
}

static inline bool pgraph_vk_get_dynamic_uniform_offset(
    uint64_t offset, uint64_t required_alignment, uint32_t *dynamic_offset)
{
    if (!required_alignment || offset % required_alignment ||
        offset > UINT32_MAX) {
        return false;
    }
    *dynamic_offset = offset;
    return true;
}

#endif

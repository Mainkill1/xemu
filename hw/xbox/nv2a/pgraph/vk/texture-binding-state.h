/*
 * NV2A Vulkan texture-binding state helpers
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_TEXTURE_BINDING_STATE_H
#define HW_XBOX_NV2A_PGRAPH_VK_TEXTURE_BINDING_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A disabled stage retains its guest dirtiness until it is re-enabled. */
static inline bool pgraph_vk_texture_stage_needs_rebind(bool enabled,
                                                       bool dirty, bool bound,
                                                       bool bound_dummy)
{
    if (!enabled) {
        return !bound_dummy || dirty;
    }

    return !bound || bound_dummy || dirty;
}

static inline bool pgraph_vk_texture_source_identity_matches(
    bool stage_enabled, bool binding_valid, bool binding_is_dummy,
    bool source_is_surface, uint64_t current_texture, uint64_t bound_texture,
    size_t palette_length, uint64_t current_palette, uint64_t bound_palette)
{
    if (!stage_enabled || !binding_valid || binding_is_dummy ||
        source_is_surface) {
        return true;
    }

    return current_texture == bound_texture &&
           (!palette_length || current_palette == bound_palette);
}

#endif

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

typedef struct PGRAPHVkTextureDescriptorIdentity {
    /* Process-local Vulkan handles only. The renderer requires a host pointer
     * width that can retain every non-dispatchable handle without loss. */
    uintptr_t image_view;
    uintptr_t sampler;
} PGRAPHVkTextureDescriptorIdentity;

static inline bool pgraph_vk_texture_descriptor_identity_changed(
    PGRAPHVkTextureDescriptorIdentity before,
    PGRAPHVkTextureDescriptorIdentity after)
{
    return before.image_view != after.image_view ||
           before.sampler != after.sampler;
}

/* Descriptor-visible changes remain pending across abandoned draws. Only the
 * descriptor writer may retire them after publishing the current bindings. */
static inline void pgraph_vk_texture_descriptor_publication_observe(
    bool *pending, PGRAPHVkTextureDescriptorIdentity before,
    PGRAPHVkTextureDescriptorIdentity after)
{
    *pending |= pgraph_vk_texture_descriptor_identity_changed(before, after);
}

static inline void pgraph_vk_texture_descriptor_publication_complete(
    bool *pending)
{
    *pending = false;
}

/* A disabled stage retains its guest dirtiness until it is re-enabled. */
static inline bool pgraph_vk_texture_stage_needs_rebind(bool enabled,
                                                       bool dirty, bool bound,
                                                       bool bound_dummy)
{
    if (!enabled) {
        return !bound_dummy;
    }

    return !bound || bound_dummy || dirty;
}

/* The policy changes only the final skip after all reuse guards have run. */
static inline bool pgraph_vk_should_skip_clean_texture_stage(
    bool shortcut_enabled, bool stage_is_clean_and_reusable)
{
    return shortcut_enabled && stage_is_clean_and_reusable;
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

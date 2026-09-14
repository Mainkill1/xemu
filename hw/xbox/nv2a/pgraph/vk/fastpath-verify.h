/*
 * Bounded diagnostic verification of the Vulkan shader shortcut.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_XBOX_NV2A_PGRAPH_VK_FASTPATH_VERIFY_H
#define HW_XBOX_NV2A_PGRAPH_VK_FASTPATH_VERIFY_H

#include "hw/xbox/nv2a/pgraph/vk/renderer.h"

typedef enum PGRAPHVkFastpathMismatchKind {
    PGRAPH_VK_FASTPATH_MATCH,
    PGRAPH_VK_FASTPATH_MISMATCH_SHADER_KEY,
    PGRAPH_VK_FASTPATH_MISMATCH_PIPELINE_KEY,
    PGRAPH_VK_FASTPATH_MISMATCH_UBER_CONTROLS,
} PGRAPHVkFastpathMismatchKind;

typedef struct PGRAPHVkFastpathSnapshot {
    uint64_t shader_key_hash;
    uint64_t pipeline_key_hash;
    uint64_t control_packet_hash;
    PGRAPHVkFragmentRoute route;
    uint64_t selection_epoch;
    uint32_t descriptor_set_index;
    VkDeviceSize uber_control_offset;
    unsigned int command_buffer_start_time;
} PGRAPHVkFastpathSnapshot;

typedef struct PGRAPHVkFastpathMismatch {
    PGRAPHVkFastpathMismatchKind kind;
    PGRAPHVkFastpathSnapshot expected;
    PGRAPHVkFastpathSnapshot bound;
} PGRAPHVkFastpathMismatch;

/* These values are read by the vertex uniform setter, not by the generator.
 * Do not erase point_size: the programmable generator embeds it in GLSL. */
static inline void pgraph_vk_fastpath_normalize_shader_state(
    ShaderState *state)
{
    state->vsh.specular_power = 0;
    state->vsh.specular_power_back = 0;
    memset(state->vsh.point_params, 0, sizeof(state->vsh.point_params));
}

static inline PGRAPHVkFastpathMismatchKind
pgraph_vk_fastpath_compare_identity(
    const ShaderState *expected_state, const ShaderState *bound_state,
    const PipelineKey *expected_key, const PipelineKey *bound_key,
    const PGRAPHUberControls *expected_controls,
    const PGRAPHUberControls *bound_controls,
    PGRAPHVkFragmentRoute route)
{
    ShaderState expected_shader = *expected_state;
    ShaderState bound_shader = *bound_state;
    PipelineKey expected_pipeline = *expected_key;
    PipelineKey bound_pipeline = *bound_key;

    pgraph_vk_fastpath_normalize_shader_state(&expected_shader);
    pgraph_vk_fastpath_normalize_shader_state(&bound_shader);
    if (memcmp(&expected_shader, &bound_shader,
               sizeof(expected_shader)) != 0) {
        return PGRAPH_VK_FASTPATH_MISMATCH_SHADER_KEY;
    }

    pgraph_vk_fastpath_normalize_shader_state(
        &expected_pipeline.shader_state);
    pgraph_vk_fastpath_normalize_shader_state(
        &bound_pipeline.shader_state);
    if (expected_pipeline.fragment_route != route ||
        bound_pipeline.fragment_route != route ||
        memcmp(&expected_pipeline, &bound_pipeline,
               sizeof(expected_pipeline)) != 0) {
        return PGRAPH_VK_FASTPATH_MISMATCH_PIPELINE_KEY;
    }

    if (route == PGRAPH_VK_FRAGMENT_UBERSHADER &&
        (!expected_controls || !bound_controls ||
         memcmp(expected_controls, bound_controls,
                sizeof(*expected_controls)) != 0)) {
        return PGRAPH_VK_FASTPATH_MISMATCH_UBER_CONTROLS;
    }
    return PGRAPH_VK_FASTPATH_MATCH;
}

#endif

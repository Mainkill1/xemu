/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Graphics commands persist across render passes within a command buffer.
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_DRAW_COMMAND_STATE_H
#define HW_XBOX_NV2A_PGRAPH_VK_DRAW_COMMAND_STATE_H

#include <stdbool.h>
#include <vulkan/vulkan.h>

typedef struct PGRAPHVkDrawCommandState {
    VkPipeline pipeline;
    VkViewport viewport;
    VkRect2D scissor;
    float line_width;
    bool pipeline_valid;
    bool viewport_valid;
    bool scissor_valid;
    bool line_width_valid;
} PGRAPHVkDrawCommandState;

static inline void pgraph_vk_draw_state_reset(PGRAPHVkDrawCommandState *state)
{
    state->pipeline_valid = false;
    state->viewport_valid = false;
    state->scissor_valid = false;
    state->line_width_valid = false;
}

static inline bool pgraph_vk_draw_state_bind_pipeline(
    PGRAPHVkDrawCommandState *state, VkPipeline pipeline)
{
    if (state->pipeline_valid && state->pipeline == pipeline) {
        return false;
    }
    state->pipeline = pipeline;
    state->pipeline_valid = true;
    /* A different pipeline may make previously set dynamic state invalid. */
    state->viewport_valid = false;
    state->scissor_valid = false;
    state->line_width_valid = false;
    return true;
}

static inline bool pgraph_vk_draw_state_set_viewport(
    PGRAPHVkDrawCommandState *state, const VkViewport *viewport)
{
    if (state->viewport_valid &&
        state->viewport.x == viewport->x &&
        state->viewport.y == viewport->y &&
        state->viewport.width == viewport->width &&
        state->viewport.height == viewport->height &&
        state->viewport.minDepth == viewport->minDepth &&
        state->viewport.maxDepth == viewport->maxDepth) {
        return false;
    }
    state->viewport = *viewport;
    state->viewport_valid = true;
    return true;
}

static inline bool pgraph_vk_draw_state_set_scissor(
    PGRAPHVkDrawCommandState *state, const VkRect2D *scissor)
{
    if (state->scissor_valid &&
        state->scissor.offset.x == scissor->offset.x &&
        state->scissor.offset.y == scissor->offset.y &&
        state->scissor.extent.width == scissor->extent.width &&
        state->scissor.extent.height == scissor->extent.height) {
        return false;
    }
    state->scissor = *scissor;
    state->scissor_valid = true;
    return true;
}

static inline void pgraph_vk_draw_state_scissor_overridden(
    PGRAPHVkDrawCommandState *state)
{
    state->scissor_valid = false;
}

static inline bool pgraph_vk_draw_state_set_line_width(
    PGRAPHVkDrawCommandState *state, float line_width)
{
    if (state->line_width_valid && state->line_width == line_width) {
        return false;
    }
    state->line_width = line_width;
    state->line_width_valid = true;
    return true;
}

#endif

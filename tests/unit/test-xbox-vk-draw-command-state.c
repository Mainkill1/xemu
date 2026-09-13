/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <assert.h>
#include <stdint.h>
#include "hw/xbox/nv2a/pgraph/vk/draw-command-state.h"

static void test_pass_restart_reuses_commands(void)
{
    PGRAPHVkDrawCommandState state = {0};
    VkPipeline pipeline = (VkPipeline)(uintptr_t)1;
    VkViewport viewport = { .width = 640, .height = 480, .maxDepth = 1 };
    VkRect2D scissor = { .extent = { 640, 480 } };

    assert(pgraph_vk_draw_state_bind_pipeline(&state, pipeline));
    assert(pgraph_vk_draw_state_set_viewport(&state, &viewport));
    assert(pgraph_vk_draw_state_set_scissor(&state, &scissor));
    assert(pgraph_vk_draw_state_set_line_width(&state, 1.0f));

    /* A render-pass boundary does not start a new command buffer. */
    assert(!pgraph_vk_draw_state_bind_pipeline(&state, pipeline));
    assert(!pgraph_vk_draw_state_set_viewport(&state, &viewport));
    assert(!pgraph_vk_draw_state_set_scissor(&state, &scissor));
    assert(!pgraph_vk_draw_state_set_line_width(&state, 1.0f));

    pgraph_vk_draw_state_reset(&state);
    assert(pgraph_vk_draw_state_bind_pipeline(&state, pipeline));
    assert(pgraph_vk_draw_state_set_viewport(&state, &viewport));
    assert(pgraph_vk_draw_state_set_scissor(&state, &scissor));
    assert(pgraph_vk_draw_state_set_line_width(&state, 1.0f));
}

static void test_pipeline_and_dynamic_changes(void)
{
    PGRAPHVkDrawCommandState state = {0};
    VkPipeline first = (VkPipeline)(uintptr_t)1;
    VkPipeline second = (VkPipeline)(uintptr_t)2;
    VkViewport viewport = { .width = 640, .height = 480, .maxDepth = 1 };
    VkRect2D scissor = { .extent = { 640, 480 } };

    assert(pgraph_vk_draw_state_bind_pipeline(&state, first));
    assert(pgraph_vk_draw_state_set_viewport(&state, &viewport));
    assert(pgraph_vk_draw_state_set_scissor(&state, &scissor));
    assert(pgraph_vk_draw_state_set_line_width(&state, 1.0f));

    viewport.width = 800;
    assert(pgraph_vk_draw_state_set_viewport(&state, &viewport));
    assert(!pgraph_vk_draw_state_set_scissor(&state, &scissor));
    assert(!pgraph_vk_draw_state_set_line_width(&state, 1.0f));

    assert(pgraph_vk_draw_state_bind_pipeline(&state, second));
    assert(pgraph_vk_draw_state_set_viewport(&state, &viewport));
    assert(pgraph_vk_draw_state_set_scissor(&state, &scissor));
    assert(pgraph_vk_draw_state_set_line_width(&state, 1.0f));
}

static void test_partial_clear_restores_scissor(void)
{
    PGRAPHVkDrawCommandState state = {0};
    VkPipeline pipeline = (VkPipeline)(uintptr_t)1;
    VkRect2D scissor = { .extent = { 640, 480 } };

    assert(pgraph_vk_draw_state_bind_pipeline(&state, pipeline));
    assert(pgraph_vk_draw_state_set_scissor(&state, &scissor));
    pgraph_vk_draw_state_scissor_overridden(&state);
    assert(!pgraph_vk_draw_state_bind_pipeline(&state, pipeline));
    assert(pgraph_vk_draw_state_set_scissor(&state, &scissor));
    assert(!pgraph_vk_draw_state_set_scissor(&state, &scissor));
}

int main(void)
{
    test_pass_restart_reuses_commands();
    test_pipeline_and_dynamic_changes();
    test_partial_clear_restores_scissor();
    return 0;
}

/*
 * NV2A Vulkan draw lifecycle tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/draw-lifecycle.h"
#include "hw/xbox/nv2a/pgraph/vk/renderer.h"

static unsigned int dirty_calls;
static bool last_color_dirty;
static bool last_zeta_dirty;

static void test_prepare_result_mapping(void)
{
    g_assert_cmpint(pgraph_vk_draw_result_from_prepare(
                        PGRAPH_VK_DRAW_PREPARE_READY),
                    ==, PGRAPH_VK_DRAW_SUBMITTED);
    g_assert_cmpint(pgraph_vk_draw_result_from_prepare(
                        PGRAPH_VK_DRAW_PREPARE_OMITTED_SHADER_MISS),
                    ==, PGRAPH_VK_DRAW_OMITTED_SHADER_MISS);
    g_assert_cmpint(pgraph_vk_draw_result_from_prepare(
                        PGRAPH_VK_DRAW_PREPARE_FAILED),
                    ==, PGRAPH_VK_DRAW_FAILED);
}

void pgraph_vk_set_surface_dirty(PGRAPHState *pg, bool color, bool zeta)
{
    dirty_calls++;
    last_color_dirty = color;
    last_zeta_dirty = zeta;
}

static void test_omitted_draw_does_not_publish_surface_generation(void)
{
    PGRAPHState *pg = g_new0(PGRAPHState, 1);
    PGRAPHVkState *renderer = g_new0(PGRAPHVkState, 1);
    SurfaceBinding color = { .draw_time = 7 };
    SurfaceBinding zeta = { .draw_time = 8 };

    pg->draw_time = 10;
    pg->vk_renderer_state = renderer;
    renderer->color_binding = &color;
    renderer->zeta_binding = &zeta;
    dirty_calls = 0;

    pgraph_vk_complete_draw_lifecycle(
        pg, renderer, PGRAPH_VK_DRAW_OMITTED_SHADER_MISS,
        true, true, true, true);

    g_assert_cmpuint(pg->draw_time, ==, 10);
    g_assert_cmpuint(color.draw_time, ==, 7);
    g_assert_cmpuint(zeta.draw_time, ==, 8);
    g_assert_cmpuint(dirty_calls, ==, 0);
    g_free(renderer);
    g_free(pg);
}

static void test_failed_draw_does_not_publish_surface_generation(void)
{
    PGRAPHState *pg = g_new0(PGRAPHState, 1);
    PGRAPHVkState *renderer = g_new0(PGRAPHVkState, 1);
    SurfaceBinding color = { .draw_time = 17 };
    SurfaceBinding zeta = { .draw_time = 18 };

    pg->draw_time = 20;
    pg->vk_renderer_state = renderer;
    renderer->color_binding = &color;
    renderer->zeta_binding = &zeta;
    dirty_calls = 0;

    pgraph_vk_complete_draw_lifecycle(
        pg, renderer, PGRAPH_VK_DRAW_FAILED,
        true, true, true, true);

    g_assert_cmpuint(pg->draw_time, ==, 20);
    g_assert_cmpuint(color.draw_time, ==, 17);
    g_assert_cmpuint(zeta.draw_time, ==, 18);
    g_assert_cmpuint(dirty_calls, ==, 0);
    g_free(renderer);
    g_free(pg);
}

static void test_submitted_draw_publishes_surface_generation(void)
{
    PGRAPHState *pg = g_new0(PGRAPHState, 1);
    PGRAPHVkState *renderer = g_new0(PGRAPHVkState, 1);
    SurfaceBinding color = { .draw_time = 27 };
    SurfaceBinding zeta = { .draw_time = 28 };

    pg->draw_time = 30;
    pg->vk_renderer_state = renderer;
    renderer->color_binding = &color;
    renderer->zeta_binding = &zeta;
    dirty_calls = 0;

    pgraph_vk_complete_draw_lifecycle(
        pg, renderer, PGRAPH_VK_DRAW_SUBMITTED,
        true, true, true, true);

    g_assert_cmpuint(pg->draw_time, ==, 31);
    g_assert_cmpuint(color.draw_time, ==, 31);
    g_assert_cmpuint(zeta.draw_time, ==, 31);
    g_assert_cmpuint(dirty_calls, ==, 1);
    g_assert_true(last_color_dirty);
    g_assert_true(last_zeta_dirty);
    g_free(renderer);
    g_free(pg);
}

static void test_shader_miss_action_preserves_wait(void)
{
    g_assert_cmpint(
        pgraph_vk_draw_shader_miss_action(false, true, true, false), ==,
        PGRAPH_VK_DRAW_MISS_WAIT);
    g_assert_cmpint(
        pgraph_vk_draw_shader_miss_action(true, false, true, false), ==,
        PGRAPH_VK_DRAW_MISS_WAIT);
    g_assert_cmpint(
        pgraph_vk_draw_shader_miss_action(true, true, false, false), ==,
        PGRAPH_VK_DRAW_MISS_WAIT);
}

static void test_shader_miss_action_uses_ready_executable(void)
{
    g_assert_cmpint(
        pgraph_vk_draw_shader_miss_action(true, true, true, true), ==,
        PGRAPH_VK_DRAW_MISS_READY);
}

static void test_shader_miss_action_omits_supported_continue_draw(void)
{
    g_assert_cmpint(
        pgraph_vk_draw_shader_miss_action(true, true, true, false), ==,
        PGRAPH_VK_DRAW_MISS_OMIT);
}

static void test_omission_classifier_defaults_unknown_to_wait(void)
{
    PGRAPHState *pg = g_new0(PGRAPHState, 1);
    PGRAPHVkState *renderer = g_new0(PGRAPHVkState, 1);

    PGRAPHVkOmissionDecision decision =
        pgraph_vk_classify_draw_omission(pg, renderer);
    g_assert_false(decision.safe);
    g_assert_cmphex(decision.blockers & PGRAPH_VK_OMIT_BLOCK_UNKNOWN, !=, 0);

    g_free(renderer);
    g_free(pg);
}

static void test_omission_classifier_reports_individual_blockers(void)
{
    PGRAPHState *pg = g_new0(PGRAPHState, 1);
    PGRAPHVkState *renderer = g_new0(PGRAPHVkState, 1);
    SurfaceBinding color = { .initialized = true };
    SurfaceBinding zeta = { .initialized = true };
    pg->vk_renderer_state = renderer;
    renderer->color_binding = &color;
    renderer->zeta_binding = &zeta;

    PGRAPHVkOmissionDecision decision =
        pgraph_vk_classify_draw_omission(pg, renderer);
    g_assert_true(decision.safe);
    g_assert_cmphex(decision.blockers, ==, 0);

    pgraph_reg_w(pg, NV_PGRAPH_CONTROL_0,
                 NV_PGRAPH_CONTROL_0_RED_WRITE_ENABLE |
                 NV_PGRAPH_CONTROL_0_ZWRITEENABLE |
                 NV_PGRAPH_CONTROL_0_STENCIL_WRITE_ENABLE);
    pgraph_reg_w(pg, NV_PGRAPH_CONTROL_1,
                 NV_PGRAPH_CONTROL_1_STENCIL_TEST_ENABLE);
    pg->zpass_pixel_count_enable = true;
    renderer->report_queue_depth = 1;
    renderer->color_binding->download_pending = true;
    decision = pgraph_vk_classify_draw_omission(pg, renderer);
    g_assert_false(decision.safe);
    g_assert_cmphex(decision.blockers & PGRAPH_VK_OMIT_BLOCK_QUERY, !=, 0);
    g_assert_cmphex(decision.blockers & PGRAPH_VK_OMIT_BLOCK_COLOR_WRITE, !=,
                    0);
    g_assert_cmphex(decision.blockers & PGRAPH_VK_OMIT_BLOCK_DEPTH_WRITE, !=,
                    0);
    g_assert_cmphex(decision.blockers & PGRAPH_VK_OMIT_BLOCK_STENCIL, !=, 0);
    g_assert_cmphex(decision.blockers & PGRAPH_VK_OMIT_BLOCK_SURFACE_DEP, !=,
                    0);
    g_assert_cmphex(decision.blockers & PGRAPH_VK_OMIT_BLOCK_REPORT_DEP, !=,
                    0);

    g_free(renderer);
    g_free(pg);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/vk/draw-lifecycle/prepare-result-mapping",
                    test_prepare_result_mapping);
    g_test_add_func(
        "/xbox/vk/draw-lifecycle/omitted",
        test_omitted_draw_does_not_publish_surface_generation);
    g_test_add_func(
        "/xbox/vk/draw-lifecycle/failed",
        test_failed_draw_does_not_publish_surface_generation);
    g_test_add_func(
        "/xbox/vk/draw-lifecycle/submitted",
        test_submitted_draw_publishes_surface_generation);
    g_test_add_func(
        "/xbox/vk/draw-lifecycle/miss-wait",
        test_shader_miss_action_preserves_wait);
    g_test_add_func(
        "/xbox/vk/draw-lifecycle/miss-ready",
        test_shader_miss_action_uses_ready_executable);
    g_test_add_func(
        "/xbox/vk/draw-lifecycle/miss-omit",
        test_shader_miss_action_omits_supported_continue_draw);
    g_test_add_func(
        "/xbox/vk/draw-lifecycle/omission-unknown",
        test_omission_classifier_defaults_unknown_to_wait);
    g_test_add_func(
        "/xbox/vk/draw-lifecycle/omission-blockers",
        test_omission_classifier_reports_individual_blockers);
    return g_test_run();
}

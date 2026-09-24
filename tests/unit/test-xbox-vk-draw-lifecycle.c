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

void pgraph_vk_set_surface_dirty(PGRAPHState *pg, bool color, bool zeta)
{
    dirty_calls++;
    last_color_dirty = color;
    last_zeta_dirty = zeta;
}

static void test_omitted_draw_does_not_publish_surface_generation(void)
{
    PGRAPHState pg = { .draw_time = 10 };
    PGRAPHVkState renderer = { 0 };
    SurfaceBinding color = { .draw_time = 7 };
    SurfaceBinding zeta = { .draw_time = 8 };

    pg.vk_renderer_state = &renderer;
    renderer.color_binding = &color;
    renderer.zeta_binding = &zeta;
    dirty_calls = 0;

    pgraph_vk_complete_draw_lifecycle(
        &pg, &renderer, PGRAPH_VK_DRAW_OMITTED_SHADER_MISS,
        true, true, true, true);

    g_assert_cmpuint(pg.draw_time, ==, 10);
    g_assert_cmpuint(color.draw_time, ==, 7);
    g_assert_cmpuint(zeta.draw_time, ==, 8);
    g_assert_cmpuint(dirty_calls, ==, 0);
}

static void test_failed_draw_does_not_publish_surface_generation(void)
{
    PGRAPHState pg = { .draw_time = 20 };
    PGRAPHVkState renderer = { 0 };
    SurfaceBinding color = { .draw_time = 17 };
    SurfaceBinding zeta = { .draw_time = 18 };

    pg.vk_renderer_state = &renderer;
    renderer.color_binding = &color;
    renderer.zeta_binding = &zeta;
    dirty_calls = 0;

    pgraph_vk_complete_draw_lifecycle(
        &pg, &renderer, PGRAPH_VK_DRAW_FAILED,
        true, true, true, true);

    g_assert_cmpuint(pg.draw_time, ==, 20);
    g_assert_cmpuint(color.draw_time, ==, 17);
    g_assert_cmpuint(zeta.draw_time, ==, 18);
    g_assert_cmpuint(dirty_calls, ==, 0);
}

static void test_submitted_draw_publishes_surface_generation(void)
{
    PGRAPHState pg = { .draw_time = 30 };
    PGRAPHVkState renderer = { 0 };
    SurfaceBinding color = { .draw_time = 27 };
    SurfaceBinding zeta = { .draw_time = 28 };

    pg.vk_renderer_state = &renderer;
    renderer.color_binding = &color;
    renderer.zeta_binding = &zeta;
    dirty_calls = 0;

    pgraph_vk_complete_draw_lifecycle(
        &pg, &renderer, PGRAPH_VK_DRAW_SUBMITTED,
        true, true, true, true);

    g_assert_cmpuint(pg.draw_time, ==, 31);
    g_assert_cmpuint(color.draw_time, ==, 31);
    g_assert_cmpuint(zeta.draw_time, ==, 31);
    g_assert_cmpuint(dirty_calls, ==, 1);
    g_assert_true(last_color_dirty);
    g_assert_true(last_zeta_dirty);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(
        "/xbox/vk/draw-lifecycle/omitted",
        test_omitted_draw_does_not_publish_surface_generation);
    g_test_add_func(
        "/xbox/vk/draw-lifecycle/failed",
        test_failed_draw_does_not_publish_surface_generation);
    g_test_add_func(
        "/xbox/vk/draw-lifecycle/submitted",
        test_submitted_draw_publishes_surface_generation);
    return g_test_run();
}

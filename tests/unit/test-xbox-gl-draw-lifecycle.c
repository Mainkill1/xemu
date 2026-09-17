/*
 * NV2A OpenGL draw lifecycle tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/gl/draw-lifecycle.h"
#include "hw/xbox/nv2a/pgraph/gl/renderer.h"

static unsigned int dirty_calls;
static bool last_color_dirty;
static bool last_zeta_dirty;

void pgraph_gl_set_surface_dirty(PGRAPHState *pg, bool color, bool zeta)
{
    dirty_calls++;
    last_color_dirty = color;
    last_zeta_dirty = zeta;
}

static void test_rejected_draw_does_not_publish_surface_generation(void)
{
    PGRAPHState pg = { 0 };
    PGRAPHGLState renderer = { 0 };
    SurfaceBinding color = { .draw_time = 7, .draw_dirty = false };
    SurfaceBinding zeta = { .draw_time = 8, .draw_dirty = false };

    pg.gl_renderer_state = &renderer;
    pg.draw_time = 10;
    renderer.color_binding = &color;
    renderer.zeta_binding = &zeta;
    dirty_calls = 0;

    pgraph_gl_complete_draw_lifecycle(
        &pg, &renderer, PGRAPH_GL_DRAW_REJECTED,
        true, true, true, true);

    g_assert_cmpuint(pg.draw_time, ==, 10);
    g_assert_cmpuint(color.draw_time, ==, 7);
    g_assert_cmpuint(zeta.draw_time, ==, 8);
    g_assert_false(color.draw_dirty);
    g_assert_false(zeta.draw_dirty);
    g_assert_cmpuint(dirty_calls, ==, 0);

    pgraph_gl_complete_draw_lifecycle(
        &pg, &renderer, PGRAPH_GL_DRAW_SUBMITTED,
        true, true, true, true);

    g_assert_cmpuint(pg.draw_time, ==, 11);
    g_assert_cmpuint(color.draw_time, ==, 11);
    g_assert_cmpuint(zeta.draw_time, ==, 11);
    g_assert_cmpuint(dirty_calls, ==, 1);
    g_assert_true(last_color_dirty);
    g_assert_true(last_zeta_dirty);
}

static void test_empty_draw_does_not_publish_surface_generation(void)
{
    PGRAPHState pg = { .draw_time = 20 };
    PGRAPHGLState renderer = { 0 };

    dirty_calls = 0;
    pgraph_gl_complete_draw_lifecycle(
        &pg, &renderer, PGRAPH_GL_DRAW_EMPTY,
        true, true, true, true);

    g_assert_cmpuint(pg.draw_time, ==, 20);
    g_assert_cmpuint(dirty_calls, ==, 0);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/gl/draw-lifecycle/rejected",
                    test_rejected_draw_does_not_publish_surface_generation);
    g_test_add_func("/xbox/gl/draw-lifecycle/empty",
                    test_empty_draw_does_not_publish_surface_generation);
    return g_test_run();
}

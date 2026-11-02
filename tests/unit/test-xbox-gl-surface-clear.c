/*
 * Production OpenGL depth-clear extent regression.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include <SDL3/SDL.h>
#include "hw/xbox/nv2a/pgraph/gl/draw.c"

NV2AStats g_nv2a_stats;

/*
 * Attachment selection is supplied by the fixture; the production clear
 * executes against real GL storage and its pixels are read back below.
 */
void pgraph_gl_surface_update(NV2AState *d, bool upload, bool color, bool zeta)
{
    g_assert_true(upload);
    g_assert_false(color);
    g_assert_true(zeta);
}

void pgraph_gl_set_surface_dirty(PGRAPHState *pg, bool color, bool zeta)
{
    g_assert_true(pg->clearing);
    g_assert_false(color);
    g_assert_true(zeta);
}

void pgraph_get_clear_depth_stencil_value(PGRAPHState *pg, float *depth,
                                          int *stencil)
{
    *depth = 0.75f;
    *stencil = 0;
}

void pgraph_get_clear_color(PGRAPHState *pg, float color[4])
{
    g_assert_not_reached();
}

static void test_depth_clear(unsigned int extent, unsigned int scale,
                             unsigned int clear_extent)
{
    g_autofree NV2AState *d = g_new0(NV2AState, 1);
    g_autofree PGRAPHGLState *r = g_new0(PGRAPHGLState, 1);
    PGRAPHState *pg = &d->pgraph;
    SurfaceBinding color = { .width = 4, .height = 4 };
    SurfaceBinding depth = { .width = extent, .height = extent };
    GLuint framebuffer, images[2];
    unsigned int host_extent = extent * scale;
    size_t pixel_count = host_extent * host_extent;
    g_autofree float *pixels = g_new(float, pixel_count);

    pg->gl_renderer_state = r;
    pg->surface_scale_factor = scale;
    pg->surface_binding_dim.width = pg->surface_binding_dim.height = 4;
    r->color_binding = &color;
    r->zeta_binding = &depth;
    pg->regs_[NV_PGRAPH_CLEARRECTX] = (clear_extent - 1) << 16;
    pg->regs_[NV_PGRAPH_CLEARRECTY] = (clear_extent - 1) << 16;
    for (unsigned int i = 0; i < host_extent * host_extent; i++) {
        pixels[i] = 0.25f;
    }
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glGenTextures(2, images);
    glBindTexture(GL_TEXTURE_2D, images[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4 * scale, 4 * scale, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           images[0], 0);
    glBindTexture(GL_TEXTURE_2D, images[1]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, host_extent,
                 host_extent, 0, GL_DEPTH_COMPONENT, GL_FLOAT, pixels);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                           images[1], 0);
    g_assert_cmpuint(glCheckFramebufferStatus(GL_FRAMEBUFFER), ==,
                     GL_FRAMEBUFFER_COMPLETE);

    pgraph_gl_clear_surface(d, NV097_CLEAR_SURFACE_Z);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, GL_FLOAT, pixels);
    g_assert_cmpuint(glGetError(), ==, GL_NO_ERROR);
    for (unsigned int y = 0; y < host_extent; y++) {
        for (unsigned int x = 0; x < host_extent; x++) {
            float expected = x < 4 * scale && y < 4 * scale ? 0.75f : 0.25f;
            g_assert_cmpfloat(pixels[y * host_extent + x], ==, expected);
        }
    }
    g_assert_cmpint(depth.cleared, ==, extent == 4);
    g_assert_false(pg->clearing);
    glDeleteTextures(2, images);
    glDeleteFramebuffers(1, &framebuffer);
}

static void test_partial_depth_clear(void)
{
    test_depth_clear(8, 1, 4);
    test_depth_clear(8, 2, 4);
}

static void test_framebuffer_clipped_depth_clear(void)
{
    test_depth_clear(8, 1, 8);
    test_depth_clear(8, 2, 8);
}

static void test_full_depth_clear(void)
{
    test_depth_clear(4, 1, 4);
    test_depth_clear(4, 2, 4);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    if (!getenv("XEMU_TEST_GLOFFSCREEN")) {
        puts("1..0 # SKIP set XEMU_TEST_GLOFFSCREEN=1 on a graphics host");
        return EXIT_SUCCESS;
    }
    g_assert_true(SDL_Init(SDL_INIT_VIDEO));
    GloContext *context = glo_context_create();
    g_test_message("GL renderer: %s", glGetString(GL_RENDERER));
    g_test_message("GL version: %s", glGetString(GL_VERSION));
    g_test_add_func("/xbox/gl/surface/partial-depth-clear",
                    test_partial_depth_clear);
    g_test_add_func("/xbox/gl/surface/full-depth-clear", test_full_depth_clear);
    g_test_add_func("/xbox/gl/surface/framebuffer-clipped-depth-clear",
                    test_framebuffer_clipped_depth_clear);
    int result = g_test_run();
    glo_context_destroy(context);
    SDL_Quit();
    return result;
}

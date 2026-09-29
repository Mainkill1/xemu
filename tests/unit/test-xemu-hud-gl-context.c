/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <SDL3/SDL.h>
#include <glib.h>

/* Model a driver that returns exactly the requested version, as on the
 * Windows test rig. Platform creation failures must be deterministic here. */
static int requested_minor;
static int highest_minor;
static int contexts[6];

static bool set_attribute(SDL_GLAttr attribute, int value)
{
    if (attribute == SDL_GL_CONTEXT_MINOR_VERSION) {
        requested_minor = value;
    }
    return true;
}

static SDL_GLContext create_context(SDL_Window *window)
{
    (void)window;
    return requested_minor <= highest_minor ?
               (SDL_GLContext)&contexts[requested_minor] :
               NULL;
}

#define SDL_GL_SetAttribute set_attribute
#define SDL_GL_CreateContext create_context
#include "../../ui/xemu-gl-context.h"

static void test_preferred_context(void)
{
    requested_minor = 0;
    highest_minor = 5;
    SDL_GLContext context = xemu_create_hud_gl_context(NULL);
    g_assert_true(context == (SDL_GLContext)&contexts[5]);
    g_assert_cmpint(requested_minor, ==, 0);
}

static void test_fallback_context(void)
{
    requested_minor = 0;
    highest_minor = 0;
    SDL_GLContext context = xemu_create_hud_gl_context(NULL);
    g_assert_true(context == (SDL_GLContext)&contexts[0]);
    g_assert_cmpint(requested_minor, ==, 0);
}

static void test_no_context(void)
{
    requested_minor = 0;
    highest_minor = -1;
    g_assert_null(xemu_create_hud_gl_context(NULL));
    g_assert_cmpint(requested_minor, ==, 0);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xemu/hud-context/preferred", test_preferred_context);
    g_test_add_func("/xemu/hud-context/fallback", test_fallback_context);
    g_test_add_func("/xemu/hud-context/unavailable", test_no_context);
    return g_test_run();
}

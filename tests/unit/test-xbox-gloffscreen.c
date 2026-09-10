/*
 * Real SDL/OpenGL lifecycle regression. Requires an interactive graphics host:
 * XEMU_TEST_GLOFFSCREEN=1 ./test-xbox-gloffscreen
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <SDL3/SDL.h>
#include "hw/xbox/nv2a/pgraph/thirdparty/gloffscreen/gloffscreen.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "line %d: %s failed (%s)\n", \
                __LINE__, #condition, SDL_GetError()); \
        return EXIT_FAILURE; \
    } \
} while (0)

static int window_count(void)
{
    int count = -1;
    SDL_Window **windows = SDL_GetWindows(&count);
    SDL_free(windows);
    return count;
}

int main(int argc, char **argv)
{
    if (!getenv("XEMU_TEST_GLOFFSCREEN")) {
        puts("SKIP: set XEMU_TEST_GLOFFSCREEN=1 on a graphics host");
        return 77;
    }

    CHECK(SDL_Init(SDL_INIT_VIDEO));
    int baseline = window_count();
    CHECK(baseline >= 0);
    GloContext *anchor = glo_context_create();
    CHECK(window_count() == baseline + 1);
    SDL_GLContext anchor_gl = SDL_GL_GetCurrentContext();
    CHECK(anchor_gl != NULL);
    glo_context_destroy(NULL);
    CHECK(SDL_GL_GetCurrentContext() == anchor_gl);

    GLuint texture;
    const uint32_t pixel = 0xff123456;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, &pixel);
    CHECK(glGetError() == GL_NO_ERROR);

    for (int i = 0; i < 64; i++) {
        GloContext *temporary = glo_context_create();
        CHECK(window_count() == baseline + 2);
        CHECK(glIsTexture(texture));
        glo_context_destroy(temporary);
        CHECK(SDL_GL_GetCurrentContext() == NULL);
        SDL_PumpEvents();
        CHECK(window_count() == baseline + 1);
        glo_set_current(anchor);
        CHECK(SDL_GL_GetCurrentContext() == anchor_gl);
        CHECK(glIsTexture(texture));
        uint32_t actual = 0;
        glBindTexture(GL_TEXTURE_2D, texture);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, &actual);
        CHECK(glGetError() == GL_NO_ERROR);
        CHECK(actual == pixel);
    }

    glDeleteTextures(1, &texture);
    glo_context_destroy(anchor);
    SDL_PumpEvents();
    CHECK(window_count() == baseline);
    CHECK(SDL_GL_GetCurrentContext() == NULL);
    SDL_Quit();
    puts("PASS: 64 shared-context cycles, preserved texture, "
         "zero added windows");
    return EXIT_SUCCESS;
}

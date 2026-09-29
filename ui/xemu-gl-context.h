/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_GL_CONTEXT_H
#define XEMU_GL_CONTEXT_H

#include <SDL3/SDL.h>

static inline SDL_GLContext xemu_create_hud_gl_context(SDL_Window *window)
{
    /* Captured Vulkan stages use GLSL 450. Prefer a capable HUD without
     * changing the emulator's existing OpenGL 4.0 minimum. */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    SDL_GLContext context = SDL_GL_CreateContext(window);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    if (!context) {
        context = SDL_GL_CreateContext(window);
    }
    /* Later guest renderer and preview contexts inherit the original 4.0
     * request, independently of the negotiated HUD version. */
    return context;
}

#endif

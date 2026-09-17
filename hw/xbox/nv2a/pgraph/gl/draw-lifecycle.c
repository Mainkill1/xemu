/*
 * NV2A OpenGL draw completion lifecycle
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "draw-lifecycle.h"
#include "renderer.h"

void pgraph_gl_complete_draw_lifecycle(
    PGRAPHState *pg, PGRAPHGLState *r, PGRAPHGLDrawResult result,
    bool color_write, bool zeta_write, bool color_dirty, bool zeta_dirty)
{
    if (result != PGRAPH_GL_DRAW_SUBMITTED) {
        return;
    }

    pg->draw_time++;
    if (r->color_binding && color_write) {
        r->color_binding->draw_time = pg->draw_time;
    }
    if (r->zeta_binding && zeta_write) {
        r->zeta_binding->draw_time = pg->draw_time;
    }
    pgraph_gl_set_surface_dirty(pg, color_dirty, zeta_dirty);
}

/*
 * NV2A OpenGL draw completion lifecycle
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_XBOX_NV2A_PGRAPH_GL_DRAW_LIFECYCLE_H
#define HW_XBOX_NV2A_PGRAPH_GL_DRAW_LIFECYCLE_H

#include <stdbool.h>

typedef struct PGRAPHState PGRAPHState;
typedef struct PGRAPHGLState PGRAPHGLState;

typedef enum PGRAPHGLDrawResult {
    PGRAPH_GL_DRAW_EMPTY,
    PGRAPH_GL_DRAW_SUBMITTED,
    PGRAPH_GL_DRAW_REJECTED,
} PGRAPHGLDrawResult;

void pgraph_gl_complete_draw_lifecycle(
    PGRAPHState *pg, PGRAPHGLState *r, PGRAPHGLDrawResult result,
    bool color_write, bool zeta_write, bool color_dirty, bool zeta_dirty);

#endif

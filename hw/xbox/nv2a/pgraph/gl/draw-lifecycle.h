/*
 * NV2A OpenGL draw completion lifecycle
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_XBOX_NV2A_PGRAPH_GL_DRAW_LIFECYCLE_H
#define HW_XBOX_NV2A_PGRAPH_GL_DRAW_LIFECYCLE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct PGRAPHState PGRAPHState;
typedef struct PGRAPHGLState PGRAPHGLState;
typedef struct PGRAPHGLShaderTimingSlot PGRAPHGLShaderTimingSlot;
typedef struct ShaderBinding ShaderBinding;

typedef enum PGRAPHGLDrawResult {
    PGRAPH_GL_DRAW_EMPTY,
    PGRAPH_GL_DRAW_SUBMITTED,
    PGRAPH_GL_DRAW_REJECTED,
    PGRAPH_GL_DRAW_SUPPRESSED, /* Guest lifecycle without a host command. */
} PGRAPHGLDrawResult;

typedef struct PGRAPHGLDrawLifecycle {
    PGRAPHGLDrawResult result;
    bool prepared;
    bool query_active;
    bool color_write;
    bool zeta_write;
    bool color_dirty;
    bool zeta_dirty;
} PGRAPHGLDrawLifecycle;

void pgraph_gl_draw_lifecycle_reset(PGRAPHGLDrawLifecycle *lifecycle);
void pgraph_gl_draw_lifecycle_prepare(
    PGRAPHGLDrawLifecycle *lifecycle, bool query_active,
    bool color_write, bool zeta_write, bool color_dirty, bool zeta_dirty);
void pgraph_gl_draw_lifecycle_record(
    PGRAPHGLDrawLifecycle *lifecycle, PGRAPHGLDrawResult result);
bool pgraph_gl_draw_lifecycle_take_query(
    PGRAPHGLDrawLifecycle *lifecycle);
void pgraph_gl_shader_timing_record_result(PGRAPHGLShaderTimingSlot *slot,
                                           PGRAPHGLDrawResult result,
                                           const ShaderBinding *binding,
                                           uint64_t frame, uint32_t route);

void pgraph_gl_complete_draw_lifecycle(
    PGRAPHState *pg, PGRAPHGLState *r, PGRAPHGLDrawResult result,
    bool color_write, bool zeta_write, bool color_dirty, bool zeta_dirty);

#endif

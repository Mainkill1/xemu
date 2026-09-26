/*
 * OpenGL authored shader interface validation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_XBOX_NV2A_PGRAPH_GL_OVERRIDE_INTERFACE_H
#define HW_XBOX_NV2A_PGRAPH_GL_OVERRIDE_INTERFACE_H

#include "hw/xbox/nv2a/pgraph/glsl/common.h"
#include <epoxy/gl.h>

bool pgraph_gl_override_validate_uniforms(GLuint program,
                                          const UniformInfo *infos,
                                          size_t count, char **error);
bool pgraph_gl_override_validate_sampler(GLuint program, const char *name,
                                         GLenum expected_type, char **error);

#endif

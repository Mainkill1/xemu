/*
 * OpenGL authored shader interface validation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "override-interface.h"

static GLenum uniform_type(enum UniformElementType type)
{
    switch (type) {
    case UniformElementType_float:
        return GL_FLOAT;
    case UniformElementType_int:
        return GL_INT;
    case UniformElementType_ivec2:
        return GL_INT_VEC2;
    case UniformElementType_ivec4:
        return GL_INT_VEC4;
    case UniformElementType_mat2:
        return GL_FLOAT_MAT2;
    case UniformElementType_uint:
        return GL_UNSIGNED_INT;
    case UniformElementType_vec2:
        return GL_FLOAT_VEC2;
    case UniformElementType_vec3:
        return GL_FLOAT_VEC3;
    case UniformElementType_vec4:
        return GL_FLOAT_VEC4;
    }
    g_assert_not_reached();
}

bool pgraph_gl_override_validate_uniforms(GLuint program,
                                          const UniformInfo *infos,
                                          size_t count, char **error)
{
    for (size_t i = 0; i < count; ++i) {
        char name[64];
        snprintf(name, sizeof(name), infos[i].count > 1 ? "%s[0]" : "%s",
                 infos[i].name);
        const GLchar *names[] = { name };
        GLuint index = GL_INVALID_INDEX;
        glGetUniformIndices(program, 1, names, &index);
        if (index == GL_INVALID_INDEX) {
            continue;
        }
        GLint type = 0, size = 0, block = -1;
        glGetActiveUniformsiv(program, 1, &index, GL_UNIFORM_TYPE, &type);
        glGetActiveUniformsiv(program, 1, &index, GL_UNIFORM_SIZE, &size);
        glGetActiveUniformsiv(program, 1, &index, GL_UNIFORM_BLOCK_INDEX,
                              &block);
        /* OpenGL reports only the active prefix of an array. */
        if (block != -1 || (GLenum)type != uniform_type(infos[i].type) ||
            size < 1 || size > (GLint)infos[i].count) {
            *error = g_strdup_printf(
                "Uniform %s has incompatible type or size (type=0x%x size=%d)",
                infos[i].name, type, size);
            return false;
        }
    }
    GLenum gl_error = glGetError();
    if (gl_error != GL_NO_ERROR) {
        *error = g_strdup_printf("OpenGL interface reflection failed (0x%x)",
                                 gl_error);
        return false;
    }
    return true;
}

bool pgraph_gl_override_validate_sampler(GLuint program, const char *name,
                                         GLenum expected_type, char **error)
{
    const GLchar *names[] = { name };
    GLuint index = GL_INVALID_INDEX;
    glGetUniformIndices(program, 1, names, &index);
    if (index == GL_INVALID_INDEX) {
        return true;
    }
    GLint type = 0, size = 0, block = -1;
    glGetActiveUniformsiv(program, 1, &index, GL_UNIFORM_TYPE, &type);
    glGetActiveUniformsiv(program, 1, &index, GL_UNIFORM_SIZE, &size);
    glGetActiveUniformsiv(program, 1, &index, GL_UNIFORM_BLOCK_INDEX, &block);
    GLenum gl_error = glGetError();
    if (gl_error != GL_NO_ERROR || !expected_type || block != -1 || size != 1 ||
        (GLenum)type != expected_type) {
        *error = g_strdup_printf(
            "Sampler %s is incompatible with the guest texture", name);
        while (glGetError() != GL_NO_ERROR) {
        }
        return false;
    }
    return true;
}

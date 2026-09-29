// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "shader-browser-preview-model.hh"
#include <epoxy/gl.h>
#include <algorithm>
namespace xemu::shader_browser {
inline GLenum CapturedGlUniformType(const OwnedDrawUniform &uniform)
{
    static const GLenum floats[] = { 0, GL_FLOAT, GL_FLOAT_VEC2, GL_FLOAT_VEC3,
                                     GL_FLOAT_VEC4 };
    static const GLenum integers[] = { 0, GL_INT, GL_INT_VEC2, GL_INT_VEC3,
                                       GL_INT_VEC4 };
    static const GLenum uints[] = { 0, GL_UNSIGNED_INT, GL_UNSIGNED_INT_VEC2,
                                    GL_UNSIGNED_INT_VEC3,
                                    GL_UNSIGNED_INT_VEC4 };
    if (uniform.type == XEMU_SHADER_DRAW_UNIFORM_MAT2)
        return GL_FLOAT_MAT2;
    if (uniform.type == XEMU_SHADER_DRAW_UNIFORM_MAT4)
        return GL_FLOAT_MAT4;
    if (uniform.components > 4)
        return 0;
    if (uniform.type == XEMU_SHADER_DRAW_UNIFORM_FLOAT)
        return floats[uniform.components];
    if (uniform.type == XEMU_SHADER_DRAW_UNIFORM_INT)
        return integers[uniform.components];
    if (uniform.type == XEMU_SHADER_DRAW_UNIFORM_UINT)
        return uints[uniform.components];
    return 0;
}

inline size_t
ApplyCapturedGlUniforms(GLuint program,
                        const std::vector<OwnedDrawUniform> &uniforms,
                        bool original_pipeline = false)
{
    size_t unapplied = 0;
    for (const auto &uniform : uniforms) {
        if (!original_pipeline && !PreviewCapturedUniformAllowed(uniform))
            continue;
        const char *name = uniform.name.c_str();
        GLuint index = GL_INVALID_INDEX;
        glGetUniformIndices(program, 1, &name, &index);
        GLint location = glGetUniformLocation(program, name);
        if (index == GL_INVALID_INDEX || location < 0) {
            ++unapplied;
            continue;
        }
        GLint type, count;
        glGetActiveUniformsiv(program, 1, &index, GL_UNIFORM_TYPE, &type);
        glGetActiveUniformsiv(program, 1, &index, GL_UNIFORM_SIZE, &count);
        GLenum expected = CapturedGlUniformType(uniform);
        if (!expected || expected != GLenum(type) || count <= 0) {
            ++unapplied;
            continue;
        }
        if (uniform.count > uint32_t(count))
            ++unapplied;
        count = std::min(uint32_t(count), uniform.count);
        const auto *f = reinterpret_cast<const GLfloat *>(uniform.data.data());
        const auto *i = reinterpret_cast<const GLint *>(uniform.data.data());
        const auto *u = reinterpret_cast<const GLuint *>(uniform.data.data());
        if (uniform.type == XEMU_SHADER_DRAW_UNIFORM_MAT2)
            glUniformMatrix2fv(location, count, GL_FALSE, f);
        else if (uniform.type == XEMU_SHADER_DRAW_UNIFORM_MAT4)
            glUniformMatrix4fv(location, count, GL_FALSE, f);
        else if (uniform.type == XEMU_SHADER_DRAW_UNIFORM_FLOAT) {
            switch (uniform.components) {
            case 1:
                glUniform1fv(location, count, f);
                break;
            case 2:
                glUniform2fv(location, count, f);
                break;
            case 3:
                glUniform3fv(location, count, f);
                break;
            case 4:
                glUniform4fv(location, count, f);
                break;
            }
        } else if (uniform.type == XEMU_SHADER_DRAW_UNIFORM_INT) {
            switch (uniform.components) {
            case 1:
                glUniform1iv(location, count, i);
                break;
            case 2:
                glUniform2iv(location, count, i);
                break;
            case 3:
                glUniform3iv(location, count, i);
                break;
            case 4:
                glUniform4iv(location, count, i);
                break;
            }
        } else {
            switch (uniform.components) {
            case 1:
                glUniform1uiv(location, count, u);
                break;
            case 2:
                glUniform2uiv(location, count, u);
                break;
            case 3:
                glUniform3uiv(location, count, u);
                break;
            case 4:
                glUniform4uiv(location, count, u);
                break;
            }
        }
    }
    return unapplied;
}

inline GLint CapturedGlWrap(uint32_t wrap)
{
    return wrap == GL_REPEAT || wrap == GL_MIRRORED_REPEAT ||
                   wrap == GL_CLAMP_TO_EDGE ?
               GLint(wrap) :
               GL_CLAMP_TO_EDGE;
}

inline bool GlDepthClampSupported()
{
    return epoxy_gl_version() >= 32 ||
           epoxy_has_gl_extension("GL_ARB_depth_clamp");
}

inline void ApplyOriginalGlRaster(const PreviewCapturedRaster &r,
                                  uint32_t height)
{
    const GLenum factors[] = { GL_ZERO,
                               GL_ONE,
                               GL_SRC_COLOR,
                               GL_ONE_MINUS_SRC_COLOR,
                               GL_DST_COLOR,
                               GL_ONE_MINUS_DST_COLOR,
                               GL_SRC_ALPHA,
                               GL_ONE_MINUS_SRC_ALPHA,
                               GL_DST_ALPHA,
                               GL_ONE_MINUS_DST_ALPHA,
                               GL_CONSTANT_COLOR,
                               GL_ONE_MINUS_CONSTANT_COLOR,
                               GL_CONSTANT_ALPHA,
                               GL_ONE_MINUS_CONSTANT_ALPHA,
                               GL_SRC_ALPHA_SATURATE };
    const GLenum ops[] = { GL_FUNC_ADD, GL_FUNC_SUBTRACT,
                           GL_FUNC_REVERSE_SUBTRACT, GL_MIN, GL_MAX };
    const GLenum compares[] = { GL_NEVER,   GL_LESS,     GL_EQUAL,  GL_LEQUAL,
                                GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS };
    const GLenum stencil_ops[] = { GL_KEEP,      GL_ZERO,     GL_REPLACE,
                                   GL_INCR,      GL_DECR,     GL_INVERT,
                                   GL_INCR_WRAP, GL_DECR_WRAP };
    if (r.scissor_enabled) {
        glEnable(GL_SCISSOR_TEST);
        glScissor(r.scissor_x, int64_t(height) - r.scissor_y - r.scissor_height,
                  r.scissor_width, r.scissor_height);
    } else
        glDisable(GL_SCISSOR_TEST);
    glColorMask(r.color_write & 1, r.color_write & 2, r.color_write & 4,
                r.color_write & 8);
    if (r.blend_enabled)
        glEnable(GL_BLEND);
    else
        glDisable(GL_BLEND);
    glBlendFuncSeparate(factors[r.src_rgb], factors[r.dst_rgb],
                        factors[r.src_alpha], factors[r.dst_alpha]);
    glBlendEquationSeparate(ops[r.blend_rgb], ops[r.blend_alpha]);
    glBlendColor(r.blend_color[0], r.blend_color[1], r.blend_color[2],
                 r.blend_color[3]);
    if (r.depth_test)
        glEnable(GL_DEPTH_TEST);
    else
        glDisable(GL_DEPTH_TEST);
    glDepthMask(r.depth_write);
    glDepthFunc(compares[r.depth_compare]);
    glDepthRange(r.depth_min, r.depth_max);
    if (GlDepthClampSupported()) {
        if (r.depth_clamp)
            glEnable(GL_DEPTH_CLAMP);
        else
            glDisable(GL_DEPTH_CLAMP);
    }
    if (r.stencil_test)
        glEnable(GL_STENCIL_TEST);
    else
        glDisable(GL_STENCIL_TEST);
    auto stencil = [&](GLenum face, const PreviewCapturedStencil &s) {
        glStencilFuncSeparate(face, compares[s.compare], s.reference,
                              s.read_mask);
        glStencilMaskSeparate(face, s.write_mask);
        glStencilOpSeparate(face, stencil_ops[s.fail],
                            stencil_ops[s.depth_fail], stencil_ops[s.pass]);
    };
    stencil(GL_FRONT, r.front_stencil);
    stencil(GL_BACK, r.back_stencil);
    glFrontFace(r.front_ccw ? GL_CCW : GL_CW);
    if (r.cull_mode) {
        glEnable(GL_CULL_FACE);
        glCullFace(r.cull_mode == 1 ? GL_FRONT :
                   r.cull_mode == 2 ? GL_BACK :
                                      GL_FRONT_AND_BACK);
    } else
        glDisable(GL_CULL_FACE);
    if (r.depth_bias) {
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(r.bias_slope, r.bias_constant);
    } else
        glDisable(GL_POLYGON_OFFSET_FILL);
}

} // namespace xemu::shader_browser

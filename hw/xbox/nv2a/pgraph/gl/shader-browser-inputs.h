/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_XBOX_NV2A_PGRAPH_GL_SHADER_BROWSER_INPUTS_H
#define HW_XBOX_NV2A_PGRAPH_GL_SHADER_BROWSER_INPUTS_H

#include "renderer.h"
#include "ui/xui/shader-browser-draw-inputs.h"
#include <glib.h>

/* capture.*.status registers are bitmasks. Metadata survives partial captures;
 * a missing image/source must never be presented as a replay of the game. */
enum {
    XEMU_GL_INPUT_MISSING = 1,
    XEMU_GL_INPUT_UNSUPPORTED = 2,
    XEMU_GL_INPUT_LIMIT = 4,
    XEMU_GL_INPUT_ALLOCATION = 8,
    XEMU_GL_INPUT_REJECTED = 16,
};

typedef struct XemuShaderDrawGLPackState {
    GLint buffer;
    GLint values[8];
} XemuShaderDrawGLPackState;

static const GLenum xemu_shader_draw_gl_pack_parameters[] = {
    GL_PACK_ALIGNMENT,   GL_PACK_ROW_LENGTH, GL_PACK_IMAGE_HEIGHT,
    GL_PACK_SKIP_PIXELS, GL_PACK_SKIP_ROWS,  GL_PACK_SKIP_IMAGES,
    GL_PACK_SWAP_BYTES,  GL_PACK_LSB_FIRST,
};

static inline void
xemu_shader_draw_gl_pack_begin(XemuShaderDrawGLPackState *state)
{
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &state->buffer);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    for (size_t i = 0; i < G_N_ELEMENTS(state->values); ++i) {
        glGetIntegerv(xemu_shader_draw_gl_pack_parameters[i],
                      &state->values[i]);
        glPixelStorei(xemu_shader_draw_gl_pack_parameters[i], i == 0 ? 1 : 0);
    }
}

static inline void
xemu_shader_draw_gl_pack_end(const XemuShaderDrawGLPackState *state)
{
    for (size_t i = 0; i < G_N_ELEMENTS(state->values); ++i)
        glPixelStorei(xemu_shader_draw_gl_pack_parameters[i], state->values[i]);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, state->buffer);
}

static inline size_t xemu_shader_draw_gl_image_bytes(GLint width, GLint height)
{
    if (width <= 0 || height <= 0 || width > 2048 || height > 2048)
        return 0;
    return (size_t)width * height * 4;
}

static inline void
xemu_shader_draw_gl_flip_image(uint8_t *pixels, uint32_t width, uint32_t height)
{
    for (uint32_t y = 0; y < height / 2; ++y) {
        uint8_t *a = pixels + (size_t)y * width * 4;
        uint8_t *b = pixels + (size_t)(height - 1 - y) * width * 4;
        for (size_t x = 0; x < (size_t)width * 4; ++x) {
            uint8_t tmp = a[x];
            a[x] = b[x];
            b[x] = tmp;
        }
    }
}

/* Query the actual attachment of the command's draw framebuffer. xemu's color
 * surfaces are 2D textures; other attachments are explicitly unsupported. */
static inline bool xemu_shader_draw_gl_target(PGRAPHState *pg, GLint *width,
                                              GLint *height)
{
    PGRAPHGLState *r = pg->gl_renderer_state;
    GLint draw, read, type = GL_NONE, object = 0, previous, level = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
    if (!r->color_binding || (GLuint)draw != r->gl_framebuffer)
        return false;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, draw);
    glGetFramebufferAttachmentParameteriv(
        GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &type);
    if (type == GL_TEXTURE) {
        glGetFramebufferAttachmentParameteriv(
            GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &object);
        glGetFramebufferAttachmentParameteriv(
            GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL, &level);
    }
    bool supported = type == GL_TEXTURE &&
                     (GLuint)object == r->color_binding->gl_buffer &&
                     glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) ==
                         GL_FRAMEBUFFER_COMPLETE;
    if (supported) {
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
        glBindTexture(GL_TEXTURE_2D, object);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_WIDTH, width);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_HEIGHT,
                                 height);
        glBindTexture(GL_TEXTURE_2D, previous);
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, read);
    return supported;
}

static inline size_t
xemu_shader_draw_gl_stage_target(PGRAPHState *pg, uint64_t token, bool before)
{
    GLint width = 0, height = 0;
    uint32_t status = 0;
    size_t bytes = 0;
    if (!xemu_shader_draw_gl_target(pg, &width, &height)) {
        status = XEMU_GL_INPUT_MISSING | XEMU_GL_INPUT_UNSUPPORTED;
    } else if (!(bytes = xemu_shader_draw_gl_image_bytes(width, height))) {
        status = XEMU_GL_INPUT_LIMIT;
    } else {
        g_autofree uint8_t *pixels = g_try_malloc(bytes);
        if (!pixels) {
            status = XEMU_GL_INPUT_ALLOCATION;
        } else {
            GLint draw, read, read_buffer;
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, draw);
            glGetIntegerv(GL_READ_BUFFER, &read_buffer);
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            XemuShaderDrawGLPackState pack;
            xemu_shader_draw_gl_pack_begin(&pack);
            glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE,
                         pixels);
            xemu_shader_draw_gl_pack_end(&pack);
            glReadBuffer(read_buffer);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, read);
            xemu_shader_draw_gl_flip_image(pixels, width, height);
            XemuShaderDrawImage image = { width, height, pixels, bytes };
            if (!xemu_shader_draw_request_stage_image(token, before, &image))
                status = XEMU_GL_INPUT_REJECTED;
        }
    }
    xemu_shader_draw_request_stage_register(
        token, before ? "capture.before.status" : "capture.after.status",
        status);
    xemu_shader_draw_request_stage_register(
        token, before ? "capture.before.width" : "capture.after.width", width);
    xemu_shader_draw_request_stage_register(
        token, before ? "capture.before.height" : "capture.after.height",
        height);
    return bytes;
}

static inline void xemu_shader_draw_gl_stage_program(uint64_t token,
                                                     size_t *budget)
{
    GLint program, attached, active, name_size;
    uint32_t source_status = 0, uniform_status = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    xemu_shader_draw_request_stage_register(token, "capture.gl_program",
                                            program);
    if (!program) {
        xemu_shader_draw_request_stage_register(token, "capture.sources.status",
                                                XEMU_GL_INPUT_MISSING);
        xemu_shader_draw_request_stage_register(
            token, "capture.uniforms.status", XEMU_GL_INPUT_MISSING);
        return;
    }
    glGetProgramiv(program, GL_ATTACHED_SHADERS, &attached);
    if (attached <= 0 || attached > 16) {
        source_status =
            attached <= 0 ? XEMU_GL_INPUT_MISSING : XEMU_GL_INPUT_LIMIT;
    } else {
        GLuint shaders[16];
        GLsizei count = 0;
        bool stages[4] = { false };
        glGetAttachedShaders(program, 16, &count, shaders);
        for (GLsizei i = 0; i < count; ++i) {
            GLint type, length;
            glGetShaderiv(shaders[i], GL_SHADER_TYPE, &type);
            uint32_t stage = type == GL_VERTEX_SHADER ?
                                 XEMU_SHADER_BROWSER_STAGE_VERTEX :
                             type == GL_FRAGMENT_SHADER ?
                                 XEMU_SHADER_BROWSER_STAGE_PIXEL :
                             type == GL_GEOMETRY_SHADER ?
                                 XEMU_SHADER_BROWSER_STAGE_GEOMETRY :
                                 0;
            if (!stage || stages[stage]) {
                source_status |= XEMU_GL_INPUT_UNSUPPORTED;
                continue;
            }
            glGetShaderiv(shaders[i], GL_SHADER_SOURCE_LENGTH, &length);
            if (length <= 1) {
                source_status |= XEMU_GL_INPUT_MISSING;
                continue;
            }
            if (length > 4 * 1024 * 1024 || (size_t)length > *budget) {
                source_status |= XEMU_GL_INPUT_LIMIT;
                continue;
            }
            g_autofree char *source = g_try_malloc(length);
            if (!source) {
                source_status |= XEMU_GL_INPUT_ALLOCATION;
                continue;
            }
            GLsizei written = 0;
            glGetShaderSource(shaders[i], length, &written, source);
            if (written <= 0 || !xemu_shader_draw_request_stage_source(
                                    token, stage, source, written)) {
                source_status |= XEMU_GL_INPUT_REJECTED;
            } else {
                stages[stage] = true;
                *budget -= length;
            }
        }
        if (!stages[XEMU_SHADER_BROWSER_STAGE_VERTEX] ||
            !stages[XEMU_SHADER_BROWSER_STAGE_PIXEL])
            source_status |= XEMU_GL_INPUT_MISSING;
    }
    glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &active);
    glGetProgramiv(program, GL_ACTIVE_UNIFORM_MAX_LENGTH, &name_size);
    xemu_shader_draw_request_stage_register(token, "capture.uniforms.active",
                                            active);
    if (active > 2048 || name_size > 1024) {
        uniform_status = XEMU_GL_INPUT_LIMIT;
    } else if (active > 0 && name_size > 0) {
        char name[1024];
        size_t captured = 0;
        for (GLint i = 0; i < active; ++i) {
            if (captured == 256) {
                uniform_status |= XEMU_GL_INPUT_LIMIT;
                break;
            }
            GLsizei length;
            GLint count;
            GLenum gl_type;
            glGetActiveUniform(program, i, sizeof(name), &length, &count,
                               &gl_type, name);
            GLuint index = i;
            GLint block = -1;
            glGetActiveUniformsiv(program, 1, &index, GL_UNIFORM_BLOCK_INDEX,
                                  &block);
            if (block >= 0) {
                uniform_status |= XEMU_GL_INPUT_UNSUPPORTED;
                continue;
            }
            uint32_t type = 0, components = 0;
            switch (gl_type) {
            case GL_FLOAT:
                components = 1;
                type = XEMU_SHADER_DRAW_UNIFORM_FLOAT;
                break;
            case GL_FLOAT_VEC2:
                components = 2;
                type = XEMU_SHADER_DRAW_UNIFORM_FLOAT;
                break;
            case GL_FLOAT_VEC3:
                components = 3;
                type = XEMU_SHADER_DRAW_UNIFORM_FLOAT;
                break;
            case GL_FLOAT_VEC4:
                components = 4;
                type = XEMU_SHADER_DRAW_UNIFORM_FLOAT;
                break;
            case GL_FLOAT_MAT2:
                components = 4;
                type = XEMU_SHADER_DRAW_UNIFORM_MAT2;
                break;
            case GL_FLOAT_MAT4:
                components = 16;
                type = XEMU_SHADER_DRAW_UNIFORM_MAT4;
                break;
            case GL_INT:
            case GL_BOOL:
            case GL_SAMPLER_1D:
            case GL_SAMPLER_2D:
            case GL_SAMPLER_3D:
            case GL_SAMPLER_CUBE:
            case GL_SAMPLER_2D_SHADOW:
            case GL_SAMPLER_1D_SHADOW:
            case GL_SAMPLER_CUBE_SHADOW:
            case GL_SAMPLER_1D_ARRAY:
            case GL_SAMPLER_2D_ARRAY:
            case GL_SAMPLER_1D_ARRAY_SHADOW:
            case GL_SAMPLER_2D_ARRAY_SHADOW:
            case GL_SAMPLER_2D_RECT:
            case GL_SAMPLER_2D_RECT_SHADOW:
            case GL_SAMPLER_BUFFER:
            case GL_SAMPLER_2D_MULTISAMPLE:
            case GL_SAMPLER_2D_MULTISAMPLE_ARRAY:
            case GL_INT_SAMPLER_1D:
            case GL_INT_SAMPLER_2D:
            case GL_INT_SAMPLER_3D:
            case GL_INT_SAMPLER_CUBE:
            case GL_INT_SAMPLER_1D_ARRAY:
            case GL_INT_SAMPLER_2D_ARRAY:
            case GL_INT_SAMPLER_2D_RECT:
            case GL_INT_SAMPLER_BUFFER:
            case GL_INT_SAMPLER_2D_MULTISAMPLE:
            case GL_INT_SAMPLER_2D_MULTISAMPLE_ARRAY:
            case GL_UNSIGNED_INT_SAMPLER_1D:
            case GL_UNSIGNED_INT_SAMPLER_2D:
            case GL_UNSIGNED_INT_SAMPLER_3D:
            case GL_UNSIGNED_INT_SAMPLER_CUBE:
            case GL_UNSIGNED_INT_SAMPLER_1D_ARRAY:
            case GL_UNSIGNED_INT_SAMPLER_2D_ARRAY:
            case GL_UNSIGNED_INT_SAMPLER_2D_RECT:
            case GL_UNSIGNED_INT_SAMPLER_BUFFER:
            case GL_UNSIGNED_INT_SAMPLER_2D_MULTISAMPLE:
            case GL_UNSIGNED_INT_SAMPLER_2D_MULTISAMPLE_ARRAY:
                components = 1;
                type = XEMU_SHADER_DRAW_UNIFORM_INT;
                break;
            case GL_INT_VEC2:
            case GL_BOOL_VEC2:
                components = 2;
                type = XEMU_SHADER_DRAW_UNIFORM_INT;
                break;
            case GL_INT_VEC3:
            case GL_BOOL_VEC3:
                components = 3;
                type = XEMU_SHADER_DRAW_UNIFORM_INT;
                break;
            case GL_INT_VEC4:
            case GL_BOOL_VEC4:
                components = 4;
                type = XEMU_SHADER_DRAW_UNIFORM_INT;
                break;
            case GL_UNSIGNED_INT:
                components = 1;
                type = XEMU_SHADER_DRAW_UNIFORM_UINT;
                break;
            case GL_UNSIGNED_INT_VEC2:
                components = 2;
                type = XEMU_SHADER_DRAW_UNIFORM_UINT;
                break;
            case GL_UNSIGNED_INT_VEC3:
                components = 3;
                type = XEMU_SHADER_DRAW_UNIFORM_UINT;
                break;
            case GL_UNSIGNED_INT_VEC4:
                components = 4;
                type = XEMU_SHADER_DRAW_UNIFORM_UINT;
                break;
            default:
                uniform_status |= XEMU_GL_INPUT_UNSUPPORTED;
                continue;
            }
            if (count <= 0 || count > 4096) {
                uniform_status |= XEMU_GL_INPUT_LIMIT;
                continue;
            }
            size_t bytes = (size_t)count * components * 4;
            if (bytes > 65536 || bytes + (size_t)length > *budget) {
                uniform_status |= XEMU_GL_INPUT_LIMIT;
                continue;
            }
            g_autofree uint8_t *data = g_try_malloc(bytes);
            if (!data) {
                uniform_status |= XEMU_GL_INPUT_ALLOCATION;
                continue;
            }
            bool valid = true;
            char *array = strstr(name, "[0]");
            if (array) {
                char *next;
                while ((next = strstr(array + 3, "[0]")))
                    array = next;
            }
            for (GLint element = 0; element < count; ++element) {
                char element_name[1056];
                if (array) {
                    snprintf(element_name, sizeof(element_name), "%.*s[%d]%s",
                             (int)(array - name), name, element, array + 3);
                } else {
                    snprintf(element_name, sizeof(element_name), "%s", name);
                    if (element) {
                        valid = false;
                        break;
                    }
                }
                GLint location = glGetUniformLocation(program, element_name);
                if (location < 0) {
                    valid = false;
                    break;
                }
                void *value = data + (size_t)element * components * 4;
                if (type == XEMU_SHADER_DRAW_UNIFORM_INT)
                    glGetUniformiv(program, location, value);
                else if (type == XEMU_SHADER_DRAW_UNIFORM_UINT)
                    glGetUniformuiv(program, location, value);
                else
                    glGetUniformfv(program, location, value);
            }
            XemuShaderDrawUniform uniform = {
                .stage = 0,
                .name = name,
                .type = type,
                .components = components,
                .count = count,
                .data = data,
                .byte_count = bytes,
            };
            if (!valid ||
                !xemu_shader_draw_request_stage_uniform(token, &uniform))
                uniform_status |= XEMU_GL_INPUT_REJECTED;
            else {
                *budget -= bytes + length;
                ++captured;
            }
        }
    }
    xemu_shader_draw_request_stage_register(token, "capture.sources.status",
                                            source_status);
    xemu_shader_draw_request_stage_register(token, "capture.uniforms.status",
                                            uniform_status);
}

static inline void xemu_shader_draw_gl_texture_register(uint64_t token,
                                                        uint32_t slot,
                                                        const char *field,
                                                        uint32_t value)
{
    char name[64];
    snprintf(name, sizeof(name), "capture.texture%u.%s", slot, field);
    xemu_shader_draw_request_stage_register(token, name, value);
}

static inline GLint
xemu_shader_draw_gl_sampler_int(GLenum target, GLuint sampler, GLenum parameter)
{
    GLint value;
    if (sampler)
        glGetSamplerParameteriv(sampler, parameter, &value);
    else
        glGetTexParameteriv(target, parameter, &value);
    return value;
}

static inline void
xemu_shader_draw_gl_sampler_float(uint64_t token, uint32_t slot, GLenum target,
                                  GLuint sampler, GLenum parameter,
                                  const char *field)
{
    GLfloat value;
    uint32_t bits;
    if (sampler)
        glGetSamplerParameterfv(sampler, parameter, &value);
    else
        glGetTexParameterfv(target, parameter, &value);
    memcpy(&bits, &value, sizeof(bits));
    xemu_shader_draw_gl_texture_register(token, slot, field, bits);
}

static inline void xemu_shader_draw_gl_stage_textures(PGRAPHState *pg,
                                                      uint64_t token,
                                                      size_t *budget)
{
    PGRAPHGLState *r = pg->gl_renderer_state;
    GLint active;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    XemuShaderDrawGLPackState pack;
    xemu_shader_draw_gl_pack_begin(&pack);
    for (uint32_t slot = 0; slot < 4; ++slot) {
        TextureBinding *binding = r->texture_binding[slot];
        GLenum target = binding ? binding->gl_target : GL_TEXTURE_2D;
        GLenum query = target == GL_TEXTURE_1D ? GL_TEXTURE_BINDING_1D :
                       target == GL_TEXTURE_2D ? GL_TEXTURE_BINDING_2D :
                       target == GL_TEXTURE_3D ? GL_TEXTURE_BINDING_3D :
                       target == GL_TEXTURE_CUBE_MAP ?
                                                 GL_TEXTURE_BINDING_CUBE_MAP :
                                                 0;
        XemuShaderDrawTexture texture = {
            .slot = slot,
            .guest_format = pgraph_reg_r(pg, NV_PGRAPH_TEXFMT0 + slot * 4),
            .coordinate_scale = binding ? binding->scale : 1,
        };
        uint32_t status = 0;
        GLint object = 0;
        glActiveTexture(GL_TEXTURE0 + slot);
        if (query)
            glGetIntegerv(query, &object);
        texture.bound = object != 0;
        if (xemu_shader_capture_session_token(token) && object) {
            XemuShaderCaptureResource resource = {
                .owner = binding && binding->gl_texture == (GLuint)object ?
                             binding->capture_owner :
                             0,
                .byte_size = 1,
                .size = 1,
                .access = XEMU_SHADER_CAPTURE_RESOURCE_READ,
                .kind = XEMU_SHADER_CAPTURE_RESOURCE_TEXTURE,
                .slot = slot,
                .flags = XEMU_SHADER_CAPTURE_RESOURCE_OPAQUE_EXTENT,
            };
            xemu_shader_capture_session_resource(token, &resource);
        }
        xemu_shader_draw_gl_texture_register(token, slot, "target", target);
        xemu_shader_draw_gl_texture_register(token, slot, "gl_object", object);
        if (!object || !query) {
            if (!query)
                status |= XEMU_GL_INPUT_UNSUPPORTED;
            if (pgraph_is_texture_enabled(pg, slot) && !object)
                status |= XEMU_GL_INPUT_MISSING;
            xemu_shader_draw_request_stage_texture(token, &texture);
        } else {
            GLint value, base, max, sampler;
            glGetIntegerv(GL_SAMPLER_BINDING, &sampler);
            xemu_shader_draw_gl_texture_register(token, slot, "sampler",
                                                 sampler);
#define XEMU_GL_TEXTURE_PARAMETER(parameter, field) \
    texture.field = xemu_shader_draw_gl_sampler_int(target, sampler, parameter)
            XEMU_GL_TEXTURE_PARAMETER(GL_TEXTURE_MIN_FILTER, min_filter);
            XEMU_GL_TEXTURE_PARAMETER(GL_TEXTURE_MAG_FILTER, mag_filter);
            XEMU_GL_TEXTURE_PARAMETER(GL_TEXTURE_WRAP_S, wrap_s);
            XEMU_GL_TEXTURE_PARAMETER(GL_TEXTURE_WRAP_T, wrap_t);
            XEMU_GL_TEXTURE_PARAMETER(GL_TEXTURE_WRAP_R, wrap_r);
#undef XEMU_GL_TEXTURE_PARAMETER
            xemu_shader_draw_gl_sampler_float(token, slot, target, sampler,
                                              GL_TEXTURE_MIN_LOD,
                                              "min_lod_bits");
            xemu_shader_draw_gl_sampler_float(token, slot, target, sampler,
                                              GL_TEXTURE_MAX_LOD,
                                              "max_lod_bits");
            xemu_shader_draw_gl_sampler_float(token, slot, target, sampler,
                                              GL_TEXTURE_LOD_BIAS,
                                              "lod_bias_bits");
            if (r->supported_extensions.texture_filter_anisotropic)
                xemu_shader_draw_gl_sampler_float(token, slot, target, sampler,
                                                  GL_TEXTURE_MAX_ANISOTROPY_EXT,
                                                  "anisotropy_bits");
            xemu_shader_draw_gl_texture_register(
                token, slot, "compare_mode",
                xemu_shader_draw_gl_sampler_int(target, sampler,
                                                GL_TEXTURE_COMPARE_MODE));
            xemu_shader_draw_gl_texture_register(
                token, slot, "compare_func",
                xemu_shader_draw_gl_sampler_int(target, sampler,
                                                GL_TEXTURE_COMPARE_FUNC));
            GLfloat border[4];
            if (sampler)
                glGetSamplerParameterfv(sampler, GL_TEXTURE_BORDER_COLOR,
                                        border);
            else
                glGetTexParameterfv(target, GL_TEXTURE_BORDER_COLOR, border);
            for (uint32_t channel = 0; channel < 4; ++channel) {
                char field[32];
                uint32_t bits;
                memcpy(&bits, &border[channel], sizeof(bits));
                snprintf(field, sizeof(field), "border%u_bits", channel);
                xemu_shader_draw_gl_texture_register(token, slot, field, bits);
            }
            GLint swizzle[4];
            glGetTexParameteriv(target, GL_TEXTURE_SWIZZLE_RGBA, swizzle);
            for (uint32_t channel = 0; channel < 4; ++channel) {
                char field[32];
                snprintf(field, sizeof(field), "swizzle%u", channel);
                xemu_shader_draw_gl_texture_register(token, slot, field,
                                                     swizzle[channel]);
            }
            glGetTexParameteriv(target, GL_TEXTURE_BASE_LEVEL, &base);
            glGetTexParameteriv(target, GL_TEXTURE_MAX_LEVEL, &max);
            xemu_shader_draw_gl_texture_register(token, slot, "base_level",
                                                 base);
            xemu_shader_draw_gl_texture_register(token, slot, "max_level", max);
            texture.face_count = target == GL_TEXTURE_CUBE_MAP ? 6 : 1;
            GLenum image_target = target == GL_TEXTURE_CUBE_MAP ?
                                      GL_TEXTURE_CUBE_MAP_POSITIVE_X :
                                      target;
            GLint width = 0, height = 1, depth = 1;
            glGetTexLevelParameteriv(image_target, 0, GL_TEXTURE_WIDTH, &width);
            if (target != GL_TEXTURE_1D)
                glGetTexLevelParameteriv(image_target, 0, GL_TEXTURE_HEIGHT,
                                         &height);
            if (target == GL_TEXTURE_3D)
                glGetTexLevelParameteriv(image_target, 0, GL_TEXTURE_DEPTH,
                                         &depth);
            glGetTexLevelParameteriv(image_target, 0,
                                     GL_TEXTURE_INTERNAL_FORMAT, &value);
            texture.width = width;
            texture.height = height;
            texture.depth = depth;
            texture.host_format = value;
            xemu_shader_draw_gl_texture_register(token, slot, "width", width);
            xemu_shader_draw_gl_texture_register(token, slot, "height", height);
            xemu_shader_draw_gl_texture_register(token, slot, "depth", depth);
            GLint depth_bits = 0;
            glGetTexLevelParameteriv(image_target, 0, GL_TEXTURE_DEPTH_SIZE,
                                     &depth_bits);
            bool supported = target != GL_TEXTURE_3D && !depth_bits;
            if (!supported)
                status |= XEMU_GL_INPUT_UNSUPPORTED;
            if (width <= 0 || height <= 0)
                status |= XEMU_GL_INPUT_MISSING;
            for (GLint mip = 0; mip < 16; ++mip) {
                glGetTexLevelParameteriv(image_target, mip, GL_TEXTURE_WIDTH,
                                         &width);
                if (width <= 0)
                    break;
                ++texture.mip_levels;
                height = 1;
                if (target != GL_TEXTURE_1D)
                    glGetTexLevelParameteriv(image_target, mip,
                                             GL_TEXTURE_HEIGHT, &height);
                depth = 1;
                if (target == GL_TEXTURE_3D)
                    glGetTexLevelParameteriv(image_target, mip,
                                             GL_TEXTURE_DEPTH, &depth);
                if (width == 1 && height == 1 && depth == 1)
                    break;
            }
            if (!xemu_shader_draw_request_stage_texture(token, &texture))
                status |= XEMU_GL_INPUT_REJECTED;
            for (uint32_t mip = 0; supported && mip < texture.mip_levels;
                 ++mip) {
                for (uint32_t face = 0; face < texture.face_count; ++face) {
                    GLenum face_target =
                        target == GL_TEXTURE_CUBE_MAP ?
                            GL_TEXTURE_CUBE_MAP_POSITIVE_X + face :
                            target;
                    glGetTexLevelParameteriv(face_target, mip, GL_TEXTURE_WIDTH,
                                             &width);
                    height = 1;
                    if (target != GL_TEXTURE_1D)
                        glGetTexLevelParameteriv(face_target, mip,
                                                 GL_TEXTURE_HEIGHT, &height);
                    size_t bytes =
                        xemu_shader_draw_gl_image_bytes(width, height);
                    if (!bytes || bytes > *budget) {
                        status |= XEMU_GL_INPUT_LIMIT;
                        continue;
                    }
                    g_autofree uint8_t *pixels = g_try_malloc(bytes);
                    if (!pixels) {
                        status |= XEMU_GL_INPUT_ALLOCATION;
                        continue;
                    }
                    glGetTexImage(face_target, mip, GL_RGBA, GL_UNSIGNED_BYTE,
                                  pixels);
                    xemu_shader_draw_gl_flip_image(pixels, width, height);
                    texture.mip_level = mip;
                    texture.face = face;
                    texture.image =
                        (XemuShaderDrawImage){ width, height, pixels, bytes };
                    if (!xemu_shader_draw_request_stage_texture(token,
                                                                &texture))
                        status |= XEMU_GL_INPUT_REJECTED;
                    else
                        *budget -= bytes;
                }
            }
        }
        xemu_shader_draw_gl_texture_register(token, slot, "status", status);
    }
    xemu_shader_draw_gl_pack_end(&pack);
    glActiveTexture(active);
}

static inline void xemu_shader_draw_gl_stage_registers(PGRAPHState *pg,
                                                       uint64_t token)
{
    static const struct {
        const char *name;
        uint32_t offset;
    } registers[] = {
#define XEMU_GL_REGISTER(reg) { #reg, reg }
        XEMU_GL_REGISTER(NV_PGRAPH_SURFACE),
        XEMU_GL_REGISTER(NV_PGRAPH_CONTROL_0),
        XEMU_GL_REGISTER(NV_PGRAPH_CONTROL_1),
        XEMU_GL_REGISTER(NV_PGRAPH_CONTROL_2),
        XEMU_GL_REGISTER(NV_PGRAPH_CONTROL_3),
        XEMU_GL_REGISTER(NV_PGRAPH_BLEND),
        XEMU_GL_REGISTER(NV_PGRAPH_BLENDCOLOR),
        XEMU_GL_REGISTER(NV_PGRAPH_SETUPRASTER),
        XEMU_GL_REGISTER(NV_PGRAPH_SHADERCLIPMODE),
        XEMU_GL_REGISTER(NV_PGRAPH_SHADERCTL),
        XEMU_GL_REGISTER(NV_PGRAPH_SHADERPROG),
        XEMU_GL_REGISTER(NV_PGRAPH_COMBINECTL),
        XEMU_GL_REGISTER(NV_PGRAPH_COMBINESPECFOG0),
        XEMU_GL_REGISTER(NV_PGRAPH_COMBINESPECFOG1),
        XEMU_GL_REGISTER(NV_PGRAPH_FOGCOLOR),
        XEMU_GL_REGISTER(NV_PGRAPH_FOGPARAM0),
        XEMU_GL_REGISTER(NV_PGRAPH_FOGPARAM1),
        XEMU_GL_REGISTER(NV_PGRAPH_ZCLIPMIN),
        XEMU_GL_REGISTER(NV_PGRAPH_ZCLIPMAX),
        XEMU_GL_REGISTER(NV_PGRAPH_ZOFFSETBIAS),
        XEMU_GL_REGISTER(NV_PGRAPH_ZOFFSETFACTOR),
#undef XEMU_GL_REGISTER
    };
    for (size_t i = 0; i < G_N_ELEMENTS(registers); ++i)
        xemu_shader_draw_request_stage_register(
            token, registers[i].name, pgraph_reg_r(pg, registers[i].offset));
    static const struct {
        const char *name;
        uint32_t offset;
        uint32_t count;
    } groups[] = {
        { "NV_PGRAPH_TEXFMT", NV_PGRAPH_TEXFMT0, 4 },
        { "NV_PGRAPH_TEXCTL0_", NV_PGRAPH_TEXCTL0_0, 4 },
        { "NV_PGRAPH_TEXCTL1_", NV_PGRAPH_TEXCTL1_0, 4 },
        { "NV_PGRAPH_TEXFILTER", NV_PGRAPH_TEXFILTER0, 4 },
        { "NV_PGRAPH_TEXADDRESS", NV_PGRAPH_TEXADDRESS0, 4 },
        { "NV_PGRAPH_TEXIMAGERECT", NV_PGRAPH_TEXIMAGERECT0, 4 },
        { "NV_PGRAPH_TEXOFFSET", NV_PGRAPH_TEXOFFSET0, 4 },
        { "NV_PGRAPH_TEXPALETTE", NV_PGRAPH_TEXPALETTE0, 4 },
        { "NV_PGRAPH_BORDERCOLOR", NV_PGRAPH_BORDERCOLOR0, 4 },
        { "NV_PGRAPH_COMBINEFACTOR0[", NV_PGRAPH_COMBINEFACTOR0, 8 },
        { "NV_PGRAPH_COMBINEFACTOR1[", NV_PGRAPH_COMBINEFACTOR1, 8 },
        { "NV_PGRAPH_COMBINEALPHAI", NV_PGRAPH_COMBINEALPHAI0, 8 },
        { "NV_PGRAPH_COMBINEALPHAO", NV_PGRAPH_COMBINEALPHAO0, 8 },
        { "NV_PGRAPH_COMBINECOLORI", NV_PGRAPH_COMBINECOLORI0, 8 },
        { "NV_PGRAPH_COMBINECOLORO", NV_PGRAPH_COMBINECOLORO0, 8 },
        { "NV_PGRAPH_WINDOWCLIPX", NV_PGRAPH_WINDOWCLIPX0, 8 },
        { "NV_PGRAPH_WINDOWCLIPY", NV_PGRAPH_WINDOWCLIPY0, 8 },
    };
    for (size_t group = 0; group < G_N_ELEMENTS(groups); ++group) {
        for (uint32_t i = 0; i < groups[group].count; ++i) {
            char name[64];
            snprintf(name, sizeof(name), "%s%u%s", groups[group].name, i,
                     strchr(groups[group].name, '[') ? "]" : "");
            xemu_shader_draw_request_stage_register(
                token, name, pgraph_reg_r(pg, groups[group].offset + i * 4));
        }
    }
}

static inline bool xemu_shader_draw_gl_wants_inputs(uint64_t token)
{
    return token && xemu_shader_draw_request_wants_inputs(token) &&
           (xemu_shader_capture_session_token(token) ||
            xemu_shader_draw_request_has_geometry(token));
}

static inline void shader_capture_gl_host_state(uint64_t token)
{
    GLint viewport[4], scissor[4];
    GLdouble depth_range[2];
    GLboolean color_mask[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_SCISSOR_BOX, scissor);
    glGetDoublev(GL_DEPTH_RANGE, depth_range);
    glGetBooleanv(GL_COLOR_WRITEMASK, color_mask);
    const struct {
        const char *name;
        const void *data;
        size_t bytes;
    } values[] = {
        { "host.viewport", viewport, sizeof(viewport) },
        { "host.scissor", scissor, sizeof(scissor) },
        { "host.depth_range", depth_range, sizeof(depth_range) },
        { "host.color_write", color_mask, sizeof(color_mask) },
    };
    for (size_t i = 0; i < G_N_ELEMENTS(values); ++i) {
        XemuShaderDrawBlob blob = { .name = values[i].name,
                                    .data = values[i].data,
                                    .byte_count = values[i].bytes };
        xemu_shader_draw_request_stage_blob(token, &blob);
    }
    const struct {
        const char *name;
        GLenum pname;
    } states[] = {
        { "host.scissor_enabled", GL_SCISSOR_TEST },
        { "host.blend_enabled", GL_BLEND },
        { "host.blend_equation_rgb", GL_BLEND_EQUATION_RGB },
        { "host.blend_equation_alpha", GL_BLEND_EQUATION_ALPHA },
        { "host.blend_src_rgb", GL_BLEND_SRC_RGB },
        { "host.blend_dst_rgb", GL_BLEND_DST_RGB },
        { "host.blend_src_alpha", GL_BLEND_SRC_ALPHA },
        { "host.blend_dst_alpha", GL_BLEND_DST_ALPHA },
        { "host.depth_enabled", GL_DEPTH_TEST },
        { "host.depth_clamp_enabled", GL_DEPTH_CLAMP },
        { "host.sample_buffers", GL_SAMPLE_BUFFERS },
        { "host.samples", GL_SAMPLES },
        { "host.multisample_enabled", GL_MULTISAMPLE },
        { "host.sample_alpha_to_coverage", GL_SAMPLE_ALPHA_TO_COVERAGE },
        { "host.sample_alpha_to_one", GL_SAMPLE_ALPHA_TO_ONE },
        { "host.sample_coverage_enabled", GL_SAMPLE_COVERAGE },
        { "host.sample_mask_enabled", GL_SAMPLE_MASK },
        { "host.sample_coverage_invert", GL_SAMPLE_COVERAGE_INVERT },
        { "host.depth_func", GL_DEPTH_FUNC },
        { "host.depth_write", GL_DEPTH_WRITEMASK },
        { "host.stencil_enabled", GL_STENCIL_TEST },
        { "host.stencil_func", GL_STENCIL_FUNC },
        { "host.stencil_ref", GL_STENCIL_REF },
        { "host.stencil_read_mask", GL_STENCIL_VALUE_MASK },
        { "host.stencil_write_mask", GL_STENCIL_WRITEMASK },
        { "host.stencil_fail", GL_STENCIL_FAIL },
        { "host.stencil_depth_fail", GL_STENCIL_PASS_DEPTH_FAIL },
        { "host.stencil_pass", GL_STENCIL_PASS_DEPTH_PASS },
        { "host.stencil_back_func", GL_STENCIL_BACK_FUNC },
        { "host.stencil_back_ref", GL_STENCIL_BACK_REF },
        { "host.stencil_back_read_mask", GL_STENCIL_BACK_VALUE_MASK },
        { "host.stencil_back_write_mask", GL_STENCIL_BACK_WRITEMASK },
        { "host.stencil_back_fail", GL_STENCIL_BACK_FAIL },
        { "host.stencil_back_depth_fail", GL_STENCIL_BACK_PASS_DEPTH_FAIL },
        { "host.stencil_back_pass", GL_STENCIL_BACK_PASS_DEPTH_PASS },
        { "host.cull_enabled", GL_CULL_FACE },
        { "host.cull_face", GL_CULL_FACE_MODE },
        { "host.front_face", GL_FRONT_FACE },
        { "host.polygon_offset", GL_POLYGON_OFFSET_FILL },
    };
    for (size_t i = 0; i < G_N_ELEMENTS(states); ++i) {
        GLint value;
        glGetIntegerv(states[i].pname, &value);
        xemu_shader_draw_request_stage_register(token, states[i].name,
                                                (uint32_t)value);
    }
    GLfloat sample_coverage;
    glGetFloatv(GL_SAMPLE_COVERAGE_VALUE, &sample_coverage);
    XemuShaderDrawBlob coverage = { .name = "host.sample_coverage_value",
                                    .data = &sample_coverage,
                                    .byte_count = sizeof(sample_coverage) };
    xemu_shader_draw_request_stage_blob(token, &coverage);
    GLint sample_mask;
    glGetIntegeri_v(GL_SAMPLE_MASK_VALUE, 0, &sample_mask);
    xemu_shader_draw_request_stage_register(token, "host.sample_mask_value0",
                                            (uint32_t)sample_mask);
    GLfloat blend_color[4];
    glGetFloatv(GL_BLEND_COLOR, blend_color);
    XemuShaderDrawBlob color = { .name = "host.blend_color",
                                 .data = blend_color,
                                 .byte_count = sizeof(blend_color) };
    xemu_shader_draw_request_stage_blob(token, &color);
}

static inline void xemu_shader_draw_gl_stage_before(PGRAPHState *pg,
                                                    uint64_t token)
{
    if (!xemu_shader_draw_gl_wants_inputs(token))
        return;
    ++pg->shader_browser_input_snapshots;
    /* Reserve the after image and 256 KiB for owned geometry and metadata. */
    size_t bytes = xemu_shader_capture_session_snapshots(token) ?
                       xemu_shader_draw_gl_stage_target(pg, token, true) :
                       0;
    size_t budget = 64 * 1024 * 1024 - 256 * 1024 - 2 * bytes;
    xemu_shader_draw_gl_stage_registers(pg, token);
    shader_capture_gl_host_state(token);
    xemu_shader_draw_gl_stage_program(token, &budget);
    xemu_shader_draw_gl_stage_textures(pg, token, &budget);
}

static inline void xemu_shader_draw_gl_stage_after(PGRAPHState *pg,
                                                   uint64_t token)
{
    if (!xemu_shader_draw_gl_wants_inputs(token))
        return;
    if (xemu_shader_capture_session_snapshots(token))
        xemu_shader_draw_gl_stage_target(pg, token, false);
    xemu_shader_draw_request_inputs_complete(token);
}

#endif

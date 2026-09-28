/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_XBOX_NV2A_PGRAPH_GL_SHADER_BROWSER_CAPTURE_H
#define HW_XBOX_NV2A_PGRAPH_GL_SHADER_BROWSER_CAPTURE_H

#include "hw/xbox/nv2a/pgraph/shader-browser-geometry-copy.h"
#include <epoxy/gl.h>
#include <glib.h>

/* Read the host buffers bound to this command, after upload and conversion.
 * Capturing guest VRAM here would miss cached or privately owned generations.
 */
static inline uint8_t *
xemu_shader_draw_gl_read_buffer(GLuint buffer, size_t offset, size_t bytes)
{
    if (!buffer || !bytes || bytes > 4 * 1024 * 1024)
        return NULL;
    GLint previous;
    GLint64 size = 0;
    glGetIntegerv(GL_COPY_READ_BUFFER_BINDING, &previous);
    glBindBuffer(GL_COPY_READ_BUFFER, buffer);
    glGetBufferParameteri64v(GL_COPY_READ_BUFFER, GL_BUFFER_SIZE, &size);
    uint8_t *copy = NULL;
    if (size >= 0 && offset <= (uint64_t)size &&
        bytes <= (uint64_t)size - offset) {
        copy = g_try_new(uint8_t, bytes);
        if (copy)
            glGetBufferSubData(GL_COPY_READ_BUFFER, offset, bytes, copy);
    }
    glBindBuffer(GL_COPY_READ_BUFFER, previous);
    return copy;
}

/* Uses the final attribute binding, including expanded inline and converted
 * streams, without consulting guest inline-populated state. */
static inline bool
xemu_shader_draw_gl_stage_bound(uint64_t token,
                                const XemuShaderDrawLayout *layout)
{
    if (!token || !layout || !layout->vertex_count ||
        layout->vertex_count > 4096)
        return false;
    GLint enabled, type, components, stride, buffer;
    void *pointer;
    glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &enabled);
    glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_TYPE, &type);
    glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_SIZE, &components);
    glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &stride);
    glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &buffer);
    glGetVertexAttribPointerv(0, GL_VERTEX_ATTRIB_ARRAY_POINTER, &pointer);
    if (!enabled || type != GL_FLOAT || (components != 3 && components != 4)) {
        xemu_shader_draw_request_note_rejection(
            token, enabled ? XEMU_SHADER_CAPTURE_REJECT_VERTEX_FORMAT :
                             XEMU_SHADER_CAPTURE_REJECT_POSITION_UNAVAILABLE);
        return false;
    }
    if (!stride)
        stride = components * sizeof(float);
    size_t element = components * sizeof(float);
    if (stride < 0 || (size_t)stride < element ||
        layout->first_vertex > (SIZE_MAX - (uintptr_t)pointer) / stride ||
        layout->vertex_count - 1 > (SIZE_MAX - element) / stride)
        return false;
    size_t offset = (uintptr_t)pointer + (size_t)layout->first_vertex * stride;
    size_t bytes = (layout->vertex_count - 1) * (size_t)stride + element;
    g_autofree uint8_t *source =
        xemu_shader_draw_gl_read_buffer(buffer, offset, bytes);
    if (!source) {
        xemu_shader_draw_request_note_rejection(
            token, XEMU_SHADER_CAPTURE_REJECT_POSITION_UNAVAILABLE);
        return false;
    }
    return xemu_shader_draw_stage_source(
        token, layout, source, bytes, layout->first_vertex, stride, components);
}
#endif

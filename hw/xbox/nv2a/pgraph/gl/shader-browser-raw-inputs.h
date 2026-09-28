/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_XBOX_NV2A_PGRAPH_GL_SHADER_BROWSER_RAW_INPUTS_H
#define HW_XBOX_NV2A_PGRAPH_GL_SHADER_BROWSER_RAW_INPUTS_H

/* Raw host-bound evidence does not depend on the safe triangle decoder. */
static unsigned int shader_capture_gl_element_bytes(GLenum type,
                                                    GLint components)
{
    if (components == GL_BGRA)
        components = 4;
    if (components < 1 || components > 4)
        return 0;
    switch (type) {
    case GL_BYTE:
    case GL_UNSIGNED_BYTE:
        return components;
    case GL_SHORT:
    case GL_UNSIGNED_SHORT:
    case GL_HALF_FLOAT:
        return components * 2;
    case GL_INT:
    case GL_UNSIGNED_INT:
    case GL_FLOAT:
        return components * 4;
    case GL_DOUBLE:
        return components * 8;
    case GL_INT_2_10_10_10_REV:
    case GL_UNSIGNED_INT_2_10_10_10_REV:
        return 4;
    default:
        return 0;
    }
}

static void shader_capture_gl_resource_read(PGRAPHGLState *r, uint64_t token,
                                            GLuint buffer,
                                            const XemuShaderDrawBlob *blob)
{
    if (!xemu_shader_capture_session_token(token))
        return;
    uint64_t owner = 0, bytes = 0;
    if (buffer == r->gl_memory_buffer) {
        owner = r->capture_memory_owner;
        bytes = r->capture_memory_bytes;
    } else if (buffer == r->gl_inline_array_buffer) {
        owner = r->capture_inline_array_owner;
        bytes = r->capture_inline_array_bytes;
    } else if (buffer == r->capture_element_buffer) {
        owner = r->capture_element_owner;
        bytes = r->capture_element_bytes;
    } else {
        for (uint32_t i = 0; i < NV2A_VERTEXSHADER_ATTRIBUTES; ++i)
            if (buffer == r->gl_inline_buffer[i]) {
                owner = r->capture_inline_owner[i];
                bytes = r->capture_inline_bytes[i];
                break;
            }
    }
    XemuShaderCaptureResource resource = {
        .owner = owner,
        .byte_size = bytes,
        .offset = blob->offset,
        .size = blob->byte_count,
        .access = XEMU_SHADER_CAPTURE_RESOURCE_READ,
        .kind = XEMU_SHADER_CAPTURE_RESOURCE_BUFFER,
        .slot = blob->slot,
    };
    xemu_shader_capture_session_resource_snapshot(token, &resource, blob->name);
}

static void shader_capture_gl_raw_streams(PGRAPHState *pg, uint64_t token,
                                          bool indexed, const int32_t *starts,
                                          const int32_t *counts, size_t ranges)
{
    if (!token || !xemu_shader_draw_request_wants_inputs(token))
        return;
    PGRAPHGLState *r = pg->gl_renderer_state;
    uint32_t first = UINT32_MAX, last = 0;
    if (indexed) {
        GLint buffer;
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &buffer);
        size_t count = pg->inline_elements_length;
        g_autofree uint8_t *indices =
            xemu_shader_draw_gl_read_buffer(buffer, 0, count * 4);
        if (indices) {
            XemuShaderDrawBlob blob = { .name = "geometry.host_indices",
                                        .data = indices,
                                        .byte_count = count * 4,
                                        .format = GL_UNSIGNED_INT,
                                        .components = 1,
                                        .stride = 4,
                                        .count = count };
            xemu_shader_draw_request_stage_blob(token, &blob);
            shader_capture_gl_resource_read(r, token, buffer, &blob);
            for (size_t i = 0; i < count; ++i) {
                uint32_t index;
                memcpy(&index, indices + i * 4, 4);
                first = MIN(first, index);
                last = MAX(last, index);
            }
        }
    } else {
        for (size_t i = 0; i < ranges; ++i) {
            if (starts[i] < 0 || counts[i] <= 0 ||
                (uint32_t)counts[i] > UINT32_MAX - (uint32_t)starts[i])
                continue;
            first = MIN(first, (uint32_t)starts[i]);
            last = MAX(last, (uint32_t)starts[i] + (uint32_t)counts[i] - 1);
        }
        if (ranges && ranges <= 4096) {
            XemuShaderDrawBlob start_blob = { .name = "geometry.draw_starts",
                                              .data = starts,
                                              .byte_count =
                                                  ranges * sizeof(*starts),
                                              .count = ranges,
                                              .stride = 4 };
            XemuShaderDrawBlob count_blob = { .name = "geometry.draw_counts",
                                              .data = counts,
                                              .byte_count =
                                                  ranges * sizeof(*counts),
                                              .count = ranges,
                                              .stride = 4 };
            xemu_shader_draw_request_stage_blob(token, &start_blob);
            xemu_shader_draw_request_stage_blob(token, &count_blob);
        }
    }
    uint32_t unavailable = 0;
    for (unsigned int slot = 0; slot < NV2A_VERTEXSHADER_ATTRIBUTES; ++slot) {
        GLint enabled, type, components, stride, buffer, normalized, integer;
        glGetVertexAttribiv(slot, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &enabled);
        glGetVertexAttribiv(slot, GL_VERTEX_ATTRIB_ARRAY_TYPE, &type);
        glGetVertexAttribiv(slot, GL_VERTEX_ATTRIB_ARRAY_SIZE, &components);
        glGetVertexAttribiv(slot, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &stride);
        glGetVertexAttribiv(slot, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING,
                            &buffer);
        glGetVertexAttribiv(slot, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED,
                            &normalized);
        glGetVertexAttribiv(slot, GL_VERTEX_ATTRIB_ARRAY_INTEGER, &integer);
        char name[64];
        snprintf(name, sizeof(name), "vertex.enabled%u", slot);
        xemu_shader_draw_request_stage_register(token, name, enabled);
        snprintf(name, sizeof(name), "vertex.attribute%u", slot);
        if (!enabled) {
            GLfloat current[4];
            glGetVertexAttribfv(slot, GL_CURRENT_VERTEX_ATTRIB, current);
            XemuShaderDrawBlob blob = { .name = name,
                                        .data = current,
                                        .byte_count = sizeof(current),
                                        .slot = slot,
                                        .format = GL_FLOAT,
                                        .components = 4,
                                        .stride = sizeof(current),
                                        .count = 1 };
            xemu_shader_draw_request_stage_blob(token, &blob);
            continue;
        }
        void *pointer;
        glGetVertexAttribPointerv(slot, GL_VERTEX_ATTRIB_ARRAY_POINTER,
                                  &pointer);
        unsigned int element =
            shader_capture_gl_element_bytes(type, components);
        size_t step = stride > 0 ? (size_t)stride : element;
        if (!element || !step || first == UINT32_MAX || !buffer ||
            last < first || first > (SIZE_MAX - (uintptr_t)pointer) / step ||
            last - first > (SIZE_MAX - element) / step) {
            unavailable |= 1U << slot;
            continue;
        }
        size_t offset = (uintptr_t)pointer + (size_t)first * step;
        size_t bytes = (size_t)(last - first) * step + element;
        g_autofree uint8_t *source =
            xemu_shader_draw_gl_read_buffer(buffer, offset, bytes);
        if (!source) {
            unavailable |= 1U << slot;
            continue;
        }
        XemuShaderDrawBlob blob = { .name = name,
                                    .data = source,
                                    .byte_count = bytes,
                                    .slot = slot,
                                    .format = type,
                                    .components = components,
                                    .stride = step,
                                    .count = last - first + 1,
                                    .offset = offset,
                                    .normalized = normalized,
                                    .integer = integer };
        if (!xemu_shader_draw_request_stage_blob(token, &blob))
            unavailable |= 1U << slot;
        shader_capture_gl_resource_read(r, token, buffer, &blob);
    }
    xemu_shader_draw_request_stage_register(
        token, "capture.vertex_streams_missing", unavailable);
    xemu_shader_draw_request_stage_register(token, "capture.first_vertex",
                                            first);
    xemu_shader_draw_request_stage_register(token, "capture.last_vertex", last);
}
#endif

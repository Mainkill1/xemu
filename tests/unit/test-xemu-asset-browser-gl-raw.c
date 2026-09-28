/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <SDL3/SDL.h>
#include <epoxy/gl.h>
#include <glib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ui/xui/shader-browser-draw-inputs.h"

/* Only resource ownership bookkeeping is doubled. The production raw-stream
 * adapter below reads real native GL attribute bindings and buffer bytes. */
#define NV2A_VERTEXSHADER_ATTRIBUTES 16
typedef struct {
    GLuint gl_memory_buffer, gl_inline_array_buffer, capture_element_buffer;
    GLuint gl_inline_buffer[NV2A_VERTEXSHADER_ATTRIBUTES];
    uint64_t capture_memory_owner, capture_memory_bytes;
    uint64_t capture_inline_array_owner, capture_inline_array_bytes;
    uint64_t capture_element_owner, capture_element_bytes;
    uint64_t capture_inline_owner[NV2A_VERTEXSHADER_ATTRIBUTES];
    uint64_t capture_inline_bytes[NV2A_VERTEXSHADER_ATTRIBUTES];
} PGRAPHGLState;
typedef struct {
    PGRAPHGLState *gl_renderer_state;
    size_t inline_elements_length;
    uint32_t compressed_attrs;
} PGRAPHState;
static uint32_t captured_mask;
static bool saw_mask, saw_packed, saw_current;
int xemu_shader_draw_request_wants_inputs(uint64_t token)
{
    return token == 1;
}
int xemu_shader_capture_session_token(uint64_t token)
{
    (void)token;
    return 0;
}
int xemu_shader_draw_request_stage_register(uint64_t token, const char *name,
                                            uint32_t value)
{
    g_assert_cmpuint(token, ==, 1);
    if (!strcmp(name, "capture.vertices.compressed_mask")) {
        saw_mask = true;
        captured_mask = value;
    }
    return 1;
}
int xemu_shader_draw_request_stage_blob(uint64_t token,
                                        const XemuShaderDrawBlob *blob)
{
    g_assert_cmpuint(token, ==, 1);
    if (!strcmp(blob->name, "vertex.attribute0")) {
        g_assert_cmpuint(blob->format, ==, GL_INT);
        g_assert_cmpuint(blob->integer, ==, 1);
        g_assert_cmpuint(blob->count, ==, 3);
        g_assert_cmpuint(blob->byte_count, ==, 12);
        uint32_t last;
        memcpy(&last, (const uint8_t *)blob->data + 8, 4);
        g_assert_cmpuint(last, ==, 1023U << 11);
        saw_packed = true;
    } else if (!strcmp(blob->name, "vertex.attribute2")) {
        g_assert_cmpuint(blob->format, ==, GL_FLOAT);
        g_assert_cmpuint(blob->count, ==, 1);
        g_assert_cmpfloat(((const float *)blob->data)[2], ==, 1);
        saw_current = true;
    }
    return 1;
}
int xemu_shader_capture_session_resource_snapshot(
    uint64_t token, const XemuShaderCaptureResource *resource, const char *name)
{
    (void)token;
    (void)resource;
    (void)name;
    g_assert_not_reached();
    return 0;
}
static uint8_t *xemu_shader_draw_gl_read_buffer(GLuint buffer, size_t offset,
                                                size_t size)
{
    GLint previous;
    glGetIntegerv(GL_COPY_READ_BUFFER_BINDING, &previous);
    glBindBuffer(GL_COPY_READ_BUFFER, buffer);
    uint8_t *bytes = g_malloc(size);
    glGetBufferSubData(GL_COPY_READ_BUFFER, offset, size, bytes);
    glBindBuffer(GL_COPY_READ_BUFFER, previous);
    return bytes;
}
#include "hw/xbox/nv2a/pgraph/gl/shader-browser-raw-inputs.h"

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_assert_true(SDL_Init(SDL_INIT_VIDEO));
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window *window = SDL_CreateWindow(
        "Packed capture", 128, 128, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    SDL_GLContext context = SDL_GL_CreateContext(window);
    g_assert_nonnull(context);
    GLuint vao, buffer;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    const uint32_t packed[] = { 0, 1023, 1023U << 11 };
    glBufferData(GL_ARRAY_BUFFER, sizeof(packed), packed, GL_STATIC_DRAW);
    glVertexAttribIPointer(0, 1, GL_INT, 4, NULL);
    glEnableVertexAttribArray(0);
    glDisableVertexAttribArray(2);
    glVertexAttrib4f(2, 0, 0, 1, 1);
    PGRAPHGLState renderer = { 0 };
    PGRAPHState pg = { .gl_renderer_state = &renderer, .compressed_attrs = 5 };
    const int32_t first = 0, count = 3;
    shader_capture_gl_raw_streams(&pg, 1, false, &first, &count, 1);
    g_assert_true(saw_packed && saw_current);
    g_assert_true(saw_mask);
    g_assert_cmpuint(captured_mask, ==, 5);
    g_assert_cmpuint(glGetError(), ==, GL_NO_ERROR);
    glDeleteBuffers(1, &buffer);
    glDeleteVertexArrays(1, &vao);
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    g_print("Native GL packed/current stream capture passed\n");
    return 0;
}

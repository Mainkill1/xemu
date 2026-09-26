/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hw/xbox/nv2a/pgraph/glsl/shaders.h"
#include "ui/xui/shader-browser-capture-bridge.h"

#include <assert.h>
#include <string.h>

void xemu_shader_capture_test_make_state(void *bytes, size_t size, int cube)
{
    assert(size == sizeof(ShaderState));
    ShaderState state = { 0 };
    state.psh.dim_tex[0] = 2;
    state.psh.tex_cubemap[0] = cube;
    memcpy(bytes, &state, sizeof(state));
}

void xemu_shader_capture_test_make_pixel_uniforms(void *bytes, size_t size)
{
    assert(size == sizeof(PshUniformValues));
    PshUniformValues values = { 0 };
    values.texScale[0] = 1.0f;
    memcpy(bytes, &values, sizeof(values));
}

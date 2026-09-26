/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "shaders.h"
#include "ui/xui/shader-browser-capture-bridge.h"

#include <string.h>

XemuShaderCaptureAbiSizes xemu_shader_capture_abi_sizes(void)
{
    return (XemuShaderCaptureAbiSizes) {
        sizeof(ShaderState), sizeof(VshUniformValues),
        sizeof(PshUniformValues)
    };
}

int xemu_shader_capture_ordinary_2d_sampler(const void *shader_state,
                                             size_t size, uint32_t stage)
{
    if (!shader_state || size != sizeof(ShaderState) || stage >= 4) return 0;
    ShaderState state;
    memcpy(&state, shader_state, sizeof(state));
    const PshState *psh = &state.psh;
    if (psh->dim_tex[stage] != 2 || psh->tex_cubemap[stage] ||
        psh->shadow_map[stage] || psh->rect_tex[stage] ||
        psh->tex_x8y24[stage]) return 0;
    for (unsigned component = 0; component < 4; ++component) {
        if (psh->compare_mode[stage][component]) return 0;
    }
    return 1;
}

int xemu_shader_capture_pixel_scale_is(const void *pixel_uniforms,
                                        size_t size, uint32_t stage,
                                        float expected)
{
    if (!pixel_uniforms || size != sizeof(PshUniformValues) || stage >= 4)
        return 0;
    PshUniformValues values;
    memcpy(&values, pixel_uniforms, sizeof(values));
    return values.texScale[stage] == expected;
}

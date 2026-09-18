/*
 * NV2A Vulkan texture-derived uniform helpers
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_TEXTURE_UNIFORM_H
#define HW_XBOX_NV2A_PGRAPH_VK_TEXTURE_UNIFORM_H

#include "hw/xbox/nv2a/nv2a_regs.h"
#include "glsl.h"
#include "texture-binding-state.h"

typedef struct PGRAPHVkTextureScaleInput {
    float scale;
    bool linear;
} PGRAPHVkTextureScaleInput;

static inline bool pgraph_vk_texture_scale_uniform_update(
    ShaderUniformLayout *layout, int loc,
    const PGRAPHVkTextureScaleInput inputs[NV2A_MAX_TEXTURES])
{
    if (loc == -1) {
        return false;
    }

    float scales[NV2A_MAX_TEXTURES];
    for (unsigned int i = 0; i < NV2A_MAX_TEXTURES; i++) {
        scales[i] = inputs[i].scale == 1.0f ?
            1.0f :
            pgraph_vk_texture_effective_scale(inputs[i].scale,
                                              inputs[i].linear);
    }

    return uniform_copy(layout, loc, scales, sizeof(scales[0]),
                        NV2A_MAX_TEXTURES);
}

#endif

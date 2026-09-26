/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_XBOX_NV2A_PGRAPH_GLSL_SHADER_BROWSER_PUBLICATION_H
#define HW_XBOX_NV2A_PGRAPH_GLSL_SHADER_BROWSER_PUBLICATION_H

#include "shader-browser-recipe.h"

typedef struct PGRAPHShaderBrowserIdentity {
    uint8_t hash[XEMU_SHADER_BROWSER_HASH_BYTES];
    uint32_t stage;
} PGRAPHShaderBrowserIdentity;

typedef struct PGRAPHShaderBrowserBinding {
    uint64_t scope_generation;
    uint32_t count;
    PGRAPHShaderBrowserIdentity identities[3];
} PGRAPHShaderBrowserBinding;

void pgraph_shader_browser_publish_binding(const ShaderState *state,
                                           bool geometry_needed,
                                           PGRAPHShaderBrowserBinding *binding);
void pgraph_shader_browser_refresh_binding_scope(
    const ShaderState *state, bool geometry_needed,
    PGRAPHShaderBrowserBinding *binding);

#endif

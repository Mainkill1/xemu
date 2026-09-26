/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_XBOX_NV2A_PGRAPH_GLSL_SHADER_BROWSER_PUBLICATION_H
#define HW_XBOX_NV2A_PGRAPH_GLSL_SHADER_BROWSER_PUBLICATION_H

#include "shader-browser-recipe.h"
#include "ui/xui/shader-browser-override-runtime.h"

typedef struct PGRAPHShaderBrowserIdentity {
    uint8_t hash[XEMU_SHADER_BROWSER_HASH_BYTES];
    uint32_t stage;
} PGRAPHShaderBrowserIdentity;

typedef struct PGRAPHShaderBrowserBinding {
    uint64_t scope_generation;
    uint64_t override_generation;
    uint64_t compile_cpu_ns;
    uint64_t prepare_cpu_ns;
    uint32_t count;
    bool timings_pending;
    bool pixel_only;
    PGRAPHShaderBrowserIdentity identities[3];
    XemuShaderOverridePolicy opengl_policy;
    XemuShaderOverridePolicy vulkan_policy;
} PGRAPHShaderBrowserBinding;

void pgraph_shader_browser_publish_binding(const ShaderState *state,
                                           bool geometry_needed,
                                           PGRAPHShaderBrowserBinding *binding);
void pgraph_shader_browser_publish_pixel_binding(
    const ShaderState *state, PGRAPHShaderBrowserBinding *binding);
void pgraph_shader_browser_refresh_binding_scope(
    const ShaderState *state, bool geometry_needed,
    PGRAPHShaderBrowserBinding *binding);
void pgraph_shader_browser_publish_override_effect(
    const PGRAPHShaderBrowserBinding *binding, uint32_t backend,
    const XemuShaderOverrideEffect *effect);
void pgraph_shader_browser_publish_generated_artifact(
    const ShaderState *state, uint32_t stage, const char *backend,
    const char *route, const char *kind, const char *extension,
    const uint8_t *data, size_t size);

/* Called only after an actual inline draw command. The cheap disarmed gate is
 * inside the helper; all copied inputs are CPU-visible at this boundary. */
void pgraph_shader_browser_capture_inline(
    PGRAPHState *pg, const ShaderState *state,
    const PGRAPHShaderBrowserBinding *binding, uint32_t backend,
    uint32_t primitive, uint16_t attribute_mask, uint32_t width,
    uint32_t height, const char *vertex_source,
    const char *geometry_source, const char *pixel_source,
    uint32_t route);

#endif

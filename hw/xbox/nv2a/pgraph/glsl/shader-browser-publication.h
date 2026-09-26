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
    uint64_t compile_cpu_ns;
    uint64_t prepare_cpu_ns;
    uint64_t link_cpu_ns;
    uint32_t count;
    bool timing_pending;
    PGRAPHShaderBrowserIdentity identities[3];
} PGRAPHShaderBrowserBinding;

void pgraph_shader_browser_publish_binding(const ShaderState *state,
                                           bool geometry_needed,
                                           PGRAPHShaderBrowserBinding *binding);
void pgraph_shader_browser_refresh_binding_scope(
    const ShaderState *state, bool geometry_needed,
    PGRAPHShaderBrowserBinding *binding);
void pgraph_shader_browser_publish_generated_artifact(
    const ShaderState *state, uint32_t stage, const char *backend,
    const char *route, const char *kind, const char *extension,
    const uint8_t *data, size_t size);
void pgraph_shader_browser_publish_stage_timing(
    const ShaderState *state, uint32_t stage, uint32_t backend,
    uint32_t route, uint32_t metric, uint64_t duration_ns, uint32_t flags);
void pgraph_shader_browser_publish_binding_timing(
    const PGRAPHShaderBrowserBinding *binding, uint32_t backend,
    uint32_t route, uint64_t variant_id, uint64_t frame, uint32_t metric,
    uint64_t duration_ns, uint64_t represented_draws, uint32_t flags);

#endif

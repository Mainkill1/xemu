/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "shader-browser-publication.h"
#include "hw/xbox/nv2a/pgraph/pgraph.h"
#include "hw/xbox/nv2a/nv2a_regs.h"
#include "hw/xbox/nv2a/debug.h"
#include "ui/xui/shader-browser-capture-bridge.h"

#include <string.h>
#include <glib.h>

void pgraph_shader_browser_capture_inline(
    PGRAPHState *pg, const ShaderState *state,
    const PGRAPHShaderBrowserBinding *binding, uint32_t backend,
    uint32_t primitive, uint16_t attribute_mask, uint32_t width,
    uint32_t height, const char *vertex_source,
    const char *geometry_source, const char *pixel_source,
    uint32_t route, uint64_t sampled_nonce)
{
    if (!sampled_nonce || sampled_nonce != xemu_shader_capture_nonce() ||
        !pg || !state || !binding) {
        return;
    }
    XemuShaderCaptureRequest request;
    if (!xemu_shader_capture_copy_request(&request)) return;
    if (request.request_nonce != sampled_nonce) return;
    if (request.backend != backend ||
        request.renderer_epoch != nv2a_profile_preview_renderer_epoch() ||
        request.session_epoch != xemu_shader_browser_live_epoch() ||
        request.scope_generation != binding->scope_generation ||
        request.scope_generation != xemu_shader_browser_scope_generation())
        return;
    bool selected_present = false;
    for (uint32_t i = 0; i < binding->count && i < 3; ++i) {
        if (binding->identities[i].stage == request.selected.stage &&
            memcmp(binding->identities[i].hash, request.selected.hash,
                   sizeof(request.selected.hash)) == 0) {
            selected_present = true;
            break;
        }
    }
    if (!selected_present) return;
    XemuShaderCaptureDraw draw = { 0 };
    draw.capture_started_ns = (uint64_t)g_get_monotonic_time() * 1000;
    draw.context = request;
    draw.context.scope_generation = xemu_shader_browser_scope_generation();
    draw.context.backend = backend;
    draw.context.renderer_epoch = nv2a_profile_preview_renderer_epoch();
    draw.context.session_epoch = xemu_shader_browser_live_epoch();
    draw.sampled_nonce = sampled_nonce;
    draw.stage_count = binding->count;
    for (uint32_t i = 0; i < binding->count && i < 3; ++i) {
        draw.stages[i].stage = binding->identities[i].stage;
        memcpy(draw.stages[i].hash, binding->identities[i].hash,
               sizeof(draw.stages[i].hash));
    }
    draw.primitive = primitive;
    draw.vertex_count = pg->inline_buffer_length;
    draw.attribute_mask = attribute_mask;
    for (int i = 0; i < NV2A_VERTEXSHADER_ATTRIBUTES; ++i) {
        const VertexAttribute *attr = &pg->vertex_attributes[i];
        draw.attributes[i] = attr->inline_buffer;
        memcpy(draw.constant_attributes[i], attr->inline_value,
               sizeof(draw.constant_attributes[i]));
    }
    draw.shader_state = state;
    draw.shader_state_size = sizeof(*state);
    VshUniformLocs vlocs;
    PshUniformLocs plocs;
    memset(vlocs, 0, sizeof(vlocs));
    memset(plocs, 0, sizeof(plocs));
    VshUniformValues vvalues = { 0 };
    PshUniformValues pvalues = { 0 };
    pgraph_glsl_set_vsh_uniform_values(pg, &state->vsh, vlocs, &vvalues);
    pgraph_glsl_set_psh_uniform_values(pg, plocs, &pvalues);
    for (int i = 0; i < 4; ++i) pvalues.texScale[i] = 1.0f;
    draw.vertex_uniforms = &vvalues;
    draw.vertex_uniforms_size = sizeof(vvalues);
    draw.pixel_uniforms = &pvalues;
    draw.pixel_uniforms_size = sizeof(pvalues);
    draw.vertex_source = vertex_source;
    draw.geometry_source = geometry_source;
    draw.pixel_source = pixel_source;
    draw.control_0 = pgraph_reg_r(pg, NV_PGRAPH_CONTROL_0);
    draw.control_1 = pgraph_reg_r(pg, NV_PGRAPH_CONTROL_1);
    draw.blend = pgraph_reg_r(pg, NV_PGRAPH_BLEND);
    draw.blend_color = pgraph_reg_r(pg, NV_PGRAPH_BLENDCOLOR);
    draw.setup_raster = pgraph_reg_r(pg, NV_PGRAPH_SETUPRASTER);
    draw.width = width;
    draw.height = height;
    draw.color_format = pg->surface_shape.color_format;
    draw.viewport_width = pg->surface_binding_dim.width;
    draw.viewport_height = pg->surface_binding_dim.height;
    pgraph_apply_scaling_factor(pg, &draw.viewport_width,
                                &draw.viewport_height);
    draw.scissor_x = pg->surface_shape.clip_x;
    draw.scissor_y = pg->surface_shape.clip_y;
    draw.scissor_width = pg->surface_shape.clip_width;
    draw.scissor_height = pg->surface_shape.clip_height;
    pgraph_apply_anti_aliasing_factor(pg, &draw.scissor_x, &draw.scissor_y);
    pgraph_apply_anti_aliasing_factor(pg, &draw.scissor_width,
                                     &draw.scissor_height);
    pgraph_apply_scaling_factor(pg, &draw.scissor_x, &draw.scissor_y);
    pgraph_apply_scaling_factor(pg, &draw.scissor_width,
                                &draw.scissor_height);
    draw.route = route;
    for (unsigned i = 0; i < 4; ++i) {
        if ((state->psh.shader_stage_program >> (i * 5)) & 0x1f)
            draw.texture_mask |= 1U << i;
    }
    xemu_shader_capture_submitted(&draw,
        (uint64_t)g_get_monotonic_time() * 1000);
}

char *xemu_shader_capture_generate_gl_source(
    const void *shader_state, size_t shader_state_size, uint32_t stage)
{
    if (shader_state_size != sizeof(ShaderState)) return NULL;
    const ShaderState *state = shader_state;
    if (!state) return NULL;
    MString *source = NULL;
    if (stage == XEMU_SHADER_BROWSER_STAGE_VERTEX ||
        stage == XEMU_SHADER_BROWSER_STAGE_FIXED_FUNCTION) {
        GenVshGlslOptions options = { 0 };
        options.prefix_outputs = pgraph_glsl_need_geom(&state->geom);
        source = pgraph_glsl_gen_vsh(&state->vsh, options);
    } else if (stage == XEMU_SHADER_BROWSER_STAGE_PIXEL) {
        GenPshGlslOptions options = { 0 };
        source = pgraph_glsl_gen_psh(&state->psh, options);
    } else if (stage == XEMU_SHADER_BROWSER_STAGE_GEOMETRY) {
        GenGeomGlslOptions options = { 0 };
        source = pgraph_glsl_gen_geom(&state->geom, options);
    }
    if (!source) return NULL;
    char *owned = g_strdup(mstring_get_str(source));
    mstring_unref(source);
    return owned;
}

static void refresh_override_policies(
    const XemuShaderBrowserScope *scope, PGRAPHShaderBrowserBinding *binding)
{
    memset(&binding->opengl_policy, 0, sizeof(binding->opengl_policy));
    memset(&binding->vulkan_policy, 0, sizeof(binding->vulkan_policy));
    binding->override_generation = xemu_shader_override_generation();
    if (!scope || !scope->title_id) {
        return;
    }

    const PGRAPHShaderBrowserIdentity *pixel = NULL;
    for (uint32_t i = 0; i < binding->count; ++i) {
        if (binding->identities[i].stage ==
            XEMU_SHADER_BROWSER_STAGE_PIXEL) {
            pixel = &binding->identities[i];
            break;
        }
    }
    if (!pixel) {
        return;
    }

    xemu_shader_override_resolve_scoped(
        scope->title_id, scope->executable_fingerprint_version,
        scope->executable_fingerprint, XEMU_SHADER_OVERRIDE_BACKEND_OPENGL,
        1, pixel->hash, pixel->stage, &binding->opengl_policy);
    xemu_shader_override_resolve_scoped(
        scope->title_id, scope->executable_fingerprint_version,
        scope->executable_fingerprint, XEMU_SHADER_OVERRIDE_BACKEND_VULKAN,
        1, pixel->hash, pixel->stage, &binding->vulkan_policy);
}

static void publish_binding(const ShaderState *state, bool geometry_needed,
                            bool pixel_only,
                            PGRAPHShaderBrowserBinding *binding)
{
    if (!state || !binding) {
        return;
    }
    XemuShaderBrowserScope scope = { 0 };
    uint64_t generation = xemu_shader_browser_copy_current_scope(&scope);
    uint32_t stages[3] = {
        state->vsh.is_fixed_function ?
            XEMU_SHADER_BROWSER_STAGE_FIXED_FUNCTION :
            XEMU_SHADER_BROWSER_STAGE_VERTEX,
        XEMU_SHADER_BROWSER_STAGE_PIXEL,
        XEMU_SHADER_BROWSER_STAGE_GEOMETRY,
    };
    uint32_t stage_count = geometry_needed ? 3 : 2;
    binding->count = 0;
    binding->pixel_only = pixel_only;
    binding->scope_generation = generation;
    for (uint32_t i = pixel_only ? 1 : 0;
         i < (pixel_only ? 2 : stage_count); ++i) {
        uint8_t recipe[PGRAPH_SHADER_BROWSER_RECIPE_MAX];
        size_t recipe_size = 0;
        uint8_t hash[XEMU_SHADER_BROWSER_HASH_BYTES];
        if (!pgraph_shader_browser_encode_recipe(
                state, stages[i], recipe, sizeof(recipe), &recipe_size) ||
            !xemu_shader_browser_compute_shader_hash(
                1, stages[i], PGRAPH_SHADER_BROWSER_RECIPE_VERSION, recipe,
                recipe_size, hash)) {
            continue;
        }
        XemuShaderBrowserShaderRecord record = { 0 };
        record.identity_version = 1;
        memcpy(record.identity_hash, hash, sizeof(hash));
        record.stage = stages[i];
        record.recipe_format_version = PGRAPH_SHADER_BROWSER_RECIPE_VERSION;
        record.recipe_data = recipe;
        record.recipe_size = recipe_size;
        record.scope = scope;
        xemu_shader_browser_publish_shader(&record);

        PGRAPHShaderBrowserIdentity *identity =
            &binding->identities[binding->count++];
        identity->stage = stages[i];
        memcpy(identity->hash, hash, sizeof(hash));
    }
    refresh_override_policies(&scope, binding);
}

void pgraph_shader_browser_publish_binding(const ShaderState *state,
                                           bool geometry_needed,
                                           PGRAPHShaderBrowserBinding *binding)
{
    publish_binding(state, geometry_needed, false, binding);
}

void pgraph_shader_browser_publish_pixel_binding(
    const ShaderState *state, PGRAPHShaderBrowserBinding *binding)
{
    publish_binding(state, false, true, binding);
}

void pgraph_shader_browser_refresh_binding_scope(
    const ShaderState *state, bool geometry_needed,
    PGRAPHShaderBrowserBinding *binding)
{
    if (!binding) {
        return;
    }
    if (binding->scope_generation != xemu_shader_browser_scope_generation()) {
        if (binding->pixel_only) {
            pgraph_shader_browser_publish_pixel_binding(state, binding);
        } else {
            pgraph_shader_browser_publish_binding(state, geometry_needed,
                                                  binding);
        }
        return;
    }
    if (binding->override_generation != xemu_shader_override_generation()) {
        XemuShaderBrowserScope scope = { 0 };
        xemu_shader_browser_copy_current_scope(&scope);
        refresh_override_policies(&scope, binding);
    }
}

void pgraph_shader_browser_publish_override_effect(
    const PGRAPHShaderBrowserBinding *binding, uint32_t backend,
    const XemuShaderOverrideEffect *effect)
{
    if (!binding || !effect || !effect->rule_id) {
        return;
    }
    XemuShaderBrowserScope scope = { 0 };
    xemu_shader_browser_copy_current_scope(&scope);
    if (!scope.title_id) {
        return;
    }
    for (uint32_t i = 0; i < binding->count; ++i) {
        const PGRAPHShaderBrowserIdentity *identity = &binding->identities[i];
        if (identity->stage != XEMU_SHADER_BROWSER_STAGE_PIXEL) {
            continue;
        }
        xemu_shader_override_publish_effect(
            scope.title_id, scope.executable_fingerprint_version,
            scope.executable_fingerprint, backend, 1, identity->hash,
            identity->stage, effect);
        return;
    }
}

void pgraph_shader_browser_publish_generated_artifact(
    const ShaderState *state, uint32_t stage, const char *backend,
    const char *route, const char *kind, const char *extension,
    const uint8_t *data, size_t size)
{
    if (!state || !data || !size ||
        !xemu_shader_browser_external_artifacts_enabled()) {
        return;
    }
    XemuShaderBrowserScope scope = { 0 };
    xemu_shader_browser_copy_current_scope(&scope);
    if (!scope.title_id) {
        return;
    }
    uint8_t recipe[PGRAPH_SHADER_BROWSER_RECIPE_MAX];
    size_t recipe_size = 0;
    uint8_t hash[XEMU_SHADER_BROWSER_HASH_BYTES];
    if (!pgraph_shader_browser_encode_recipe(state, stage, recipe,
                                             sizeof(recipe), &recipe_size) ||
        !xemu_shader_browser_compute_shader_hash(
            1, stage, PGRAPH_SHADER_BROWSER_RECIPE_VERSION, recipe, recipe_size,
            hash)) {
        return;
    }
    XemuShaderBrowserShaderRecord record = { 0 };
    record.identity_version = 1;
    memcpy(record.identity_hash, hash, sizeof(hash));
    record.stage = stage;
    record.recipe_format_version = PGRAPH_SHADER_BROWSER_RECIPE_VERSION;
    record.recipe_data = recipe;
    record.recipe_size = recipe_size;
    record.scope = scope;
    if (!xemu_shader_browser_publish_shader(&record)) {
        return;
    }
    char *content_hash =
        g_compute_checksum_for_data(G_CHECKSUM_SHA256, data, size);
    XemuShaderBrowserExternalArtifact artifact = { 0 };
    artifact.identity_version = 1;
    memcpy(artifact.identity_hash, hash, sizeof(hash));
    artifact.stage = stage;
    artifact.title_id = scope.title_id;
    artifact.backend = backend;
    artifact.route = route;
    artifact.kind = kind;
    artifact.extension = extension;
    artifact.generator_abi = 1;
    artifact.content_hash = content_hash;
    artifact.data = data;
    artifact.size = size;
    xemu_shader_browser_publish_external_artifact(&artifact);
    g_free(content_hash);
}

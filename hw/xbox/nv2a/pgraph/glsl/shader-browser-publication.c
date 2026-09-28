/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "shader-browser-publication.h"

#include <string.h>
#include <glib.h>

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

void pgraph_shader_browser_capture_binding(const ShaderState *state,
                                           bool geometry_needed,
                                           PGRAPHShaderBrowserBinding *binding)
{
    if (!state || !binding)
        return;
    uint32_t stages[3] = {
        state->vsh.is_fixed_function ?
            XEMU_SHADER_BROWSER_STAGE_FIXED_FUNCTION :
            XEMU_SHADER_BROWSER_STAGE_VERTEX,
        XEMU_SHADER_BROWSER_STAGE_PIXEL,
        XEMU_SHADER_BROWSER_STAGE_GEOMETRY,
    };
    binding->count = 0;
    binding->scope_generation = xemu_shader_browser_scope_generation();
    for (uint32_t i = 0; i < (geometry_needed ? 3U : 2U); ++i) {
        uint8_t recipe[PGRAPH_SHADER_BROWSER_RECIPE_MAX];
        size_t size = 0;
        PGRAPHShaderBrowserIdentity *identity =
            &binding->identities[binding->count];
        if (!pgraph_shader_browser_encode_recipe(state, stages[i], recipe,
                                                 sizeof(recipe), &size) ||
            !xemu_shader_browser_compute_shader_hash(
                1, stages[i], PGRAPH_SHADER_BROWSER_RECIPE_VERSION, recipe,
                size, identity->hash))
            continue;
        identity->stage = stages[i];
        ++binding->count;
    }
}

void pgraph_shader_browser_publish_stage_timing(
    const ShaderState *state, uint32_t stage, uint32_t backend, uint32_t route,
    uint32_t metric, uint64_t duration_ns, uint32_t flags)
{
    pgraph_shader_browser_publish_stage_timing_at_scope(
        state, stage, backend, route, metric, duration_ns, flags,
        xemu_shader_browser_scope_generation());
}

void pgraph_shader_browser_publish_stage_timing_at_scope(
    const ShaderState *state, uint32_t stage, uint32_t backend, uint32_t route,
    uint32_t metric, uint64_t duration_ns, uint32_t flags,
    uint64_t scope_generation)
{
    XemuShaderBrowserPerformanceContext context = { 0 };
    if (!pgraph_shader_browser_capture_performance_context(&context))
        return;
    context.scope_generation = scope_generation;
    pgraph_shader_browser_publish_stage_timing_at_context(
        state, stage, backend, route, metric, duration_ns, flags, &context);
}

int pgraph_shader_browser_capture_performance_context(
    XemuShaderBrowserPerformanceContext *context)
{
    return xemu_shader_browser_capture_performance_context(context);
}

int pgraph_shader_browser_capture_binding_timing_context(
    PGRAPHShaderBrowserBinding *binding)
{
    if (!binding)
        return 0;
    return pgraph_shader_browser_capture_performance_context(
        &binding->timing_context);
}

void pgraph_shader_browser_publish_captured_binding_timing(
    const PGRAPHShaderBrowserBinding *binding, uint32_t backend, uint32_t route,
    uint32_t variant_kind, uint64_t variant_id, uint64_t frame, uint32_t metric,
    uint64_t duration_ns, uint64_t represented_draws, uint32_t flags)
{
    if (!binding)
        return;
    pgraph_shader_browser_publish_binding_timing_at_context(
        binding, backend, route, variant_kind, variant_id, frame, metric,
        duration_ns, represented_draws, flags, &binding->timing_context);
}

void pgraph_shader_browser_publish_stage_timing_at_context(
    const ShaderState *state, uint32_t stage, uint32_t backend, uint32_t route,
    uint32_t metric, uint64_t duration_ns, uint32_t flags,
    const XemuShaderBrowserPerformanceContext *context)
{
    if (!state || !duration_ns || !context || !context->destinations)
        return;
    uint8_t recipe[PGRAPH_SHADER_BROWSER_RECIPE_MAX];
    size_t recipe_size = 0;
    XemuShaderBrowserPerformanceSample sample = { 0 };
    if (!pgraph_shader_browser_encode_recipe(state, stage, recipe,
                                             sizeof(recipe), &recipe_size) ||
        !xemu_shader_browser_compute_shader_hash(
            1, stage, PGRAPH_SHADER_BROWSER_RECIPE_VERSION, recipe, recipe_size,
            sample.identities[0].hash))
        return;
    sample.owner = XEMU_SHADER_BROWSER_PERF_STAGE;
    sample.metric = metric;
    sample.backend = backend;
    sample.route = route;
    sample.context = *context;
    sample.duration_ns = duration_ns;
    sample.identity_count = 1;
    sample.identities[0].version = 1;
    sample.identities[0].stage = stage;
    sample.flags = flags;
    xemu_shader_browser_publish_performance_samples(&sample, 1);
}

void pgraph_shader_browser_publish_binding_timing(
    const PGRAPHShaderBrowserBinding *binding, uint32_t backend, uint32_t route,
    uint64_t variant_id, uint64_t frame, uint32_t metric, uint64_t duration_ns,
    uint64_t represented_draws, uint32_t flags)
{
    XemuShaderBrowserPerformanceContext context = { 0 };
    if (!pgraph_shader_browser_capture_performance_context(&context))
        return;
    pgraph_shader_browser_publish_binding_timing_at_context(
        binding, backend, route, XEMU_SHADER_BROWSER_VARIANT_BINDING,
        variant_id, frame, metric, duration_ns, represented_draws, flags,
        &context);
}

void pgraph_shader_browser_publish_binding_timing_at_context(
    const PGRAPHShaderBrowserBinding *binding, uint32_t backend, uint32_t route,
    uint32_t variant_kind, uint64_t variant_id, uint64_t frame, uint32_t metric,
    uint64_t duration_ns, uint64_t represented_draws, uint32_t flags,
    const XemuShaderBrowserPerformanceContext *context)
{
    if (!binding || !binding->count || !duration_ns || !context ||
        !context->destinations ||
        binding->scope_generation != context->scope_generation)
        return;
    XemuShaderBrowserPerformanceSample sample = { 0 };
    sample.owner = XEMU_SHADER_BROWSER_PERF_BINDING;
    sample.metric = metric;
    sample.backend = backend;
    sample.route = route;
    sample.variant_kind = variant_kind;
    sample.variant_id = variant_id;
    sample.context = *context;
    sample.frame = frame;
    sample.duration_ns = duration_ns;
    sample.represented_draws = represented_draws;
    sample.identity_count = binding->count;
    sample.flags = flags;
    for (uint32_t i = 0; i < binding->count; ++i) {
        sample.identities[i].version = 1;
        sample.identities[i].stage = binding->identities[i].stage;
        memcpy(sample.identities[i].hash, binding->identities[i].hash,
               sizeof(sample.identities[i].hash));
    }
    xemu_shader_browser_publish_performance_samples(&sample, 1);
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

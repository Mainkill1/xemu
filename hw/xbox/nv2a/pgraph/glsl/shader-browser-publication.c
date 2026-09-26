/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "shader-browser-publication.h"

#include <string.h>
#include <glib.h>

void pgraph_shader_browser_publish_binding(const ShaderState *state,
                                           bool geometry_needed,
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
    binding->scope_generation = generation;
    for (uint32_t i = 0; i < stage_count; ++i) {
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
}

void pgraph_shader_browser_refresh_binding_scope(
    const ShaderState *state, bool geometry_needed,
    PGRAPHShaderBrowserBinding *binding)
{
    if (binding && xemu_shader_browser_monitoring_enabled() &&
        binding->scope_generation != xemu_shader_browser_scope_generation()) {
        pgraph_shader_browser_publish_binding(state, geometry_needed, binding);
    }
}

void pgraph_shader_browser_capture_binding(
    const ShaderState *state, bool geometry_needed,
    PGRAPHShaderBrowserBinding *binding)
{
    if (!state || !binding) return;
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
        if (!pgraph_shader_browser_encode_recipe(
                state, stages[i], recipe, sizeof(recipe), &size) ||
            !xemu_shader_browser_compute_shader_hash(
                1, stages[i], PGRAPH_SHADER_BROWSER_RECIPE_VERSION,
                recipe, size, identity->hash)) continue;
        identity->stage = stages[i];
        ++binding->count;
    }
}

void pgraph_shader_browser_publish_stage_timing(
    const ShaderState *state, uint32_t stage, uint32_t backend,
    uint32_t route, uint32_t metric, uint64_t duration_ns, uint32_t flags)
{
    pgraph_shader_browser_publish_stage_timing_at_scope(
        state, stage, backend, route, metric, duration_ns, flags,
        xemu_shader_browser_scope_generation());
}

void pgraph_shader_browser_publish_stage_timing_at_scope(
    const ShaderState *state, uint32_t stage, uint32_t backend,
    uint32_t route, uint32_t metric, uint64_t duration_ns, uint32_t flags,
    uint64_t scope_generation)
{
    if (!state || !duration_ns) return;
    uint8_t recipe[PGRAPH_SHADER_BROWSER_RECIPE_MAX];
    size_t recipe_size = 0;
    XemuShaderBrowserPerformanceSample sample = { 0 };
    if (!pgraph_shader_browser_encode_recipe(
            state, stage, recipe, sizeof(recipe), &recipe_size) ||
        !xemu_shader_browser_compute_shader_hash(
            1, stage, PGRAPH_SHADER_BROWSER_RECIPE_VERSION, recipe,
            recipe_size, sample.identities[0].hash)) return;
    sample.owner = XEMU_SHADER_BROWSER_PERF_STAGE;
    sample.metric = metric;
    sample.backend = backend;
    sample.route = route;
    sample.scope_generation = scope_generation;
    sample.duration_ns = duration_ns;
    sample.identity_count = 1;
    sample.identities[0].version = 1;
    sample.identities[0].stage = stage;
    sample.flags = flags;
    xemu_shader_browser_publish_performance_samples(&sample, 1);
}

void pgraph_shader_browser_publish_binding_timing(
    const PGRAPHShaderBrowserBinding *binding, uint32_t backend,
    uint32_t route, uint64_t variant_id, uint64_t frame, uint32_t metric,
    uint64_t duration_ns, uint64_t represented_draws, uint32_t flags)
{
    if (!binding || !binding->count || !duration_ns) return;
    XemuShaderBrowserPerformanceSample sample = { 0 };
    sample.owner = XEMU_SHADER_BROWSER_PERF_BINDING;
    sample.metric = metric;
    sample.backend = backend;
    sample.route = route;
    sample.variant_id = variant_id;
    sample.scope_generation = binding->scope_generation;
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

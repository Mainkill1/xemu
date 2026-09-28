#include "hw/xbox/nv2a/pgraph/glsl/shader-browser-publication.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint64_t generation = 7;
static uint64_t live_epoch = 1;
static uint64_t override_generation = 11;
static uint32_t title_id = 0x4d530064;
static uint32_t published_stages[10];
static uint32_t published_titles[10];
static size_t published_count;
static size_t override_resolve_count;
static int artifacts_enabled;
static int monitoring_enabled = 1;
static size_t artifact_count;
static size_t effect_count;

void xemu_shader_override_publish_effect(
    uint32_t scoped_title_id, uint32_t fingerprint_version,
    const uint8_t fingerprint[
        XEMU_SHADER_BROWSER_EXECUTABLE_FINGERPRINT_BYTES], uint32_t backend,
    uint32_t identity_version,
    const uint8_t identity_hash[XEMU_SHADER_BROWSER_HASH_BYTES],
    uint32_t stage, const XemuShaderOverrideEffect *effect)
{
    assert(scoped_title_id == title_id && fingerprint_version == 3);
    assert(fingerprint[0] == 0x44);
    assert(backend == XEMU_SHADER_OVERRIDE_BACKEND_OPENGL);
    assert(identity_version == 1 && stage == XEMU_SHADER_BROWSER_STAGE_PIXEL);
    assert(identity_hash[0] == XEMU_SHADER_BROWSER_STAGE_PIXEL);
    assert(effect->state == XEMU_SHADER_OVERRIDE_EFFECT_EFFECTIVE);
    ++effect_count;
}

static XemuShaderBrowserPerformanceSample perf_samples[8];
static size_t perf_count;

void xemu_shader_browser_publish_performance_samples(
    const XemuShaderBrowserPerformanceSample *samples, size_t count)
{
    assert(perf_count + count <= 8);
    memcpy(perf_samples + perf_count, samples, count * sizeof(*samples));
    perf_count += count;
}

uint64_t xemu_shader_browser_scope_generation(void)
{
    return generation;
}

int xemu_shader_browser_capture_performance_context(
    XemuShaderBrowserPerformanceContext *context)
{
    memset(context, 0, sizeof(*context));
    context->scope_generation = generation;
    context->live_epoch = live_epoch;
    context->profiling_generation = 1;
    context->destinations = XEMU_SHADER_BROWSER_PERF_DEST_LIVE;
    return 1;
}

uint64_t xemu_shader_browser_copy_current_scope(XemuShaderBrowserScope *scope)
{
    memset(scope, 0, sizeof(*scope));
    scope->title_id = title_id;
    scope->executable_fingerprint_version = 3;
    scope->executable_fingerprint[0] = 0x44;
    return generation;
}

uint64_t xemu_shader_override_generation(void)
{
    return override_generation;
}

int xemu_shader_override_resolve_scoped(
    uint32_t scoped_title_id, uint32_t executable_fingerprint_version,
    const uint8_t executable_fingerprint[
        XEMU_SHADER_BROWSER_EXECUTABLE_FINGERPRINT_BYTES],
    uint32_t backend, uint32_t identity_version,
    const uint8_t identity_hash[XEMU_SHADER_BROWSER_HASH_BYTES],
    uint32_t stage, XemuShaderOverridePolicy *policy)
{
    ++override_resolve_count;
    assert(scoped_title_id == title_id);
    assert(executable_fingerprint_version == 3);
    assert(executable_fingerprint[0] == 0x44);
    assert(identity_version == 1);
    assert(stage == XEMU_SHADER_BROWSER_STAGE_PIXEL);
    assert(identity_hash[0] == XEMU_SHADER_BROWSER_STAGE_PIXEL);
    memset(policy, 0, sizeof(*policy));
    policy->generation = override_generation;
    policy->rule_id = backend;
    policy->action = backend == XEMU_SHADER_OVERRIDE_BACKEND_OPENGL ?
        XEMU_SHADER_OVERRIDE_ACTION_HIGHLIGHT :
        XEMU_SHADER_OVERRIDE_ACTION_FORCE_UBER;
    return 1;
}

int xemu_shader_browser_compute_shader_hash(
    uint32_t identity_version, uint32_t stage, uint32_t recipe_version,
    const uint8_t *recipe, size_t size,
    uint8_t hash[XEMU_SHADER_BROWSER_HASH_BYTES])
{
    assert(identity_version == 1 && recipe_version == 1);
    assert(size > 6 && memcmp(recipe, "NV2A", 4) == 0);
    assert(recipe[4] == stage);
    memset(hash, 0, XEMU_SHADER_BROWSER_HASH_BYTES);
    hash[0] = stage;
    return 1;
}

int xemu_shader_browser_publish_shader(
    const XemuShaderBrowserShaderRecord *record)
{
    assert(published_count < 10);
    assert(record->identity_hash[0] == record->stage);
    published_stages[published_count] = record->stage;
    published_titles[published_count] = record->scope.title_id;
    ++published_count;
    return 1;
}

int xemu_shader_browser_external_artifacts_enabled(void)
{
    return artifacts_enabled;
}

int xemu_shader_browser_monitoring_enabled(void)
{
    return monitoring_enabled;
}

int xemu_shader_browser_publish_external_artifact(
    const XemuShaderBrowserExternalArtifact *artifact)
{
    assert(artifact->title_id == title_id);
    assert(artifact->stage == XEMU_SHADER_BROWSER_STAGE_FIXED_FUNCTION);
    assert(artifact->identity_hash[0] == artifact->stage);
    assert(strcmp(artifact->backend, "opengl") == 0);
    assert(strcmp(artifact->kind, "glsl") == 0);
    assert(strcmp(artifact->content_hash, "2cf24dba5fb0a30e26e83b2ac5b9e29e1b16"
                                          "1e5c1fa7425e73043362938b9824") == 0);
    ++artifact_count;
    return 1;
}

int main(void)
{
    ShaderState state = { 0 };
    state.vsh.programmable.program_length = 1;
    PGRAPHShaderBrowserBinding binding = { 0 };
    pgraph_shader_browser_publish_binding(&state, false, &binding);
    assert(binding.count == 2 && binding.scope_generation == 7);
    assert(binding.override_generation == override_generation);
    assert(published_count == 2);
    assert(published_stages[0] == XEMU_SHADER_BROWSER_STAGE_VERTEX);
    assert(published_stages[1] == XEMU_SHADER_BROWSER_STAGE_PIXEL);
    assert(published_titles[0] == title_id && published_titles[1] == title_id);
    assert(override_resolve_count == 2);
    assert(binding.opengl_policy.action ==
           XEMU_SHADER_OVERRIDE_ACTION_HIGHLIGHT);
    assert(binding.vulkan_policy.action ==
           XEMU_SHADER_OVERRIDE_ACTION_FORCE_UBER);
    XemuShaderOverrideEffect effect = {
        .rule_id = binding.opengl_policy.rule_id,
        .state = XEMU_SHADER_OVERRIDE_EFFECT_EFFECTIVE,
    };
    pgraph_shader_browser_publish_override_effect(
        &binding, XEMU_SHADER_OVERRIDE_BACKEND_OPENGL, &effect);
    assert(effect_count == 1);

    pgraph_shader_browser_refresh_binding_scope(&state, false, &binding);
    assert(published_count == 2);
    assert(override_resolve_count == 2);

    /* A policy edit refreshes the attached policies without re-publishing
     * canonical shader identity or waiting for guest shader state to dirty. */
    ++override_generation;
    pgraph_shader_browser_refresh_binding_scope(&state, false, &binding);
    assert(published_count == 2);
    assert(override_resolve_count == 4);
    assert(binding.override_generation == override_generation);

    title_id = 0x54540001;
    ++generation;
    pgraph_shader_browser_refresh_binding_scope(&state, false, &binding);
    assert(binding.scope_generation == generation && published_count == 4);
    assert(published_titles[2] == title_id && published_titles[3] == title_id);
    assert(override_resolve_count == 6);

    memset(&binding, 0, sizeof(binding));
    state.vsh.is_fixed_function = true;
    pgraph_shader_browser_publish_binding(&state, true, &binding);
    assert(binding.count == 3 && published_count == 7);
    assert(published_stages[4] == XEMU_SHADER_BROWSER_STAGE_FIXED_FUNCTION);
    assert(published_stages[5] == XEMU_SHADER_BROWSER_STAGE_PIXEL);
    assert(published_stages[6] == XEMU_SHADER_BROWSER_STAGE_GEOMETRY);
    assert(override_resolve_count == 8);
    pgraph_shader_browser_publish_stage_timing(
        &state, XEMU_SHADER_BROWSER_STAGE_FIXED_FUNCTION,
        XEMU_SHADER_BROWSER_BACKEND_GL, XEMU_SHADER_BROWSER_ROUTE_SPECIALIZED,
        XEMU_SHADER_BROWSER_PERF_COMPILE_CPU, 1234,
        XEMU_SHADER_BROWSER_SAMPLE_FOREGROUND);
    pgraph_shader_browser_publish_binding_timing(
        &binding, XEMU_SHADER_BROWSER_BACKEND_GL,
        XEMU_SHADER_BROWSER_ROUTE_SPECIALIZED, 99, 12,
        XEMU_SHADER_BROWSER_PERF_LINK_OR_PIPELINE_CPU, 5000, 0,
        XEMU_SHADER_BROWSER_SAMPLE_FOREGROUND);
    assert(perf_count == 2);
    assert(perf_samples[0].owner == XEMU_SHADER_BROWSER_PERF_STAGE);
    assert(perf_samples[0].identity_count == 1);
    assert(perf_samples[0].identities[0].stage ==
           XEMU_SHADER_BROWSER_STAGE_FIXED_FUNCTION);
    assert(perf_samples[1].owner == XEMU_SHADER_BROWSER_PERF_BINDING);
    assert(perf_samples[1].identity_count == 3);
    assert(perf_samples[1].variant_id == 99);
    assert(perf_samples[1].variant_kind == XEMU_SHADER_BROWSER_VARIANT_BINDING);
    assert(perf_samples[1].identities[2].stage ==
           XEMU_SHADER_BROWSER_STAGE_GEOMETRY);
    size_t catalog_count = published_count;
    PGRAPHShaderBrowserBinding captured = { 0 };
    pgraph_shader_browser_capture_binding(&state, true, &captured);
    assert(captured.count == 3);
    assert(captured.scope_generation == generation);
    assert(published_count == catalog_count);
    state.vsh.is_fixed_function = false;
    const uint32_t stages[] = {
        XEMU_SHADER_BROWSER_STAGE_VERTEX,
        XEMU_SHADER_BROWSER_STAGE_PIXEL,
        XEMU_SHADER_BROWSER_STAGE_GEOMETRY,
    };
    for (size_t i = 0; i < 3; ++i) {
        pgraph_shader_browser_publish_stage_timing(
            &state, stages[i], XEMU_SHADER_BROWSER_BACKEND_GL,
            XEMU_SHADER_BROWSER_ROUTE_SPECIALIZED,
            XEMU_SHADER_BROWSER_PERF_SOURCE_CPU, 100 + i,
            XEMU_SHADER_BROWSER_SAMPLE_FOREGROUND);
        assert(perf_samples[2 + i].identities[0].stage == stages[i]);
        assert(perf_samples[2 + i].identities[0].hash[0] == stages[i]);
    }
    pgraph_shader_browser_publish_stage_timing_at_scope(
        &state, XEMU_SHADER_BROWSER_STAGE_PIXEL, XEMU_SHADER_BROWSER_BACKEND_VK,
        XEMU_SHADER_BROWSER_ROUTE_SPECIALIZED,
        XEMU_SHADER_BROWSER_PERF_COMPILE_CPU, 250,
        XEMU_SHADER_BROWSER_SAMPLE_BACKGROUND, 12);
    assert(perf_count == 6);
    assert(perf_samples[5].context.scope_generation == 12);
    assert(perf_samples[5].flags == XEMU_SHADER_BROWSER_SAMPLE_BACKGROUND);

    /* A compile begun before a session transition keeps its issue-time
     * context even when publication happens after the transition. */
    perf_count = 0;
    PGRAPHShaderBrowserBinding timed_binding = binding;
    assert(
        pgraph_shader_browser_capture_binding_timing_context(&timed_binding));
    uint64_t captured_generation = generation;
    uint64_t captured_epoch = live_epoch;
    ++generation;
    ++live_epoch;
    pgraph_shader_browser_publish_captured_binding_timing(
        &timed_binding, XEMU_SHADER_BROWSER_BACKEND_GL,
        XEMU_SHADER_BROWSER_ROUTE_SPECIALIZED,
        XEMU_SHADER_BROWSER_VARIANT_BINDING, 99, 12,
        XEMU_SHADER_BROWSER_PERF_FOREGROUND_STALL_CPU, 7000, 0,
        XEMU_SHADER_BROWSER_SAMPLE_FOREGROUND);
    assert(perf_count == 1);
    assert(perf_samples[0].context.scope_generation == captured_generation);
    assert(perf_samples[0].context.live_epoch == captured_epoch);
    assert(perf_samples[0].duration_ns == 7000);

    state.vsh.is_fixed_function = true;
    const uint8_t source[] = "hello";
    pgraph_shader_browser_publish_generated_artifact(
        &state, XEMU_SHADER_BROWSER_STAGE_FIXED_FUNCTION, "opengl",
        "specialized", "glsl", "glsl", source, sizeof(source) - 1);
    assert(artifact_count == 0);
    artifacts_enabled = 1;
    pgraph_shader_browser_publish_generated_artifact(
        &state, XEMU_SHADER_BROWSER_STAGE_FIXED_FUNCTION, "opengl",
        "specialized", "glsl", "glsl", source, sizeof(source) - 1);
    assert(artifact_count == 1 && published_count == 8);
    PGRAPHShaderBrowserBinding pixel_only = { 0 };
    pgraph_shader_browser_publish_pixel_binding(&state, &pixel_only);
    assert(pixel_only.count == 1 && published_count == 9);
    assert(pixel_only.identities[0].stage == XEMU_SHADER_BROWSER_STAGE_PIXEL);
    assert(published_stages[8] == XEMU_SHADER_BROWSER_STAGE_PIXEL);
    ++generation;
    pgraph_shader_browser_refresh_binding_scope(&state, true, &pixel_only);
    assert(pixel_only.count == 1 && published_count == 10);
    assert(published_stages[9] == XEMU_SHADER_BROWSER_STAGE_PIXEL);
    puts("1..1\nok 1 - renderer discovery publishes guest stages, title scope, "
         "and refreshable override policies");
    return 0;
}

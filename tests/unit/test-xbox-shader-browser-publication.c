#include "hw/xbox/nv2a/pgraph/glsl/shader-browser-publication.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint64_t generation = 7;
static uint32_t title_id = 0x4d530064;
static uint32_t published_stages[8];
static uint32_t published_titles[8];
static size_t published_count;
static int artifacts_enabled;
static int monitoring_enabled = 1;
static size_t artifact_count;
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

uint64_t xemu_shader_browser_copy_current_scope(XemuShaderBrowserScope *scope)
{
    memset(scope, 0, sizeof(*scope));
    scope->title_id = title_id;
    return generation;
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
    assert(published_count < 8);
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
    assert(published_count == 2);
    assert(published_stages[0] == XEMU_SHADER_BROWSER_STAGE_VERTEX);
    assert(published_stages[1] == XEMU_SHADER_BROWSER_STAGE_PIXEL);
    assert(published_titles[0] == title_id && published_titles[1] == title_id);

    pgraph_shader_browser_refresh_binding_scope(&state, false, &binding);
    assert(published_count == 2);
    monitoring_enabled = 0;
    ++generation;
    pgraph_shader_browser_refresh_binding_scope(&state, false, &binding);
    assert(published_count == 2 && binding.scope_generation == 7);
    monitoring_enabled = 1;
    pgraph_shader_browser_refresh_binding_scope(&state, false, &binding);
    assert(published_count == 4 && binding.scope_generation == generation);
    published_count = 2;
    title_id = 0x54540001;
    ++generation;
    pgraph_shader_browser_refresh_binding_scope(&state, false, &binding);
    assert(binding.scope_generation == generation && published_count == 4);
    assert(published_titles[2] == title_id && published_titles[3] == title_id);

    memset(&binding, 0, sizeof(binding));
    state.vsh.is_fixed_function = true;
    pgraph_shader_browser_publish_binding(&state, true, &binding);
    assert(binding.count == 3 && published_count == 7);
    assert(published_stages[4] == XEMU_SHADER_BROWSER_STAGE_FIXED_FUNCTION);
    assert(published_stages[5] == XEMU_SHADER_BROWSER_STAGE_PIXEL);
    assert(published_stages[6] == XEMU_SHADER_BROWSER_STAGE_GEOMETRY);
    pgraph_shader_browser_publish_stage_timing(
        &state, XEMU_SHADER_BROWSER_STAGE_FIXED_FUNCTION,
        XEMU_SHADER_BROWSER_BACKEND_GL,
        XEMU_SHADER_BROWSER_ROUTE_SPECIALIZED,
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
    assert(perf_samples[1].identities[2].stage ==
           XEMU_SHADER_BROWSER_STAGE_GEOMETRY);
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
    puts("1..1\nok 1 - renderer discovery publishes guest stages and title "
         "scope");
    return 0;
}

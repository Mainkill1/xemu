#include "hw/xbox/nv2a/pgraph/glsl/shader-browser-publication.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint64_t generation = 7;
static uint64_t override_generation = 11;
static uint32_t title_id = 0x4d530064;
static uint32_t published_stages[8];
static uint32_t published_titles[8];
static size_t published_count;
static size_t override_resolve_count;
static int artifacts_enabled;
static size_t artifact_count;

uint64_t xemu_shader_browser_scope_generation(void)
{
    return generation;
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
    puts("1..1\nok 1 - renderer discovery publishes guest stages, title scope, "
         "and refreshable override policies");
    return 0;
}

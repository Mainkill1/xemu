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
        if (binding->count == 1 &&
            binding->identities[0].stage == XEMU_SHADER_BROWSER_STAGE_PIXEL) {
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

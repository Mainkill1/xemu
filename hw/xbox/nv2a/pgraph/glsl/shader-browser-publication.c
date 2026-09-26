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
    if (binding &&
        binding->scope_generation != xemu_shader_browser_scope_generation()) {
        pgraph_shader_browser_publish_binding(state, geometry_needed, binding);
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

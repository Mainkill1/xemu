#include "hw/xbox/nv2a/pgraph/glsl/shader-browser-publication.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint64_t generation = 7;
static uint32_t title_id = 0x4d530064;
static uint32_t published_stages[8];
static uint32_t published_titles[8];
static size_t published_count;

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
    puts("1..1\nok 1 - renderer discovery publishes guest stages and title "
         "scope");
    return 0;
}

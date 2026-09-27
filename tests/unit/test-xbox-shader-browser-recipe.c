#include "hw/xbox/nv2a/pgraph/glsl/shaders.h"
#include "hw/xbox/nv2a/pgraph/glsl/shader-browser-recipe.h"
#include "ui/xui/shader-browser-session-provider.hh"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static size_t encode(const ShaderState *state, uint32_t stage, uint8_t *data)
{
    size_t size = 0;
    assert(
        pgraph_shader_browser_encode_recipe(state, stage, data, 8192, &size));
    assert(size > 6);
    assert(memcmp(data, "NV2A", 4) == 0);
    assert(data[4] == stage);
    return size;
}

int main(void)
{
    ShaderState state = { 0 };
    uint8_t first[8192], second[8192];

    state.vsh.programmable.program_length = 1;
    state.vsh.programmable.program_data[0][0] = 0x12345678;
    size_t first_size = encode(&state, XEMU_SHADER_BROWSER_STAGE_VERTEX, first);
    assert(first[5] == 0);
    assert(first_size == 53);
    assert(first[37] == 0x78 && first[38] == 0x56 && first[39] == 0x34 &&
           first[40] == 0x12);

    /* Host render scale and inactive program slots are not guest identity. */
    state.vsh.surface_scale_factor = 4;
    state.vsh.programmable.program_data[2][0] = 0xdeadbeef;
    size_t second_size =
        encode(&state, XEMU_SHADER_BROWSER_STAGE_VERTEX, second);
    assert(second_size == first_size);
    assert(memcmp(first, second, first_size) == 0);

    state.vsh.programmable.program_data[0][0] ^= 1;
    second_size = encode(&state, XEMU_SHADER_BROWSER_STAGE_VERTEX, second);
    assert(second_size == first_size);
    assert(memcmp(first, second, first_size) != 0);

    state.vsh.is_fixed_function = true;
    state.vsh.fixed_function.lighting = true;
    first_size =
        encode(&state, XEMU_SHADER_BROWSER_STAGE_FIXED_FUNCTION, first);
    assert(first[5] == 1);
    state.vsh.fixed_function.lighting = false;
    second_size =
        encode(&state, XEMU_SHADER_BROWSER_STAGE_FIXED_FUNCTION, second);
    assert(second_size == first_size);
    assert(memcmp(first, second, first_size) != 0);
    assert(!pgraph_shader_browser_encode_recipe(
        &state, XEMU_SHADER_BROWSER_STAGE_VERTEX, second, sizeof(second),
        &second_size));

    state.psh.combiner_control = 1;
    first_size = encode(&state, XEMU_SHADER_BROWSER_STAGE_PIXEL, first);
    state.psh.border_inv_real_size[0][0] = 0.125f;
    second_size = encode(&state, XEMU_SHADER_BROWSER_STAGE_PIXEL, second);
    assert(second_size == first_size);
    assert(memcmp(first, second, first_size) == 0);
    state.psh.rgb_inputs[0] = 0x11223344;
    second_size = encode(&state, XEMU_SHADER_BROWSER_STAGE_PIXEL, second);
    assert(second_size == first_size);
    assert(memcmp(first, second, first_size) != 0);

    first_size = encode(&state, XEMU_SHADER_BROWSER_STAGE_GEOMETRY, first);
    state.geom.tri_rot0 = -2;
    second_size = encode(&state, XEMU_SHADER_BROWSER_STAGE_GEOMETRY, second);
    assert(second_size == first_size);
    assert(memcmp(first, second, first_size) != 0);

    assert(!pgraph_shader_browser_encode_recipe(
        &state, XEMU_SHADER_BROWSER_STAGE_PIXEL, second, 8, &second_size));
    assert(!pgraph_shader_browser_encode_recipe(
        &state, XEMU_SHADER_BROWSER_STAGE_UNKNOWN, second, sizeof(second),
        &second_size));
    puts("1..1\nok 1 - canonical shader recipes are portable and "
         "stage-specific");
    return 0;
}

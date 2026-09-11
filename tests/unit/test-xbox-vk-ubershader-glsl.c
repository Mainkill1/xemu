/*
 * NV2A Vulkan ubershader GLSL generator tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/mstring.h"

#include "hw/xbox/nv2a/pgraph/glsl/psh.h"

/* This generator test does not exercise texture-format classification. */
const BasicColorFormatInfo kelvin_color_format_info_map[66] = { 0 };

static PshState base_state(void)
{
    PshState state = { 0 };

    state.combiner_control = 1 | (PS_COMBINERCOUNT_MUX_MSB << 8);
    state.final_inputs_0 = 0x20202020;
    state.final_inputs_1 = 0x20202000;
    state.smooth_shading = true;
    state.depth_format = DEPTH_FORMAT_D24;
    return state;
}

static char *generate(const PshState *state, bool uber)
{
    GenPshGlslOptions opts = {
        .vulkan = true,
        .ubo_binding = 1,
        .tex_binding = 2,
        .ubershader = uber,
        .uber_binding = 6,
    };
    MString *source = pgraph_glsl_gen_psh(state, opts);
    char *copy = g_strdup(mstring_get_str(source));

    mstring_unref(source);
    return copy;
}

static void test_packet_abi_and_dynamic_combiner_are_emitted(void)
{
    PshState state = base_state();
    g_autofree char *source = generate(&state, true);

    g_assert_nonnull(strstr(source,
        "layout(binding = 6, std140) uniform PshUberControls"));
    g_assert_nonnull(strstr(source, "uvec4 uberHeader;"));
    g_assert_nonnull(strstr(source, "uvec4 uberStage[8];"));
    g_assert_nonnull(strstr(source, "uvec4 uberFinal;"));
    g_assert_nonnull(strstr(source, "vec4 uberConstants[18];"));
    g_assert_nonnull(strstr(source,
        "for (uint uberIndex = 0u; uberIndex < uberHeader.y; uberIndex++)"));
    g_assert_nonnull(strstr(source, "uberOldR0.a >= 0.5"));
    g_assert_nonnull(strstr(source, "fragColor.rgb = uberD.rgb + mix("));
    g_assert_nonnull(strstr(source, "fragColor.a = uberG.a;"));
}

static void test_combiner_state_does_not_specialize_uber_source(void)
{
    PshState a = base_state();
    PshState b = a;

    b.combiner_control = 8 | (PS_COMBINERCOUNT_MUX_MSB << 8) |
                         (PS_COMBINERCOUNT_UNIQUE_C0 << 8) |
                         (PS_COMBINERCOUNT_UNIQUE_C1 << 8);
    memset(b.rgb_inputs, 0xa5, sizeof(b.rgb_inputs));
    memset(b.alpha_inputs, 0x5a, sizeof(b.alpha_inputs));
    memset(b.rgb_outputs, 0x33, sizeof(b.rgb_outputs));
    memset(b.alpha_outputs, 0xcc, sizeof(b.alpha_outputs));
    b.final_inputs_0 = 0x10203040;
    b.final_inputs_1 = 0x50607080;

    g_autofree char *source_a = generate(&a, true);
    g_autofree char *source_b = generate(&b, true);

    g_assert_cmpstr(source_a, ==, source_b);
}

static void test_shell_state_still_specializes_uber_source(void)
{
    PshState a = base_state();
    PshState b = a;

    b.alpha_test = true;
    b.alpha_func = ALPHA_FUNC_LESS;

    g_autofree char *source_a = generate(&a, true);
    g_autofree char *source_b = generate(&b, true);

    g_assert_cmpstr(source_a, !=, source_b);
    g_assert_nonnull(strstr(source_b, "fragAlpha < alphaRef"));
}

static void test_normal_generation_remains_specialized(void)
{
    PshState a = base_state();
    PshState b = a;

    a.rgb_inputs[0] = 0x04040404;
    b.rgb_inputs[0] = 0x05050505;

    g_autofree char *source_a = generate(&a, false);
    g_autofree char *source_b = generate(&b, false);

    g_assert_null(strstr(source_a, "PshUberControls"));
    g_assert_cmpstr(source_a, !=, source_b);
}

static void test_generation_option_discriminates_module_key_bytes(void)
{
    GenPshGlslOptions specialized = {
        .vulkan = true,
        .ubo_binding = 1,
        .tex_binding = 2,
        .uber_binding = 6,
    };
    GenPshGlslOptions uber = specialized;

    uber.ubershader = true;
    g_assert_cmpint(memcmp(&specialized, &uber, sizeof(uber)), !=, 0);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/vk/ubershader/glsl/packet-abi",
                    test_packet_abi_and_dynamic_combiner_are_emitted);
    g_test_add_func("/xbox/vk/ubershader/glsl/combiner-not-specialized",
                    test_combiner_state_does_not_specialize_uber_source);
    g_test_add_func("/xbox/vk/ubershader/glsl/shell-specialized",
                    test_shell_state_still_specializes_uber_source);
    g_test_add_func("/xbox/vk/ubershader/glsl/normal-unchanged",
                    test_normal_generation_remains_specialized);
    g_test_add_func("/xbox/vk/ubershader/glsl/key-discriminator",
                    test_generation_option_discriminates_module_key_bytes);
    return g_test_run();
}

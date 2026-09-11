/*
 * NV2A Vulkan ubershader runtime-key regression tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/xbox/nv2a/pgraph/vk/renderer.h"

static ShaderState base_state(void)
{
    ShaderState state = { 0 };

    state.psh.shader_stage_program = 0x12345678;
    state.psh.other_stage_input = 0x9abcdef0;
    state.psh.alpha_test = true;
    state.psh.alpha_func = ALPHA_FUNC_LESS;
    return state;
}

static PipelineKey pipeline_key(PGRAPHVkFragmentRoute route,
                                const ShaderState *state)
{
    PipelineKey key = { 0 };

    key.fragment_route = route;
    key.shader_state = *state;
    if (route == PGRAPH_VK_FRAGMENT_UBERSHADER) {
        pgraph_vk_canonicalize_uber_combiner_state(&key.shader_state.psh);
    }
    return key;
}

static void test_dynamic_control_binding_matches_packet_abi(void)
{
    g_assert_cmpuint(PGRAPH_VK_PSH_UBER_UBO_BINDING, ==, 6);
    g_assert_cmpuint(sizeof(PGRAPHUberControls), ==, 448);
    g_assert_cmpuint(_Alignof(PGRAPHUberControls), ==, 16);
}

static void test_shader_binding_key_equality_requires_route_and_full_state(void)
{
    ShaderBindingKey a = {
        .state = base_state(),
        .fragment_route = PGRAPH_VK_FRAGMENT_SPECIALIZED,
    };
    ShaderBindingKey b = a;

    g_assert_true(pgraph_vk_shader_binding_key_equal(&a, &b));
    g_assert_false(pgraph_vk_shader_binding_key_different(&a, &b));
    b.fragment_route = PGRAPH_VK_FRAGMENT_UBERSHADER;
    g_assert_false(pgraph_vk_shader_binding_key_equal(&a, &b));
    g_assert_true(pgraph_vk_shader_binding_key_different(&a, &b));
    b = a;
    b.state.psh.combiner_control = 1;
    g_assert_false(pgraph_vk_shader_binding_key_equal(&a, &b));
    g_assert_true(pgraph_vk_shader_binding_key_different(&a, &b));
}

static void test_uber_pipeline_key_ignores_only_combiner_words(void)
{
    ShaderState a = base_state();
    ShaderState b = a;

    a.psh.combiner_control = 1;
    a.psh.rgb_inputs[0] = 0x11111111;
    a.psh.alpha_inputs[0] = 0x22222222;
    a.psh.rgb_outputs[0] = 0x33333333;
    a.psh.alpha_outputs[0] = 0x44444444;
    a.psh.final_inputs_0 = 0x55555555;
    a.psh.final_inputs_1 = 0x66666666;
    b.psh.combiner_control = 8;
    b.psh.rgb_inputs[0] = 0xaaaaaaaa;
    b.psh.alpha_inputs[0] = 0xbbbbbbbb;
    b.psh.rgb_outputs[0] = 0xcccccccc;
    b.psh.alpha_outputs[0] = 0xdddddddd;
    b.psh.final_inputs_0 = 0xeeeeeeee;
    b.psh.final_inputs_1 = 0xffffffff;

    PipelineKey uber_a = pipeline_key(PGRAPH_VK_FRAGMENT_UBERSHADER, &a);
    PipelineKey uber_b = pipeline_key(PGRAPH_VK_FRAGMENT_UBERSHADER, &b);
    PipelineKey specialized_a =
        pipeline_key(PGRAPH_VK_FRAGMENT_SPECIALIZED, &a);
    PipelineKey specialized_b =
        pipeline_key(PGRAPH_VK_FRAGMENT_SPECIALIZED, &b);

    g_assert_cmpmem(&uber_a, sizeof(uber_a), &uber_b, sizeof(uber_b));
    g_assert_cmpint(memcmp(&specialized_a, &specialized_b,
                           sizeof(specialized_a)), !=, 0);
}

static void test_fragment_route_keeps_pipeline_keys_isolated(void)
{
    ShaderState state = base_state();
    PipelineKey specialized =
        pipeline_key(PGRAPH_VK_FRAGMENT_SPECIALIZED, &state);
    PipelineKey uber = pipeline_key(PGRAPH_VK_FRAGMENT_UBERSHADER, &state);

    g_assert_cmpint(memcmp(&specialized, &uber, sizeof(specialized)), !=, 0);
}

static void test_canonicalization_preserves_fragment_shell_state(void)
{
    PshState state = { 0 };

    state.combiner_control = 0xffffffff;
    memset(state.rgb_inputs, 0x11, sizeof(state.rgb_inputs));
    memset(state.alpha_inputs, 0x22, sizeof(state.alpha_inputs));
    memset(state.rgb_outputs, 0x33, sizeof(state.rgb_outputs));
    memset(state.alpha_outputs, 0x44, sizeof(state.alpha_outputs));
    state.final_inputs_0 = 0x55555555;
    state.final_inputs_1 = 0x66666666;
    state.shader_stage_program = 0x77777777;
    state.other_stage_input = 0x88888888;
    state.alpha_test = true;
    state.alpha_func = ALPHA_FUNC_GREATER;

    pgraph_vk_canonicalize_uber_combiner_state(&state);

    g_assert_cmpuint(state.combiner_control, ==, 0);
    g_assert_cmpmem(state.rgb_inputs, sizeof(state.rgb_inputs),
                    (uint32_t[8]) { 0 }, sizeof(state.rgb_inputs));
    g_assert_cmpmem(state.alpha_inputs, sizeof(state.alpha_inputs),
                    (uint32_t[8]) { 0 }, sizeof(state.alpha_inputs));
    g_assert_cmpmem(state.rgb_outputs, sizeof(state.rgb_outputs),
                    (uint32_t[8]) { 0 }, sizeof(state.rgb_outputs));
    g_assert_cmpmem(state.alpha_outputs, sizeof(state.alpha_outputs),
                    (uint32_t[8]) { 0 }, sizeof(state.alpha_outputs));
    g_assert_cmpuint(state.final_inputs_0, ==, 0);
    g_assert_cmpuint(state.final_inputs_1, ==, 0);
    g_assert_cmpuint(state.shader_stage_program, ==, 0x77777777);
    g_assert_cmpuint(state.other_stage_input, ==, 0x88888888);
    g_assert_true(state.alpha_test);
    g_assert_cmpint(state.alpha_func, ==, ALPHA_FUNC_GREATER);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/vk/ubershader/runtime/control-abi",
                    test_dynamic_control_binding_matches_packet_abi);
    g_test_add_func("/xbox/vk/ubershader/runtime/shader-binding-key",
                    test_shader_binding_key_equality_requires_route_and_full_state);
    g_test_add_func("/xbox/vk/ubershader/runtime/canonical-key",
                    test_uber_pipeline_key_ignores_only_combiner_words);
    g_test_add_func("/xbox/vk/ubershader/runtime/route-isolation",
                    test_fragment_route_keeps_pipeline_keys_isolated);
    g_test_add_func("/xbox/vk/ubershader/runtime/shell-state",
                    test_canonicalization_preserves_fragment_shell_state);
    return g_test_run();
}

/*
 * NV2A Vulkan ubershader compiler and reflection integration test
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/xbox/nv2a/pgraph/glsl/psh.h"
#include "hw/xbox/nv2a/pgraph/texture.h"
#include "hw/xbox/nv2a/pgraph/vk/renderer.h"
#include "ui/xemu-settings.h"

struct config g_config;

/* This compiler test does not exercise texture-format classification. */
const BasicColorFormatInfo kelvin_color_format_info_map[66] = { 0 };

void nv2a_profile_log_event_once(const char *event)
{
    (void)event;
}

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

static const SpvReflectDescriptorBinding *find_binding(
    const ShaderModuleInfo *info, uint32_t set, uint32_t binding)
{
    for (uint32_t i = 0; i < info->reflect_module.descriptor_set_count; i++) {
        const SpvReflectDescriptorSet *descriptor_set =
            info->descriptor_sets[i];

        if (descriptor_set->set != set) {
            continue;
        }
        for (uint32_t j = 0; j < descriptor_set->binding_count; j++) {
            if (descriptor_set->bindings[j]->binding == binding) {
                return descriptor_set->bindings[j];
            }
        }
    }
    return NULL;
}

static void test_real_compiler_and_reflection_accept_uber_abi(void)
{
    PshState state = base_state();
    GenPshGlslOptions opts = {
        .vulkan = true,
        .ubo_binding = 1,
        .tex_binding = 2,
        .ubershader = true,
        .uber_binding = PGRAPH_VK_PSH_UBER_UBO_BINDING,
    };
    PGRAPHVkState renderer = {
        .vk_api_version = VK_API_VERSION_1_1,
    };
    ShaderModuleInfo info = { 0 };
    MString *source = pgraph_glsl_gen_psh(&state, opts);

    pgraph_vk_init_glsl_compiler();
    info.spirv = pgraph_vk_compile_glsl_to_spv(
        &renderer, GLSLANG_STAGE_FRAGMENT, mstring_get_str(source));
    g_assert_nonnull(info.spirv);
    g_assert_true(pgraph_vk_init_shader_module_layout_from_spv(
        &info, VK_SHADER_STAGE_FRAGMENT_BIT));
    g_assert_true(info.uses_uber_controls);

    const SpvReflectDescriptorBinding *binding = find_binding(
        &info, 0, PGRAPH_VK_PSH_UBER_UBO_BINDING);
    g_assert_nonnull(binding);
    g_assert_true(pgraph_vk_uber_controls_block_matches_abi(&binding->block));
    g_assert_cmpuint(binding->block.size, ==, sizeof(PGRAPHUberControls));
    g_assert_cmpuint(binding->block.member_count, ==, 4);
    g_assert_cmpuint(binding->block.members[0].offset, ==,
                     offsetof(PGRAPHUberControls, header));
    g_assert_cmpuint(binding->block.members[1].offset, ==,
                     offsetof(PGRAPHUberControls, stage));
    g_assert_cmpuint(binding->block.members[2].offset, ==,
                     offsetof(PGRAPHUberControls, final_words));
    g_assert_cmpuint(binding->block.members[3].offset, ==,
                     offsetof(PGRAPHUberControls, constants));

    SpvReflectBlockVariable bad_block = binding->block;
    g_autofree SpvReflectBlockVariable *bad_members = g_memdup2(
        binding->block.members,
        binding->block.member_count * sizeof(*binding->block.members));
    bad_block.members = bad_members;
    bad_block.members[1].offset++;
    g_assert_false(pgraph_vk_uber_controls_block_matches_abi(&bad_block));

    pgraph_vk_clear_shader_module_layout(&info);
    g_byte_array_unref(info.spirv);
    pgraph_vk_finalize_glsl_compiler();
    mstring_unref(source);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/vk/ubershader/glsl/compile-reflect-abi",
                    test_real_compiler_and_reflection_accept_uber_abi);
    return g_test_run();
}

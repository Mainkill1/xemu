/*
 * NV2A Vulkan hybrid family key codec tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/xbox/nv2a/pgraph/vk/hybrid-family-codec.h"

static PipelineKey make_key(void)
{
    PipelineKey key = { 0 };

    key.fragment_route = PGRAPH_VK_FRAGMENT_UBERSHADER;
    key.render_pass_state.color_format = VK_FORMAT_B8G8R8A8_UNORM;
    key.render_pass_state.zeta_format = VK_FORMAT_D24_UNORM_S8_UINT;
    key.shader_state.vsh.surface_scale_factor = 3;
    key.shader_state.vsh.compressed_attrs = 0x12;
    key.shader_state.vsh.uniform_attrs = 0x34;
    key.shader_state.vsh.swizzle_attrs = 0x56;
    key.shader_state.vsh.fog_enable = true;
    key.shader_state.vsh.fog_mode = FOG_MODE_EXP;
    key.shader_state.vsh.specular_enable = true;
    key.shader_state.vsh.specular_power = 7.25f;
    key.shader_state.vsh.point_params_enable = true;
    key.shader_state.vsh.point_size = 2.5f;
    key.shader_state.vsh.point_params[3] = 1.25f;
    key.shader_state.vsh.is_fixed_function = true;
    key.shader_state.vsh.fixed_function.normalization = true;
    key.shader_state.vsh.fixed_function.texture_matrix_enable[2] = true;
    key.shader_state.vsh.fixed_function.lighting = true;
    key.shader_state.vsh.programmable.program_length = 1;
    key.shader_state.vsh.programmable.program_data[0][0] = 0x12345678;
    key.shader_state.geom.primitive_mode = PRIM_TYPE_TRIANGLES;
    key.shader_state.geom.smooth_shading = true;
    key.shader_state.geom.tri_rot0 = -1;
    key.shader_state.psh.shader_stage_program = 0x10203040;
    key.shader_state.psh.other_stage_input = 0x50607080;
    key.shader_state.psh.point_sprite = true;
    key.shader_state.psh.rect_tex[1] = true;
    key.shader_state.psh.compare_mode[2][3] = true;
    key.shader_state.psh.colorkey_mode[0] = 2;
    key.shader_state.psh.border_logical_size[1][2] = 64.0f;
    key.shader_state.psh.shadow_map[3] = true;
    key.shader_state.psh.alpha_test = true;
    key.shader_state.psh.surface_zeta_format = 0x2a;
    key.regs[0] = 0x11111111;
    key.regs[7] = 0x77777777;
    key.binding_description_count = 1;
    key.binding_descriptions[0] = (VkVertexInputBindingDescription) {
        .binding = 2,
        .stride = 24,
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    key.attribute_description_count = 1;
    key.attribute_descriptions[0] = (VkVertexInputAttributeDescription) {
        .location = 3,
        .binding = 2,
        .format = VK_FORMAT_R32G32B32_SFLOAT,
        .offset = 8,
    };
    return key;
}

static void test_round_trip(void)
{
    PipelineKey source = make_key();
    PipelineKey decoded;
    PGRAPHVkFamilyKeyBlob first = { 0 };
    PGRAPHVkFamilyKeyBlob second = { 0 };

    g_assert_true(pgraph_vk_family_key_encode(&source, &first));
    g_assert_true(pgraph_vk_family_key_decode(
        first.data, first.size, &decoded));
    g_assert_true(pgraph_vk_family_key_encode(&decoded, &second));
    g_assert_cmpmem(first.data, first.size, second.data, second.size);
    g_assert_cmpint(decoded.fragment_route, ==,
                    PGRAPH_VK_FRAGMENT_UBERSHADER);
    g_assert_cmpuint(decoded.binding_description_count, ==, 1);
    g_assert_cmpuint(decoded.attribute_descriptions[0].offset, ==, 8);

    pgraph_vk_family_key_blob_destroy(&second);
    pgraph_vk_family_key_blob_destroy(&first);
}

static void test_padding_is_not_serialized(void)
{
    PipelineKey first = make_key();
    PipelineKey second = first;
    PGRAPHVkFamilyKeyBlob first_blob = { 0 };
    PGRAPHVkFamilyKeyBlob second_blob = { 0 };

    for (size_t i = sizeof(first.clear);
         i < offsetof(PipelineKey, fragment_route); i++) {
        ((uint8_t *)&first)[i] = 0xaa;
        ((uint8_t *)&second)[i] = 0x55;
    }
    g_assert_true(pgraph_vk_family_key_encode(&first, &first_blob));
    g_assert_true(pgraph_vk_family_key_encode(&second, &second_blob));
    g_assert_cmpmem(first_blob.data, first_blob.size,
                    second_blob.data, second_blob.size);

    pgraph_vk_family_key_blob_destroy(&second_blob);
    pgraph_vk_family_key_blob_destroy(&first_blob);
}

static void test_rejects_invalid_payloads(void)
{
    PipelineKey source = make_key();
    PipelineKey decoded;
    PGRAPHVkFamilyKeyBlob blob = { 0 };

    g_assert_true(pgraph_vk_family_key_encode(&source, &blob));
    g_assert_false(pgraph_vk_family_key_decode(
        blob.data, blob.size - 1, &decoded));
    blob.data[blob.size - 1] ^= 0xff;
    g_assert_false(pgraph_vk_family_key_decode(
        blob.data, blob.size, &decoded));

    source.binding_description_count =
        ARRAY_SIZE(source.binding_descriptions) + 1;
    g_assert_false(pgraph_vk_family_key_encode(&source, &blob));
    pgraph_vk_family_key_blob_destroy(&blob);
}

static void test_rejects_non_draw_keys(void)
{
    PipelineKey key = make_key();
    PGRAPHVkFamilyKeyBlob blob = { 0 };

    key.clear = true;
    g_assert_false(pgraph_vk_family_key_encode(&key, &blob));
    key.clear = false;
    key.fragment_route = 99;
    g_assert_false(pgraph_vk_family_key_encode(&key, &blob));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/nv2a/vk/family-codec/round-trip", test_round_trip);
    g_test_add_func("/nv2a/vk/family-codec/no-padding",
                    test_padding_is_not_serialized);
    g_test_add_func("/nv2a/vk/family-codec/invalid",
                    test_rejects_invalid_payloads);
    g_test_add_func("/nv2a/vk/family-codec/non-draw",
                    test_rejects_non_draw_keys);
    return g_test_run();
}

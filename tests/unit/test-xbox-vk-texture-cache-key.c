/*
 * NV2A Vulkan texture cache identity tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/texture-cache-key.h"

static PGRAPHVkTextureStorageShape test_storage_shape(void)
{
    return (PGRAPHVkTextureStorageShape) {
        .cubemap = false,
        .dimensionality = 2,
        .color_format = 0x12,
        .mip_levels = 5,
        .width = 32,
        .height = 16,
        .depth = 1,
        .border = false,
        .pitch = 0,
    };
}

static VkSamplerCreateInfo test_sampler(void)
{
    return (VkSamplerCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .mipLodBias = 0.0f,
        .anisotropyEnable = VK_FALSE,
        .maxAnisotropy = 1.0f,
        .compareEnable = VK_FALSE,
        .compareOp = VK_COMPARE_OP_ALWAYS,
        .minLod = 0.0f,
        .maxLod = 4.0f,
        .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
        .unnormalizedCoordinates = VK_FALSE,
    };
}

static void test_storage_mip_count(void)
{
    g_assert_cmpuint(pgraph_vk_texture_storage_mip_count(false, 2, 8, 5, 2),
                     ==, 6);
    g_assert_cmpuint(pgraph_vk_texture_storage_mip_count(false, 2, 2, 5, 2),
                     ==, 2);
    g_assert_cmpuint(pgraph_vk_texture_storage_mip_count(true, 2, 8, 5, 2),
                     ==, 1);
    g_assert_cmpuint(pgraph_vk_texture_storage_mip_count(false, 3, 8, 5, 4),
                     ==, 3);
    g_assert_cmpuint(pgraph_vk_texture_storage_mip_count(false, 3, 8, 1, 4),
                     ==, 1);
    g_assert_cmpuint(pgraph_vk_texture_storage_mip_count(false, 2, 0, 5, 2),
                     ==, 1);
    g_assert_cmpuint(pgraph_vk_texture_storage_mip_count(
                         false, 2, UINT32_MAX, UINT32_MAX, 2),
                     ==, UINT32_MAX);
}

static void test_sampler_lod_does_not_change_image_identity(void)
{
    PGRAPHVkTextureStorageShape storage_a = test_storage_shape();
    PGRAPHVkTextureStorageShape storage_b;
    PGRAPHVkTextureImageKey image_a, image_b;
    PGRAPHVkTextureSamplerKey sampler_a, sampler_b;
    VkSamplerCreateInfo sampler_info_a = test_sampler();
    VkSamplerCreateInfo sampler_info_b = sampler_info_a;

    /* Storage levels are format/dimension state, independent of LOD clamps. */
    pgraph_vk_texture_storage_shape_init(&storage_b, false, 2, 0x12,
                                         5, 32, 16, 1, false, 0);
    pgraph_vk_texture_image_key_init(&image_a, &storage_a,
                                     0x1000, 0x400, 0, 0, 1.0f);
    pgraph_vk_texture_image_key_init(&image_b, &storage_b,
                                     0x1000, 0x400, 0, 0, 1.0f);
    g_assert_true(pgraph_vk_texture_image_key_equal(&image_a, &image_b));

    sampler_info_b.minLod = 2.0f;
    sampler_info_b.maxLod = 2.0f;
    g_assert_true(pgraph_vk_texture_sampler_key_init(&sampler_a,
                                                     &sampler_info_a, NULL));
    g_assert_true(pgraph_vk_texture_sampler_key_init(&sampler_b,
                                                     &sampler_info_b, NULL));
    g_assert_false(pgraph_vk_texture_sampler_key_equal(&sampler_a, &sampler_b));
}

static void test_material_image_differences(void)
{
    PGRAPHVkTextureStorageShape storage = test_storage_shape();
    PGRAPHVkTextureImageKey a, b;

    pgraph_vk_texture_image_key_init(&a, &storage,
                                     0x1000, 0x400, 0, 0, 0.0f);
    pgraph_vk_texture_image_key_init(&b, &storage,
                                     0x1000, 0x400, 0, 0, -0.0f);
    g_assert_true(pgraph_vk_texture_image_key_equal(&a, &b));

    storage.width++;
    pgraph_vk_texture_image_key_init(&b, &storage,
                                     0x1000, 0x400, 0, 0, 0.0f);
    g_assert_false(pgraph_vk_texture_image_key_equal(&a, &b));

    storage.width--;
    pgraph_vk_texture_image_key_init(&b, &storage,
                                     0x1004, 0x400, 0, 0, 0.0f);
    g_assert_false(pgraph_vk_texture_image_key_equal(&a, &b));
}

static void test_equivalent_resolved_samplers_coalesce(void)
{
    VkSamplerCreateInfo a = test_sampler();
    VkSamplerCreateInfo b = a;
    PGRAPHVkTextureSamplerKey key_a, key_b;
    uint32_t ignored_next = 1;

    b.pNext = &ignored_next;
    b.maxAnisotropy = 16.0f; /* Disabled. */
    b.compareOp = VK_COMPARE_OP_NEVER; /* Disabled. */
    b.borderColor = VK_BORDER_COLOR_INT_TRANSPARENT_BLACK; /* Unused. */
    b.mipLodBias = -0.0f;

    g_assert_true(pgraph_vk_texture_sampler_key_init(&key_a, &a, NULL));
    g_assert_true(pgraph_vk_texture_sampler_key_init(&key_b, &b, NULL));
    g_assert_true(pgraph_vk_texture_sampler_key_equal(&key_a, &key_b));
}

static void test_material_sampler_differences(void)
{
    VkSamplerCreateInfo a = test_sampler();
    VkSamplerCreateInfo b = a;
    PGRAPHVkTextureSamplerKey key_a, key_b;
    PGRAPHVkTextureCustomBorderColor border_a = {
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .value = { 1, 2, 3, 4 },
    };
    PGRAPHVkTextureCustomBorderColor border_b = border_a;

    g_assert_true(pgraph_vk_texture_sampler_key_init(&key_a, &a, NULL));
    b.magFilter = VK_FILTER_NEAREST;
    g_assert_true(pgraph_vk_texture_sampler_key_init(&key_b, &b, NULL));
    g_assert_false(pgraph_vk_texture_sampler_key_equal(&key_a, &key_b));

    a.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    a.borderColor = VK_BORDER_COLOR_FLOAT_CUSTOM_EXT;
    b = a;
    g_assert_false(pgraph_vk_texture_sampler_key_init(&key_a, &a, NULL));
    g_assert_true(pgraph_vk_texture_sampler_key_init(&key_a, &a, &border_a));
    g_assert_true(pgraph_vk_texture_sampler_key_init(&key_b, &b, &border_a));
    g_assert_true(pgraph_vk_texture_sampler_key_equal(&key_a, &key_b));

    border_b.value[3]++;
    g_assert_true(pgraph_vk_texture_sampler_key_init(&key_b, &b, &border_b));
    g_assert_false(pgraph_vk_texture_sampler_key_equal(&key_a, &key_b));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/vk-texture-cache-key/storage-mip-count",
                    test_storage_mip_count);
    g_test_add_func("/xbox/vk-texture-cache-key/sampler-lod-image-identity",
                    test_sampler_lod_does_not_change_image_identity);
    g_test_add_func("/xbox/vk-texture-cache-key/material-image-differences",
                    test_material_image_differences);
    g_test_add_func("/xbox/vk-texture-cache-key/equivalent-samplers",
                    test_equivalent_resolved_samplers_coalesce);
    g_test_add_func("/xbox/vk-texture-cache-key/material-sampler-differences",
                    test_material_sampler_differences);
    return g_test_run();
}

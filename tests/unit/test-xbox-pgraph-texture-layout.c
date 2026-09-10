/*
 * NV2A texture layout tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/texture-layout.h"

static void test_bordered_mip_crop(void)
{
    static const struct {
        unsigned int level;
        unsigned int stored_width;
        unsigned int stored_height;
        unsigned int width;
        unsigned int height;
        unsigned int skip_pixels;
        unsigned int skip_rows;
    } cases[] = {
        { 0, 16, 16, 8, 8, 4, 4 },
        { 1, 8, 8, 4, 4, 2, 2 },
        { 2, 4, 4, 2, 2, 1, 1 },
        { 3, 2, 2, 1, 1, 0, 0 },
    };

    for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
        PGRAPHTextureMipCrop crop = pgraph_bordered_texture_mip_crop(
            8, 8, cases[i].stored_width, cases[i].stored_height,
            cases[i].level);

        g_assert_cmpuint(crop.width, ==, cases[i].width);
        g_assert_cmpuint(crop.height, ==, cases[i].height);
        g_assert_cmpuint(crop.skip_pixels, ==, cases[i].skip_pixels);
        g_assert_cmpuint(crop.skip_rows, ==, cases[i].skip_rows);
    }
}

static void test_bordered_mip_crop_clamps_subblock_tails(void)
{
    const unsigned int base[] = { 1, 2, 4 };
    const unsigned int levels[] = { 1, 2, 3 };

    for (size_t i = 0; i < ARRAY_SIZE(base); i++) {
        unsigned int stored = 16;
        unsigned int logical = base[i];

        for (unsigned int level = 0; level < levels[i]; level++) {
            PGRAPHTextureMipCrop crop = pgraph_bordered_texture_mip_crop(
                base[i], base[i], stored, stored, level);
            unsigned int skip = 4U >> level;

            g_assert_cmpuint(crop.width, ==, logical);
            g_assert_cmpuint(crop.height, ==, logical);
            g_assert_cmpuint(crop.skip_pixels, ==, skip);
            g_assert_cmpuint(crop.skip_rows, ==, skip);
            stored /= 2;
            logical = MAX(logical / 2, 1U);
        }
    }
}

static void test_compressed_subblock_face_stride(void)
{
    const unsigned int dimensions[] = { 1, 2 };

    for (size_t i = 0; i < ARRAY_SIZE(dimensions); i++) {
        TextureShape shape = {
            .cubemap = true,
            .dimensionality = 2,
            .color_format = NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT1_A1R5G5B5,
            .levels = 1,
            .storage_levels = 1,
            .width = dimensions[i],
            .height = dimensions[i],
        };
        size_t stride = 0;

        g_assert_true(pgraph_calculate_texture_cubemap_face_stride(
            &shape, true, 4, &stride));
        g_assert_cmpuint(stride, ==, NV2A_CUBEMAP_FACE_ALIGNMENT);

        g_autofree uint8_t *source = g_malloc0(stride * 6);
        for (unsigned int face = 0; face < 6; face++) {
            source[face * stride] = face + 1;
        }
        for (unsigned int face = 0; face < 6; face++) {
            g_assert_cmpuint(source[face * stride], ==, face + 1);
        }
    }
}

static void test_other_cubemap_formats(void)
{
    const unsigned int compressed_formats[] = {
        NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT23_A8R8G8B8,
        NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT45_A8R8G8B8,
    };

    for (size_t i = 0; i < ARRAY_SIZE(compressed_formats); i++) {
        TextureShape shape = {
            .cubemap = true,
            .dimensionality = 2,
            .color_format = compressed_formats[i],
            .levels = 1,
            .storage_levels = 1,
            .width = 16,
            .height = 16,
        };
        size_t stride = 0;

        g_assert_true(pgraph_calculate_texture_cubemap_face_stride(
            &shape, true, 4, &stride));
        g_assert_cmpuint(stride, ==, 2 * NV2A_CUBEMAP_FACE_ALIGNMENT);
    }

    TextureShape rgba = {
        .cubemap = true,
        .dimensionality = 2,
        .color_format = NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8R8G8B8,
        .levels = 1,
        .storage_levels = 1,
        .width = 8,
        .height = 8,
    };
    size_t stride = 0;

    g_assert_true(pgraph_calculate_texture_cubemap_face_stride(
        &rgba, false, 4, &stride));
    g_assert_cmpuint(stride, ==, 2 * NV2A_CUBEMAP_FACE_ALIGNMENT);
}

static void test_face_stride_uses_declared_storage_levels(void)
{
    TextureShape shape = {
        .cubemap = true,
        .dimensionality = 2,
        .color_format = NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT1_A1R5G5B5,
        .levels = 1,
        .storage_levels = 4,
        .width = 8,
        .height = 8,
        .border = true,
    };
    size_t stride = 0;

    g_assert_true(pgraph_calculate_texture_cubemap_face_stride(
        &shape, true, 4, &stride));
    g_assert_cmpuint(stride, ==, 2 * NV2A_CUBEMAP_FACE_ALIGNMENT);
}

static void test_cubemap_span_info_separates_sampled_levels(void)
{
    TextureShape shape = {
        .cubemap = true,
        .dimensionality = 2,
        .color_format = NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT1_A1R5G5B5,
        .levels = 1,
        .storage_levels = 4,
        .width = 8,
        .height = 8,
        .border = true,
    };
    PGRAPHTextureCubemapSpan span = { 0 };

    g_assert_true(pgraph_calculate_texture_cubemap_span(
        &shape, true, 4, &span));
    g_assert_cmpuint(span.storage_face_stride, ==,
                     2 * NV2A_CUBEMAP_FACE_ALIGNMENT);
    g_assert_cmpuint(span.sampled_face_stride, ==,
                     NV2A_CUBEMAP_FACE_ALIGNMENT);
    g_assert_cmpuint(span.storage_span, ==,
                     12 * NV2A_CUBEMAP_FACE_ALIGNMENT);
    g_assert_cmpuint(span.sampled_span, ==,
                     6 * NV2A_CUBEMAP_FACE_ALIGNMENT);
    g_assert_cmpuint(span.extra_span, ==,
                     6 * NV2A_CUBEMAP_FACE_ALIGNMENT);

    shape.levels = shape.storage_levels;
    g_assert_true(pgraph_calculate_texture_cubemap_span(
        &shape, true, 4, &span));
    g_assert_cmpuint(span.storage_face_stride, ==, span.sampled_face_stride);
    g_assert_cmpuint(span.storage_span, ==, span.sampled_span);
    g_assert_cmpuint(span.extra_span, ==, 0);
}

static void test_face_stride_rejects_invalid_shapes(void)
{
    TextureShape shape = {
        .dimensionality = 2,
        .levels = 1,
        .storage_levels = 1,
        .width = 4,
        .height = 4,
    };
    size_t stride = 0;

    g_assert_false(pgraph_calculate_texture_cubemap_face_stride(
        &shape, true, 4, &stride));
    shape.cubemap = true;
    shape.dimensionality = 3;
    g_assert_false(pgraph_calculate_texture_cubemap_face_stride(
        &shape, true, 4, &stride));
    shape.dimensionality = 2;
    shape.storage_levels = 0;
    shape.levels = 0;
    g_assert_false(pgraph_calculate_texture_cubemap_face_stride(
        &shape, true, 4, &stride));
    g_assert_false(pgraph_calculate_texture_cubemap_face_stride(
        NULL, true, 4, &stride));
    g_assert_false(pgraph_calculate_texture_cubemap_face_stride(
        &shape, true, 4, NULL));

    shape.levels = 1;
    shape.storage_levels = 1;
    shape.width = UINT_MAX;
    shape.height = 1000000000U;
    g_assert_false(pgraph_calculate_texture_cubemap_face_stride(
        &shape, false, 1, &stride));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/nv2a/texture-layout/bordered-mip-crop",
                    test_bordered_mip_crop);
    g_test_add_func("/xbox/nv2a/texture-layout/bordered-subblock-crop",
                    test_bordered_mip_crop_clamps_subblock_tails);
    g_test_add_func("/xbox/nv2a/texture-layout/subblock-face-stride",
                    test_compressed_subblock_face_stride);
    g_test_add_func("/xbox/nv2a/texture-layout/other-formats",
                    test_other_cubemap_formats);
    g_test_add_func("/xbox/nv2a/texture-layout/storage-level-stride",
                    test_face_stride_uses_declared_storage_levels);
    g_test_add_func("/xbox/nv2a/texture-layout/cubemap-span-info",
                    test_cubemap_span_info_separates_sampled_levels);
    g_test_add_func("/xbox/nv2a/texture-layout/invalid-shapes",
                    test_face_stride_rejects_invalid_shapes);
    return g_test_run();
}

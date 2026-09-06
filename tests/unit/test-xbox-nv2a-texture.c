/* Focused tests for the NV2A encoded texture source layout. */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/texture-layout.h"

const BasicColorFormatInfo kelvin_color_format_info_map[66] = {
    [NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT1_A1R5G5B5] = { 4, false },
    [NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT23_A8R8G8B8] = { 4, false },
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8R8G8B8] = { 4, false },
    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8R8G8B8] = { 4, true },
};

static void test_bordered_bc2(void)
{
    TextureShape shape = {
        .dimensionality = 2,
        .color_format = NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT23_A8R8G8B8,
        .levels = 1,
        .width = 8,
        .height = 8,
        .border = true,
    };
    size_t size;

    g_assert_true(pgraph_calculate_texture_encoded_size(shape, true, 4,
                                                        &size));
    g_assert_cmpuint(size, ==, 256);
}

static void test_bordered_mip_crop_tracks_logical_extent(void)
{
    const unsigned int stored[] = { 16, 8, 4, 2, 1 };
    const unsigned int logical[] = { 8, 4, 2, 1, 1 };
    const unsigned int skip[] = { 4, 2, 1, 0, 0 };

    for (unsigned int level = 0; level < ARRAY_SIZE(stored); level++) {
        PGRAPHTextureMipCrop crop = pgraph_bordered_texture_mip_crop(
            8, 8, stored[level], stored[level], level);

        g_assert_cmpuint(crop.width, ==, logical[level]);
        g_assert_cmpuint(crop.height, ==, logical[level]);
        g_assert_cmpuint(crop.skip_pixels, ==, skip[level]);
        g_assert_cmpuint(crop.skip_rows, ==, skip[level]);
    }
}

static void test_bordered_mip_crop_clamps_sub_block_tails(void)
{
    const unsigned int base[] = { 1, 2, 4 };
    const unsigned int levels[] = { 1, 2, 3 };

    for (unsigned int i = 0; i < ARRAY_SIZE(base); i++) {
        unsigned int stored = 16;
        unsigned int logical = base[i];

        for (unsigned int level = 0; level < levels[i]; level++) {
            PGRAPHTextureMipCrop crop = pgraph_bordered_texture_mip_crop(
                base[i], base[i], stored, stored, level);
            unsigned int skip = level < 3 ? 4U >> level : 0;

            g_assert_cmpuint(crop.width, ==, logical);
            g_assert_cmpuint(crop.height, ==, logical);
            g_assert_cmpuint(crop.skip_pixels, ==, skip);
            g_assert_cmpuint(crop.skip_rows, ==, skip);
            stored /= 2;
            logical = MAX(logical / 2, 1U);
        }
    }
}

static void test_ordinary_mips(void)
{
    TextureShape shape = {
        .dimensionality = 2,
        .color_format = NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8R8G8B8,
        .levels = 3,
        .width = 8,
        .height = 8,
    };
    size_t size;

    g_assert_true(pgraph_calculate_texture_encoded_size(shape, false, 4,
                                                        &size));
    g_assert_cmpuint(size, ==, 336);
}

static void test_3d_depth_halves(void)
{
    TextureShape shape = {
        .dimensionality = 3,
        .color_format = NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8R8G8B8,
        .levels = 3,
        .width = 8,
        .height = 8,
        .depth = 4,
    };
    size_t size;

    g_assert_true(pgraph_calculate_texture_encoded_size(shape, false, 4,
                                                        &size));
    g_assert_cmpuint(size, ==, 1168);
}

static void test_overflow_rejected(void)
{
    TextureShape shape = {
        .dimensionality = 2,
        .color_format = NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT23_A8R8G8B8,
        .levels = 1,
        .width = UINT_MAX,
        .height = UINT_MAX,
    };
    size_t size;

    g_assert_false(pgraph_calculate_texture_encoded_size(shape, true, 4,
                                                         &size));
}

static void test_cubemap_face_alignment(void)
{
    TextureShape shape = {
        .cubemap = true,
        .dimensionality = 2,
        .color_format = NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT23_A8R8G8B8,
        .levels = 1,
        .width = 8,
        .height = 8,
    };
    size_t size;

    g_assert_true(pgraph_calculate_texture_encoded_size(shape, true, 4,
                                                        &size));
    g_assert_cmpuint(size % NV2A_CUBEMAP_FACE_ALIGNMENT, ==, 0);
    g_assert_cmpuint(size, ==,
                     ROUND_UP(64, NV2A_CUBEMAP_FACE_ALIGNMENT) * 6);
}

static void test_cubemap_face_stride_uses_declared_storage_levels(void)
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
    size_t size;

    g_assert_true(pgraph_calculate_texture_encoded_size(shape, true, 4,
                                                        &size));
    g_assert_cmpuint(size, ==, 256 * 6);
}

static void test_dma_range_boundaries(void)
{
    /* Last source byte equals the inclusive DMA limit and the exclusive
     * VRAM end. */
    g_assert_true(pgraph_texture_dma_range_valid(
        0x1000, 0x10, 0xff0, 0xfff, 0x2000));

    g_assert_false(pgraph_texture_dma_range_valid(
        0x1000, 0x10, 0xff1, 0x1000, 0x2000));
    g_assert_false(pgraph_texture_dma_range_valid(
        0x1000, 0x10, 0xff0, 0xffe, 0x2000));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/nv2a/texture/bordered-bc2", test_bordered_bc2);
    g_test_add_func("/xbox/nv2a/texture/bordered-mip-crop",
                    test_bordered_mip_crop_tracks_logical_extent);
    g_test_add_func("/xbox/nv2a/texture/bordered-mip-sub-block",
                    test_bordered_mip_crop_clamps_sub_block_tails);
    g_test_add_func("/xbox/nv2a/texture/ordinary-mips", test_ordinary_mips);
    g_test_add_func("/xbox/nv2a/texture/3d-depth-halves",
                    test_3d_depth_halves);
    g_test_add_func("/xbox/nv2a/texture/overflow", test_overflow_rejected);
    g_test_add_func("/xbox/nv2a/texture/cubemap-alignment",
                    test_cubemap_face_alignment);
    g_test_add_func("/xbox/nv2a/texture/cubemap-storage-level-stride",
                    test_cubemap_face_stride_uses_declared_storage_levels);
    g_test_add_func("/xbox/nv2a/texture/dma-range-boundaries",
                    test_dma_range_boundaries);
    return g_test_run();
}

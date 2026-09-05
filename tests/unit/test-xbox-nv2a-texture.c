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

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/nv2a/texture/bordered-bc2", test_bordered_bc2);
    g_test_add_func("/xbox/nv2a/texture/ordinary-mips", test_ordinary_mips);
    g_test_add_func("/xbox/nv2a/texture/3d-depth-halves",
                    test_3d_depth_halves);
    g_test_add_func("/xbox/nv2a/texture/overflow", test_overflow_rejected);
    g_test_add_func("/xbox/nv2a/texture/cubemap-alignment",
                    test_cubemap_face_alignment);
    return g_test_run();
}

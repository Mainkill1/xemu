/*
 * Packed R6G5B5 textures require conversion before host image sampling.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/texture.c"

static void test_packed_depth_source_needs_conversion(void)
{
    SurfaceBinding source = {
        .width = 2,
        .height = 2,
        .swizzle = true,
        .color = false,
        .host_fmt = { .host_bytes_per_pixel = 2 },
    };
    TextureShape texture = {
        .width = 2,
        .height = 2,
        .levels = 1,
        .color_format = NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R6G5B5,
    };

    g_assert_false(check_surface_to_texture_compatiblity(&source, &texture));
}

static void test_expanded_color_source_needs_conversion(void)
{
    SurfaceBinding source = {
        .width = 2,
        .height = 2,
        .swizzle = true,
        .color = true,
        .host_fmt = { .host_bytes_per_pixel = 4 },
    };
    TextureShape texture = {
        .width = 2,
        .height = 2,
        .levels = 1,
        .color_format = NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R6G5B5,
    };

    g_assert_false(check_surface_to_texture_compatiblity(&source, &texture));
    /* An ordinary matching 32-bit color format retains its copy path. */
    texture.color_format = NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8R8G8B8;
    g_assert_true(check_surface_to_texture_compatiblity(&source, &texture));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/vk/r6g5b5/depth-source-conversion",
                    test_packed_depth_source_needs_conversion);
    g_test_add_func("/xbox/vk/r6g5b5/color-source-conversion",
                    test_expanded_color_source_needs_conversion);
    return g_test_run();
}

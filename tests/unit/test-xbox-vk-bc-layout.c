/*
 * NV2A Vulkan block-compressed texture layout tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/xbox/nv2a/nv2a_regs.h"
#include "hw/xbox/nv2a/pgraph/vk/bc-layout.h"

static void test_small_mips_use_one_physical_block(void)
{
    g_assert_cmpuint(pgraph_vk_bc_mip_size(1, 1, 8), ==, 8);
    g_assert_cmpuint(pgraph_vk_bc_mip_size(2, 3, 16), ==, 16);
    g_assert_cmpuint(pgraph_vk_bc_mip_size(5, 7, 8), ==, 32);
}

static void test_mip_chain_keeps_tail_blocks(void)
{
    /* 8x8, 4x4, 2x2 and 1x1 consume 4, 1, 1 and 1 DXT1 blocks. */
    g_assert_cmpuint(pgraph_vk_bc_layer_size(8, 8, 4, 8), ==, 56);
}

static void test_cube_face_alignment(void)
{
    size_t face_size = pgraph_vk_bc_layer_size(8, 8, 4, 8);

    g_assert_cmpuint(ROUND_UP(face_size, NV2A_CUBEMAP_FACE_ALIGNMENT), ==,
                     NV2A_CUBEMAP_FACE_ALIGNMENT);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/vulkan/bc-layout/small-mips",
                    test_small_mips_use_one_physical_block);
    g_test_add_func("/xbox/vulkan/bc-layout/mip-chain",
                    test_mip_chain_keeps_tail_blocks);
    g_test_add_func("/xbox/vulkan/bc-layout/cube-alignment",
                    test_cube_face_alignment);
    return g_test_run();
}

/*
 * Focused tests for the ordered vertex staging range contract.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/vertex-staging.h"

static void test_staging_capacity_boundaries(void)
{
    const uint64_t cap = PGRAPH_VK_VERTEX_RAM_STAGING_MAX_SIZE;
    uint64_t offset;

    g_assert_true(pgraph_vk_vertex_staging_reserve(0, cap - 1, cap, 4,
                                                   &offset));
    g_assert_cmpuint(offset, ==, 0);
    g_assert_true(pgraph_vk_vertex_staging_reserve(0, cap, cap, 4,
                                                   &offset));
    g_assert_false(pgraph_vk_vertex_staging_reserve(0, cap + 1, cap, 4,
                                                    &offset));
}

static void test_staging_aggregate_exhaustion_and_reset(void)
{
    const uint64_t cap = PGRAPH_VK_VERTEX_RAM_STAGING_MAX_SIZE;
    const uint64_t half = cap / 2;
    uint64_t offset;

    g_assert_true(pgraph_vk_vertex_staging_reserve(0, half, cap, 4, &offset));
    g_assert_true(pgraph_vk_vertex_staging_reserve(half, half, cap, 4,
                                                   &offset));
    g_assert_false(pgraph_vk_vertex_staging_reserve(cap, 4, cap, 4, &offset));

    /* pgraph_vk_finish resets the ring before retrying a reservation. */
    g_assert_true(pgraph_vk_vertex_staging_reserve(0, half, cap, 4, &offset));
}

static void test_staging_vram_end_boundary(void)
{
    const uint64_t vram_size = 32 * 1024 * 1024;
    uint64_t offset;

    g_assert_true(pgraph_vk_vertex_staging_range_valid(vram_size - 4, 4,
                                                       vram_size));
    g_assert_false(pgraph_vk_vertex_staging_range_valid(vram_size - 4, 5,
                                                        vram_size));
    g_assert_false(pgraph_vk_vertex_staging_range_valid(vram_size, 1,
                                                        vram_size));
    g_assert_false(pgraph_vk_vertex_staging_reserve(UINT64_MAX, 0,
                                                    UINT64_MAX, 4, &offset));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/nv2a/vertex/staging-capacity-boundaries",
                    test_staging_capacity_boundaries);
    g_test_add_func("/xbox/nv2a/vertex/staging-aggregate-reset",
                    test_staging_aggregate_exhaustion_and_reset);
    g_test_add_func("/xbox/nv2a/vertex/staging-vram-end-boundary",
                    test_staging_vram_end_boundary);
    return g_test_run();
}

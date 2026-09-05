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

    g_assert_true(pgraph_vk_vertex_staging_growth_size(cap / 2, cap,
                                                       &offset));
    g_assert_cmpuint(offset, ==, cap);
    g_assert_false(pgraph_vk_vertex_staging_growth_size(cap, cap + 1,
                                                        &offset));
}

static void test_staging_multi_element_padding(void)
{
    const uint64_t sizes[] = { 3, 5 };
    uint64_t start, end;

    g_assert_true(pgraph_vk_vertex_staging_plan_append(
        0, sizes, ARRAY_SIZE(sizes), 16, 4, &start, &end));
    g_assert_cmpuint(start, ==, 0);
    g_assert_cmpuint(end, ==, 9);

    {
        const uint64_t exact[] = { 4, 8, 4 };
        g_assert_true(pgraph_vk_vertex_staging_plan_append(
            0, exact, ARRAY_SIZE(exact), 16, 4, &start, &end));
        g_assert_cmpuint(end, ==, 16);
    }

    {
        const uint64_t overflow[] = { UINT64_MAX, 1 };
        g_assert_false(pgraph_vk_vertex_staging_plan_append(
            0, overflow, ARRAY_SIZE(overflow), UINT64_MAX, 4, &start, &end));
    }
}

static void test_vertex_update_plans(void)
{
    const uint64_t cap = PGRAPH_VK_VERTEX_RAM_STAGING_MAX_SIZE;

    g_assert_cmpint(pgraph_vk_vertex_update_plan(true, true, 0, 4, cap, 0,
                                                 cap), ==,
                    PGRAPH_VK_VERTEX_UPDATE_STAGE);
    g_assert_cmpint(pgraph_vk_vertex_update_plan(true, true, 0, 4, cap,
                                                 cap - 2, cap), ==,
                    PGRAPH_VK_VERTEX_UPDATE_FINISH_RETRY);
    g_assert_cmpint(pgraph_vk_vertex_update_plan(true, true, 0, cap, cap, 0,
                                                 cap / 2), ==,
                    PGRAPH_VK_VERTEX_UPDATE_FINISH_RETRY);
    g_assert_cmpint(pgraph_vk_vertex_update_plan(true, true, 0, cap + 1, cap,
                                                 0, cap), ==,
                    PGRAPH_VK_VERTEX_UPDATE_FINISH_DIRECT);
    g_assert_cmpint(pgraph_vk_vertex_update_plan(true, false, 0, 4, cap, 0,
                                                 cap), ==,
                    PGRAPH_VK_VERTEX_UPDATE_FINISH_DIRECT);
    g_assert_cmpint(pgraph_vk_vertex_update_plan(false, true, 0, cap, cap, cap,
                                                 cap), ==,
                    PGRAPH_VK_VERTEX_UPDATE_DIRECT);
    g_assert_cmpint(pgraph_vk_vertex_update_plan(true, true, cap - 1, 2, cap,
                                                 0, cap), ==,
                    PGRAPH_VK_VERTEX_UPDATE_REJECT);
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
    g_test_add_func("/xbox/nv2a/vertex/staging-multi-element-padding",
                    test_staging_multi_element_padding);
    g_test_add_func("/xbox/nv2a/vertex/staging-aggregate-reset",
                    test_staging_aggregate_exhaustion_and_reset);
    g_test_add_func("/xbox/nv2a/vertex/staging-vram-end-boundary",
                    test_staging_vram_end_boundary);
    g_test_add_func("/xbox/nv2a/vertex/update-plans", test_vertex_update_plans);
    return g_test_run();
}

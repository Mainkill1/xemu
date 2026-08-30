/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/xbox/nv2a/pgraph/uniform-dirty.h"
#include "hw/xbox/nv2a/pgraph/vk/buffer-layout.h"
#include "hw/xbox/nv2a/pgraph/vk/glsl.h"
#include "hw/xbox/nv2a/pgraph/vk/surface-compute.h"
#include "hw/xbox/nv2a/pgraph/vk/vertex-range.h"

static void test_buffer_layout_accounts_for_each_alignment(void)
{
    const uint64_t sizes[] = { 3, 3 };
    uint64_t required_size;

    g_assert_true(pgraph_vk_buffer_layout_required_size(
        7, sizes, G_N_ELEMENTS(sizes), 8, &required_size));
    g_assert_cmpuint(required_size, ==, 19);
}

static void test_buffer_layout_rejects_overflow(void)
{
    const uint64_t size = 2;
    uint64_t required_size;

    g_assert_false(pgraph_vk_buffer_layout_required_size(
        UINT64_MAX, &size, 1, 8, &required_size));
}

static void test_buffer_image_size_checks_multiplication(void)
{
    uint64_t size;

    g_assert_true(pgraph_vk_buffer_image_size(1920, 1080, 4, &size));
    g_assert_cmpuint(size, ==, 8294400);
    g_assert_false(pgraph_vk_buffer_image_size(
        UINT64_MAX, 2, 4, &size));
    g_assert_false(pgraph_vk_buffer_checked_add(UINT64_MAX, 1, &size));
    g_assert_true(pgraph_vk_buffer_checked_align_up(5, 4, &size));
    g_assert_cmpuint(size, ==, 8);
    g_assert_false(pgraph_vk_buffer_checked_align_up(UINT64_MAX, 4, &size));
    g_assert_false(pgraph_vk_buffer_checked_align_up(1, 0, &size));
}

static void test_buffer_growth_is_geometric(void)
{
    const size_t mib = 1024 * 1024;

    g_assert_cmpuint(pgraph_vk_buffer_growth_target(8 * mib, 8 * mib,
                                                    8 * mib + 1),
                     ==, 16 * mib);
    g_assert_cmpuint(pgraph_vk_buffer_growth_target(16 * mib, 8 * mib,
                                                    128 * 1024),
                     ==, 16 * mib);
    g_assert_cmpuint(pgraph_vk_buffer_growth_target(0, 8 * mib, 1),
                     ==, 8 * mib);
    g_assert_cmpuint(pgraph_vk_buffer_growth_target(0, 0, 9),
                     ==, 16);
    g_assert_cmpuint(pgraph_vk_buffer_growth_target_bounded(
                         64, 8, 65, 100),
                     ==, 65);
    g_assert_cmpuint(pgraph_vk_buffer_growth_target_bounded(
                         64, 8, 101, 100),
                     ==, 0);
}

static void test_compute_workgroup_respects_both_limits(void)
{
    g_assert_cmpuint(pgraph_vk_compute_workgroup_size(1024, 256, 1024),
                     ==, 256);
    g_assert_cmpuint(pgraph_vk_compute_workgroup_size(1024, 1024, 64),
                     ==, 64);
    g_assert_cmpuint(pgraph_vk_compute_workgroup_size(1000, 256, 256),
                     ==, 8);
}

static void test_compact_vertex_range_preserves_mixed_addresses(void)
{
    const uint32_t min_vertex = 1000;
    const uint32_t original_index = 1002;
    const uint64_t direct_attribute_offset = 100;
    const uint64_t direct_stride = 12;
    const uint64_t remapped_attribute_offset = 200;
    const uint64_t remapped_stride = 16;
    uint64_t direct_base_offset;

    g_assert_true(pgraph_vk_vertex_base_offset(
        direct_attribute_offset, direct_stride, min_vertex,
        &direct_base_offset));

    uint32_t rebased_index = original_index - min_vertex;
    g_assert_cmpuint(direct_base_offset + rebased_index * direct_stride,
                     ==, direct_attribute_offset +
                             original_index * direct_stride);
    g_assert_cmpuint(remapped_attribute_offset +
                         rebased_index * remapped_stride,
                     ==, 232);

    uint32_t indexed_base = pgraph_vk_indexed_base_vertex(min_vertex);
    int32_t indexed_offset = pgraph_vk_indexed_vertex_offset(indexed_base);
    g_assert_cmpint((int64_t)original_index + indexed_offset, ==,
                    rebased_index);
    g_assert_cmpuint(pgraph_vk_indexed_base_vertex(UINT32_MAX), ==, 0);
}

static void test_changed_value_dirties_row(void)
{
    uint32_t rows[2][4] = { 0 };
    bool dirty_rows[2] = { false };

    pgraph_uniform_u32_row_update(rows, dirty_rows, 1, 2, 0x12345678);

    g_assert_cmphex(rows[1][2], ==, 0x12345678);
    g_assert_true(dirty_rows[1]);
    g_assert_false(dirty_rows[0]);
}

static void test_identical_value_stays_clean(void)
{
    uint32_t rows[2][4] = {
        { 0 },
        { 0, 0, 0x12345678, 0 },
    };
    bool dirty_rows[2] = { false };

    pgraph_uniform_u32_row_update(rows, dirty_rows, 1, 2, 0x12345678);

    g_assert_false(dirty_rows[1]);
}

static void test_identical_value_preserves_prior_dirty(void)
{
    uint32_t rows[2][4] = {
        { 0 },
        { 0, 0, 0x12345678, 0 },
    };
    bool dirty_rows[2] = { false, true };

    pgraph_uniform_u32_row_update(rows, dirty_rows, 1, 2, 0x12345678);

    g_assert_true(dirty_rows[1]);
}

static void test_post_load_invalidation_marks_all_rows_dirty(void)
{
    bool dirty_rows[4] = { false };

    pgraph_uniform_dirty_rows_invalidate(dirty_rows,
                                         G_N_ELEMENTS(dirty_rows));

    for (unsigned int row = 0; row < G_N_ELEMENTS(dirty_rows); row++) {
        g_assert_true(dirty_rows[row]);
    }
}

static ShaderUniformLayout make_test_layout(ShaderUniform *uniform,
                                            uint32_t allocation[][4],
                                            size_t row_count)
{
    *uniform = (ShaderUniform) {
        .name = "rows",
        .dim_v = 4,
        .dim_a = row_count,
        .align = 16,
        .stride = 16,
        .offset = 0,
    };
    return (ShaderUniformLayout) {
        .uniforms = uniform,
        .num_uniforms = 1,
        .total_size = row_count * sizeof(allocation[0]),
        .allocation = allocation,
    };
}

static void test_uniform_copy_reports_real_changes(void)
{
    uint32_t allocation[2][4] = { 0 };
    uint32_t values[2][4] = {
        { 1, 2, 3, 4 },
        { 5, 6, 7, 8 },
    };
    ShaderUniform uniform;
    ShaderUniformLayout layout =
        make_test_layout(&uniform, allocation, G_N_ELEMENTS(allocation));

    g_assert_true(uniform_copy(&layout, 1, values, sizeof(uint32_t), 8));
    g_assert_cmpmem(allocation, sizeof(allocation), values, sizeof(values));
    g_assert_false(uniform_copy(&layout, 1, values, sizeof(uint32_t), 8));
}

static void test_uniform_array_copy_is_row_scoped(void)
{
    uint32_t allocation[2][4] = {
        { 1, 2, 3, 4 },
        { 5, 6, 7, 8 },
    };
    uint32_t replacement[4] = { 9, 10, 11, 12 };
    const uint32_t first_row[4] = { 1, 2, 3, 4 };
    ShaderUniform uniform;
    ShaderUniformLayout layout =
        make_test_layout(&uniform, allocation, G_N_ELEMENTS(allocation));

    g_assert_true(uniform_copy_array_element(
        &layout, 1, 1, replacement, sizeof(uint32_t)));
    g_assert_cmpmem(allocation[0], sizeof(allocation[0]), first_row,
                    sizeof(first_row));
    g_assert_cmpmem(allocation[1], sizeof(allocation[1]), replacement,
                    sizeof(replacement));
    g_assert_false(uniform_copy_array_element(
        &layout, 1, 1, replacement, sizeof(uint32_t)));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/pgraph/uniform-dirty/changed",
                    test_changed_value_dirties_row);
    g_test_add_func("/xbox/pgraph/uniform-dirty/identical-clean",
                    test_identical_value_stays_clean);
    g_test_add_func("/xbox/pgraph/uniform-dirty/identical-prior-dirty",
                    test_identical_value_preserves_prior_dirty);
    g_test_add_func("/xbox/pgraph/uniform-dirty/post-load-invalidate",
                    test_post_load_invalidation_marks_all_rows_dirty);
    g_test_add_func("/xbox/vulkan/uniform-copy/change-detection",
                    test_uniform_copy_reports_real_changes);
    g_test_add_func("/xbox/vulkan/uniform-copy/row-scope",
                    test_uniform_array_copy_is_row_scoped);
    g_test_add_func("/xbox/vulkan/buffer-layout/per-item-alignment",
                    test_buffer_layout_accounts_for_each_alignment);
    g_test_add_func("/xbox/vulkan/buffer-layout/overflow",
                    test_buffer_layout_rejects_overflow);
    g_test_add_func("/xbox/vulkan/buffer-layout/image-size",
                    test_buffer_image_size_checks_multiplication);
    g_test_add_func("/xbox/vulkan/buffer-layout/geometric-growth",
                    test_buffer_growth_is_geometric);
    g_test_add_func("/xbox/vulkan/compute/workgroup-limits",
                    test_compute_workgroup_respects_both_limits);
    g_test_add_func("/xbox/vulkan/vertex-range/mixed-attributes-indexed",
                    test_compact_vertex_range_preserves_mixed_addresses);

    return g_test_run();
}

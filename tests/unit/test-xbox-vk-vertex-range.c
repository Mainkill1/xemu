/*
 * Vulkan NV2A vertex RAM page range tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/vertex-range.h"

static void test_clean_page_use_has_no_unrelated_conflict(void)
{
    PGRAPHVkVertexPageRange active;
    PGRAPHVkVertexPageRange rewrite;

    g_assert_true(pgraph_vk_vertex_page_range_for_bytes(
        2 * 4096, 64, 16 * 4096, 4096, &active));
    g_assert_cmpuint(active.start_page, ==, 2);
    g_assert_cmpuint(active.num_pages, ==, 1);

    g_assert_true(pgraph_vk_vertex_page_range_for_bytes(
        3 * 4096, 4, 16 * 4096, 4096, &rewrite));
    g_assert_false(pgraph_vk_vertex_page_ranges_overlap(active, rewrite));
}

static void test_unaligned_multi_page_range(void)
{
    PGRAPHVkVertexPageRange range;
    uint64_t offset;
    uint64_t size;
    size_t start_bit;
    size_t nbits;

    g_assert_true(pgraph_vk_vertex_page_range_for_bytes(
        4095, 2, 16 * 4096, 4096, &range));
    g_assert_cmpuint(range.start_page, ==, 0);
    g_assert_cmpuint(range.num_pages, ==, 2);

    g_assert_true(pgraph_vk_vertex_page_range_to_bytes(
        &range, 4096, &offset, &size));
    g_assert_cmpuint(offset, ==, 0);
    g_assert_cmpuint(size, ==, 8192);

    g_assert_true(pgraph_vk_vertex_page_range_to_bitmap(
        &range, 16, &start_bit, &nbits));
    g_assert_cmpuint(start_bit, ==, 0);
    g_assert_cmpuint(nbits, ==, 2);
}

static void test_attribute_read_span_normal_stride(void)
{
    uint64_t span;

    g_assert_true(pgraph_vk_vertex_attribute_read_span(
        4, 16, 12, &span));
    g_assert_cmpuint(span, ==, 60);
}

static void test_attribute_read_span_stride_less_than_element(void)
{
    uint64_t span;
    PGRAPHVkVertexPageRange range;

    g_assert_true(pgraph_vk_vertex_attribute_read_span(
        2, 4, 16, &span));
    g_assert_cmpuint(span, ==, 20);

    g_assert_true(pgraph_vk_vertex_page_range_for_bytes(
        4090, span, 8192, 4096, &range));
    g_assert_cmpuint(range.start_page, ==, 0);
    g_assert_cmpuint(range.num_pages, ==, 2);
}

static void test_attribute_read_span_one_element(void)
{
    uint64_t span;

    g_assert_true(pgraph_vk_vertex_attribute_read_span(
        1, 4096, 24, &span));
    g_assert_cmpuint(span, ==, 24);
}

static void test_overflow_and_bounds_rejected(void)
{
    PGRAPHVkVertexPageRange range;
    uint64_t offset;
    uint64_t size;
    uint64_t span;
    size_t start_bit;
    size_t nbits;

    g_assert_false(pgraph_vk_vertex_page_range_for_bytes(
        0, 0, 4096, 4096, &range));
    g_assert_false(pgraph_vk_vertex_page_range_for_bytes(
        0, 1, 4096, 0, &range));
    g_assert_false(pgraph_vk_vertex_page_range_for_bytes(
        4096, 1, 4096, 4096, &range));
    g_assert_false(pgraph_vk_vertex_page_range_for_bytes(
        4090, 8, 4096, 4096, &range));
    g_assert_false(pgraph_vk_vertex_page_range_for_bytes(
        UINT64_MAX - 1, 4, UINT64_MAX, 4096, &range));

    range = (PGRAPHVkVertexPageRange) {
        .start_page = UINT64_MAX / 4096 + 1,
        .num_pages = 1,
    };
    g_assert_false(pgraph_vk_vertex_page_range_to_bytes(
        &range, 4096, &offset, &size));

    range = (PGRAPHVkVertexPageRange) {
        .start_page = UINT64_MAX / 4096,
        .num_pages = 2,
    };
    g_assert_false(pgraph_vk_vertex_page_range_to_bytes(
        &range, 4096, &offset, &size));

    range = (PGRAPHVkVertexPageRange) {
        .start_page = 15,
        .num_pages = 2,
    };
    g_assert_false(pgraph_vk_vertex_page_range_to_bitmap(
        &range, 16, &start_bit, &nbits));

    g_assert_false(pgraph_vk_vertex_attribute_read_span(
        0, 16, 4, &span));
    g_assert_false(pgraph_vk_vertex_attribute_read_span(
        1, 16, 0, &span));
    g_assert_false(pgraph_vk_vertex_attribute_read_span(
        UINT64_MAX, 2, 4, &span));
    g_assert_false(pgraph_vk_vertex_attribute_read_span(
        2, UINT64_MAX, 2, &span));
}

static void test_late_active_mark_model(void)
{
    PGRAPHVkVertexPageRange validated;
    size_t start_bit;
    size_t nbits;

    /*
     * Model the draw-side ordering contract: the page range can be validated
     * and retained before finish-capable work, then converted to bitmap bits
     * immediately before the vertex buffer bind records.
     */
    g_assert_true(pgraph_vk_vertex_page_range_for_bytes(
        4096 + 32, 48, 16 * 4096, 4096, &validated));
    g_assert_true(pgraph_vk_vertex_page_range_to_bitmap(
        &validated, 16, &start_bit, &nbits));
    g_assert_cmpuint(start_bit, ==, 1);
    g_assert_cmpuint(nbits, ==, 1);
}

static void test_conflict_overlap(void)
{
    PGRAPHVkVertexPageRange active;
    PGRAPHVkVertexPageRange rewrite;

    g_assert_true(pgraph_vk_vertex_page_range_for_bytes(
        100, 1, 16 * 4096, 4096, &active));
    g_assert_true(pgraph_vk_vertex_page_range_for_bytes(
        4095, 1, 16 * 4096, 4096, &rewrite));
    g_assert_true(pgraph_vk_vertex_page_ranges_overlap(active, rewrite));

    active = (PGRAPHVkVertexPageRange) {
        .start_page = 2,
        .num_pages = 2,
    };
    rewrite = (PGRAPHVkVertexPageRange) {
        .start_page = 3,
        .num_pages = 1,
    };
    g_assert_true(pgraph_vk_vertex_page_ranges_overlap(active, rewrite));

    rewrite = (PGRAPHVkVertexPageRange) {
        .start_page = 4,
        .num_pages = 1,
    };
    g_assert_false(pgraph_vk_vertex_page_ranges_overlap(active, rewrite));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/vk-vertex-range/clean-page-use",
                    test_clean_page_use_has_no_unrelated_conflict);
    g_test_add_func("/xbox/vk-vertex-range/unaligned-multi-page",
                    test_unaligned_multi_page_range);
    g_test_add_func("/xbox/vk-vertex-range/span-normal-stride",
                    test_attribute_read_span_normal_stride);
    g_test_add_func("/xbox/vk-vertex-range/span-stride-less-than-element",
                    test_attribute_read_span_stride_less_than_element);
    g_test_add_func("/xbox/vk-vertex-range/span-one-element",
                    test_attribute_read_span_one_element);
    g_test_add_func("/xbox/vk-vertex-range/overflow-bounds",
                    test_overflow_and_bounds_rejected);
    g_test_add_func("/xbox/vk-vertex-range/late-active-mark-model",
                    test_late_active_mark_model);
    g_test_add_func("/xbox/vk-vertex-range/conflict",
                    test_conflict_overlap);
    return g_test_run();
}

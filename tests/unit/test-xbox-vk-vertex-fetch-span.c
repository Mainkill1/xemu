#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/vertex-fetch-span.h"

static void test_fetch_span(void)
{
    uint64_t start = UINT64_MAX, size = UINT64_MAX;

    g_assert_true(pgraph_vk_vertex_fetch_span(0, 0, 16, 12,
                                               &start, &size));
    g_assert_cmpuint(start, ==, 0);
    g_assert_cmpuint(size, ==, 12);
    g_assert_true(pgraph_vk_vertex_fetch_span(2, 4, 16, 12,
                                               &start, &size));
    g_assert_cmpuint(start, ==, 32);
    g_assert_cmpuint(size, ==, 44);
    g_assert_true(pgraph_vk_vertex_fetch_span(2, 4, 0, 12,
                                               &start, &size));
    g_assert_cmpuint(start, ==, 0);
    g_assert_cmpuint(size, ==, 12);
    g_assert_true(pgraph_vk_vertex_fetch_span(0, 2, 4, 12,
                                               &start, &size));
    g_assert_cmpuint(size, ==, 20); /* overlapping attributes */

    g_assert_false(pgraph_vk_vertex_fetch_span(3, 2, 16, 12,
                                                &start, &size));
    g_assert_false(pgraph_vk_vertex_fetch_span(0, 0, 16, 0,
                                                &start, &size));
    g_assert_false(pgraph_vk_vertex_fetch_span(UINT64_MAX, UINT64_MAX,
                                                16, 12, &start, &size));
    g_assert_false(pgraph_vk_vertex_fetch_span(0, 2, UINT64_MAX,
                                                12, &start, &size));
}

static void test_inclusive_dma_and_exclusive_vram(void)
{
    g_assert_true(pgraph_vk_vertex_span_fits_dma(63, 1, 63));
    g_assert_true(pgraph_vk_vertex_span_fits_dma(32, 32, 63));
    g_assert_false(pgraph_vk_vertex_span_fits_dma(32, 33, 63));
    g_assert_false(pgraph_vk_vertex_span_fits_dma(64, 1, 63));
    g_assert_false(pgraph_vk_vertex_span_fits_dma(63, 0, 63));
    g_assert_false(pgraph_vk_vertex_span_fits_dma(UINT64_MAX, 2,
                                                   UINT64_MAX));

    g_assert_true(pgraph_vk_vertex_span_fits_vram(63, 1, 64));
    g_assert_true(pgraph_vk_vertex_span_fits_vram(32, 32, 64));
    g_assert_false(pgraph_vk_vertex_span_fits_vram(32, 33, 64));
    g_assert_false(pgraph_vk_vertex_span_fits_vram(64, 1, 64));
}

static void test_resolve_fetch_range(void)
{
    PGRAPHVkVertexFetchRange range = { 0 };

    g_assert_true(pgraph_vk_vertex_resolve_fetch_range(
        100, 31, 8, 132, 1, 2, 8, 8, &range));
    g_assert_cmpuint(range.attribute_base, ==, 108);
    g_assert_cmpuint(range.fetch_start, ==, 116);
    g_assert_cmpuint(range.fetch_size, ==, 16);

    /* A one-byte attribute may end exactly at both inclusive DMA and
     * exclusive VRAM boundaries. */
    g_assert_true(pgraph_vk_vertex_resolve_fetch_range(
        63, 0, 0, 64, 0, 0, 0, 1, &range));
    g_assert_cmpuint(range.attribute_base, ==, 63);
    g_assert_cmpuint(range.fetch_start, ==, 63);
    g_assert_cmpuint(range.fetch_size, ==, 1);

    g_assert_false(pgraph_vk_vertex_resolve_fetch_range(
        100, 30, 8, 132, 1, 2, 8, 8, &range));
    g_assert_false(pgraph_vk_vertex_resolve_fetch_range(
        100, 31, 8, 131, 1, 2, 8, 8, &range));
    g_assert_false(pgraph_vk_vertex_resolve_fetch_range(
        UINT64_MAX, 0, 1, UINT64_MAX, 0, 0, 0, 1, &range));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/vk/vertex-fetch/span", test_fetch_span);
    g_test_add_func("/xbox/vk/vertex-fetch/bounds",
                    test_inclusive_dma_and_exclusive_vram);
    g_test_add_func("/xbox/vk/vertex-fetch/resolve",
                    test_resolve_fetch_range);
    return g_test_run();
}

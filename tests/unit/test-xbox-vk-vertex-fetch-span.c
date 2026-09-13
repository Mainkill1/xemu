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

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/vk/vertex-fetch/span", test_fetch_span);
    g_test_add_func("/xbox/vk/vertex-fetch/bounds",
                    test_inclusive_dma_and_exclusive_vram);
    return g_test_run();
}

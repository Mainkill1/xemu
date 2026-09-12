#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/vertex-version-policy.h"

static void test_span(void)
{
    g_assert_true(pgraph_vk_vertex_version_span_fits(1, 16, 16, 16));
    g_assert_true(pgraph_vk_vertex_version_span_fits(4, 32, 16, 112));
    g_assert_false(pgraph_vk_vertex_version_span_fits(4, 32, 16, 111));
    g_assert_false(pgraph_vk_vertex_version_span_fits(0, 16, 16, 16));
    g_assert_false(pgraph_vk_vertex_version_span_fits(4, 8, 16, 40));
    g_assert_false(pgraph_vk_vertex_version_span_fits(2, UINT64_MAX, 16,
                                                       UINT64_MAX));
}

static void test_budget(void)
{
    g_assert_true(pgraph_vk_vertex_version_copy_fits(256, 16, 4096));
    g_assert_false(pgraph_vk_vertex_version_copy_fits(257, 16, 4096));
    g_assert_false(pgraph_vk_vertex_version_copy_fits(0, 16, 4096));
    g_assert_false(pgraph_vk_vertex_version_copy_fits(UINT64_MAX, 16,
                                                       UINT64_MAX));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/vk/vertex-version/span", test_span);
    g_test_add_func("/xbox/vk/vertex-version/budget", test_budget);
    return g_test_run();
}

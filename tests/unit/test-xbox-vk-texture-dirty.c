/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"

#include "hw/xbox/nv2a/pgraph/vk/texture-dirty.h"

static void test_revalidation_clears_exact_binding(void)
{
    bool binding_possibly_dirty = true;

    pgraph_vk_texture_binding_revalidated(&binding_possibly_dirty);

    g_assert_false(binding_possibly_dirty);
}

static void test_revalidation_preserves_independent_alias(void)
{
    bool binding_possibly_dirty = true;
    bool overlapping_alias_possibly_dirty = true;

    pgraph_vk_texture_binding_revalidated(&binding_possibly_dirty);

    g_assert_false(binding_possibly_dirty);
    g_assert_true(overlapping_alias_possibly_dirty);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/vulkan/texture-dirty/revalidated",
                    test_revalidation_clears_exact_binding);
    g_test_add_func("/xbox/vulkan/texture-dirty/alias-independent",
                    test_revalidation_preserves_independent_alias);
    return g_test_run();
}

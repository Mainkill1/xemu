/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/descriptor-reuse.h"

static void test_exact_reuse(void)
{
    PGRAPHVkDescriptorCache cache = { 0 };
    PGRAPHVkDescriptorKey a = { 0 }, b = { 0 };
    a.uniforms[0][0] = 1;
    a.uniforms[0][2] = 256;
    a.textures[0][0] = 10;
    b = a;
    b.textures[0][0] = 20;

    g_assert_cmpint(pgraph_vk_descriptor_cache_find(&cache, &a, 1), ==, -1);
    pgraph_vk_descriptor_cache_insert(&cache, &a, 1, 0);
    pgraph_vk_descriptor_cache_insert(&cache, &b, 2, 1);
    /* Returning to A selects its original immutable set, not the last set. */
    g_assert_cmpint(pgraph_vk_descriptor_cache_find(&cache, &a, 1), ==, 0);
    g_assert_cmpint(pgraph_vk_descriptor_cache_find(&cache, &b, 2), ==, 1);

    /* Every descriptor-visible field participates, even with a hash clash. */
    unsigned char *bytes = (unsigned char *)&b;
    for (size_t i = 0; i < sizeof(b); i++) {
        b = a;
        bytes[i] ^= 1;
        g_assert_cmpint(pgraph_vk_descriptor_cache_find(&cache, &b, 1), ==, -1);
    }
}

static void test_collision_and_finish(void)
{
    PGRAPHVkDescriptorCache cache = { 0 };
    PGRAPHVkDescriptorKey a = { 0 }, b = { 0 };
    b.textures[3][1] = 42;
    pgraph_vk_descriptor_cache_insert(&cache, &a, 7, 1023);
    pgraph_vk_descriptor_cache_insert(&cache, &b, 7, 4);
    g_assert_cmpint(pgraph_vk_descriptor_cache_find(&cache, &a, 7), ==, -1);
    g_assert_cmpint(pgraph_vk_descriptor_cache_find(&cache, &b, 7), ==, 4);
    pgraph_vk_descriptor_cache_reset(&cache);
    g_assert_cmpint(pgraph_vk_descriptor_cache_find(&cache, &b, 7), ==, -1);
    /* A reused handle/offset after a finish cannot select the old batch. */
    pgraph_vk_descriptor_cache_insert(&cache, &b, 7, 0);
    g_assert_cmpint(pgraph_vk_descriptor_cache_find(&cache, &b, 7), ==, 0);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/vk/descriptor-reuse/exact", test_exact_reuse);
    g_test_add_func("/xbox/vk/descriptor-reuse/collision-finish",
                    test_collision_and_finish);
    return g_test_run();
}

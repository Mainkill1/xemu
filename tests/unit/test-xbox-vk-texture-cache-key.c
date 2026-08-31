/*
 * NV2A Vulkan texture cache identity tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/lru.h"
#include "hw/xbox/nv2a/pgraph/vk/texture-cache-key.h"

typedef struct TestSamplerCacheEntry {
    LruNode node;
    PGRAPHVkTextureSamplerKey key;
    unsigned int handle;
    uint32_t submit_time;
} TestSamplerCacheEntry;

typedef struct TestSamplerCache {
    Lru lru;
    TestSamplerCacheEntry entries[2];
    TestSamplerCacheEntry *bound;
    bool in_command_buffer;
    uint32_t submit_time;
    unsigned int next_handle;
    unsigned int destroy_count;
} TestSamplerCache;

static void test_sampler_cache_entry_init(Lru *lru, LruNode *node,
                                          const void *key)
{
    TestSamplerCache *cache = container_of(lru, TestSamplerCache, lru);
    TestSamplerCacheEntry *entry =
        container_of(node, TestSamplerCacheEntry, node);

    entry->key = *(const PGRAPHVkTextureSamplerKey *)key;
    entry->handle = ++cache->next_handle;
    entry->submit_time = cache->submit_time;
}

static bool test_sampler_cache_entry_compare(Lru *lru, LruNode *node,
                                             const void *key)
{
    TestSamplerCacheEntry *entry =
        container_of(node, TestSamplerCacheEntry, node);

    return !pgraph_vk_texture_sampler_key_equal(&entry->key, key);
}

static bool test_sampler_cache_entry_pre_evict(Lru *lru, LruNode *node)
{
    TestSamplerCache *cache = container_of(lru, TestSamplerCache, lru);
    TestSamplerCacheEntry *entry =
        container_of(node, TestSamplerCacheEntry, node);

    return pgraph_vk_texture_sampler_can_evict(
        cache->bound == entry, cache->in_command_buffer, entry->submit_time,
        cache->submit_time);
}

static void test_sampler_cache_entry_post_evict(Lru *lru, LruNode *node)
{
    TestSamplerCache *cache = container_of(lru, TestSamplerCache, lru);
    TestSamplerCacheEntry *entry =
        container_of(node, TestSamplerCacheEntry, node);

    entry->handle = 0;
    cache->destroy_count++;
}

static void test_sampler_cache_init(TestSamplerCache *cache, size_t capacity)
{
    g_assert_cmpuint(capacity, >, 0);
    g_assert_cmpuint(capacity, <=, ARRAY_SIZE(cache->entries));
    lru_init(&cache->lru);
    cache->lru.init_node = test_sampler_cache_entry_init;
    cache->lru.compare_nodes = test_sampler_cache_entry_compare;
    cache->lru.pre_node_evict = test_sampler_cache_entry_pre_evict;
    cache->lru.post_node_evict = test_sampler_cache_entry_post_evict;
    for (size_t i = 0; i < capacity; i++) {
        lru_add_free(&cache->lru, &cache->entries[i].node);
    }
}

static bool test_sampler_cache_lookup(TestSamplerCache *cache,
                                      const PGRAPHVkTextureSamplerKey *key,
                                      TestSamplerCacheEntry **entry)
{
    LruNode *node = NULL;
    bool success = lru_try_lookup(&cache->lru, 0, key,
                                  LRU_LOOKUP_ALLOW_EVICT, &node);

    *entry = success ? container_of(node, TestSamplerCacheEntry, node) : NULL;
    return success;
}

static void test_sampler_cache_destroy(TestSamplerCache *cache)
{
    cache->bound = NULL;
    cache->in_command_buffer = false;
    lru_flush(&cache->lru);
    g_assert_cmpint(cache->lru.num_used, ==, 0);
    g_assert_null(cache->lru.bin_blocks);
}

static PGRAPHVkTextureStorageShape test_storage_shape(void)
{
    return (PGRAPHVkTextureStorageShape) {
        .cubemap = false,
        .dimensionality = 2,
        .color_format = 0x12,
        .mip_levels = 5,
        .width = 32,
        .height = 16,
        .depth = 1,
        .border = false,
        .pitch = 0,
    };
}

static VkSamplerCreateInfo test_sampler(void)
{
    return (VkSamplerCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .mipLodBias = 0.0f,
        .anisotropyEnable = VK_FALSE,
        .maxAnisotropy = 1.0f,
        .compareEnable = VK_FALSE,
        .compareOp = VK_COMPARE_OP_ALWAYS,
        .minLod = 0.0f,
        .maxLod = 4.0f,
        .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
        .unnormalizedCoordinates = VK_FALSE,
    };
}

static void test_storage_mip_count(void)
{
    g_assert_cmpuint(pgraph_vk_texture_storage_mip_count(false, 2, 8, 5, 2),
                     ==, 6);
    g_assert_cmpuint(pgraph_vk_texture_storage_mip_count(false, 2, 2, 5, 2),
                     ==, 2);
    g_assert_cmpuint(pgraph_vk_texture_storage_mip_count(true, 2, 8, 5, 2),
                     ==, 1);
    g_assert_cmpuint(pgraph_vk_texture_storage_mip_count(false, 3, 8, 5, 4),
                     ==, 3);
    g_assert_cmpuint(pgraph_vk_texture_storage_mip_count(false, 3, 8, 1, 4),
                     ==, 1);
    g_assert_cmpuint(pgraph_vk_texture_storage_mip_count(false, 2, 0, 5, 2),
                     ==, 1);
    g_assert_cmpuint(pgraph_vk_texture_storage_mip_count(
                         false, 2, UINT32_MAX, UINT32_MAX, 2),
                     ==, UINT32_MAX);
}

static void test_sampler_lod_does_not_change_image_identity(void)
{
    PGRAPHTextureMipLevels mip_a = pgraph_texture_mip_levels_derive(
        false, 2, 5, 5, 4, 0, 4);
    PGRAPHTextureMipLevels mip_b = pgraph_texture_mip_levels_derive(
        false, 2, 5, 5, 4, 2, 2);
    PGRAPHVkTextureStorageShape storage_a, storage_b;
    PGRAPHVkTextureImageKey image_a, image_b;
    PGRAPHVkTextureSamplerKey sampler_a, sampler_b;
    VkSamplerCreateInfo sampler_info_a = test_sampler();
    VkSamplerCreateInfo sampler_info_b = sampler_info_a;
    size_t length_a = 0, length_b = 0;

    /* Exercise the same live helper used by pgraph_get_texture_shape(). */
    g_assert_cmpuint(mip_a.storage_levels, ==, 5);
    g_assert_cmpuint(mip_b.storage_levels, ==, mip_a.storage_levels);
    g_assert_cmpuint(mip_a.sampler_min_level, ==, 0);
    g_assert_cmpuint(mip_a.sampler_max_level, ==, 4);
    g_assert_cmpuint(mip_b.sampler_min_level, ==, 2);
    g_assert_cmpuint(mip_b.sampler_max_level, ==, 2);
    g_assert_true(pgraph_texture_mip_chain_2d_length(
        32, 16, mip_a.storage_levels, 4, 0, &length_a));
    g_assert_true(pgraph_texture_mip_chain_2d_length(
        32, 16, mip_b.storage_levels, 4, 0, &length_b));
    g_assert_cmpuint(length_a, ==, 2728);
    g_assert_cmpuint(length_b, ==, length_a);

    pgraph_vk_texture_storage_shape_init(&storage_a, false, 2, 0x12,
                                         mip_a.storage_levels,
                                         32, 16, 1, false, 0);
    pgraph_vk_texture_storage_shape_init(&storage_b, false, 2, 0x12,
                                         mip_b.storage_levels,
                                         32, 16, 1, false, 0);
    pgraph_vk_texture_image_key_init(&image_a, &storage_a,
                                     0x1000, length_a, 0, 0, 1.0f);
    pgraph_vk_texture_image_key_init(&image_b, &storage_b,
                                     0x1000, length_b, 0, 0, 1.0f);
    g_assert_true(pgraph_vk_texture_image_key_equal(&image_a, &image_b));
    g_assert_cmpmem(&image_a, sizeof(image_a), &image_b, sizeof(image_b));

    sampler_info_a.minLod = mip_a.sampler_min_level;
    sampler_info_a.maxLod = mip_a.sampler_max_level;
    sampler_info_b.minLod = mip_b.sampler_min_level;
    sampler_info_b.maxLod = mip_b.sampler_max_level;
    g_assert_true(pgraph_vk_texture_sampler_key_init(&sampler_a,
                                                     &sampler_info_a, NULL));
    g_assert_true(pgraph_vk_texture_sampler_key_init(&sampler_b,
                                                     &sampler_info_b, NULL));
    g_assert_false(pgraph_vk_texture_sampler_key_equal(&sampler_a, &sampler_b));
}

static void test_material_image_differences(void)
{
    PGRAPHVkTextureStorageShape storage = test_storage_shape();
    PGRAPHVkTextureImageKey a, b;

    pgraph_vk_texture_image_key_init(&a, &storage,
                                     0x1000, 0x400, 0, 0, 0.0f);
    pgraph_vk_texture_image_key_init(&b, &storage,
                                     0x1000, 0x400, 0, 0, -0.0f);
    g_assert_true(pgraph_vk_texture_image_key_equal(&a, &b));

    storage.width++;
    pgraph_vk_texture_image_key_init(&b, &storage,
                                     0x1000, 0x400, 0, 0, 0.0f);
    g_assert_false(pgraph_vk_texture_image_key_equal(&a, &b));

    storage.width--;
    pgraph_vk_texture_image_key_init(&b, &storage,
                                     0x1004, 0x400, 0, 0, 0.0f);
    g_assert_false(pgraph_vk_texture_image_key_equal(&a, &b));
}

static void test_equivalent_resolved_samplers_coalesce(void)
{
    VkSamplerCreateInfo a = test_sampler();
    VkSamplerCreateInfo b = a;
    PGRAPHVkTextureSamplerKey key_a, key_b;
    uint32_t ignored_next = 1;

    b.pNext = &ignored_next;
    b.maxAnisotropy = 16.0f; /* Disabled. */
    b.compareOp = VK_COMPARE_OP_NEVER; /* Disabled. */
    b.borderColor = VK_BORDER_COLOR_INT_TRANSPARENT_BLACK; /* Unused. */
    b.mipLodBias = -0.0f;

    g_assert_true(pgraph_vk_texture_sampler_key_init(&key_a, &a, NULL));
    g_assert_true(pgraph_vk_texture_sampler_key_init(&key_b, &b, NULL));
    g_assert_true(pgraph_vk_texture_sampler_key_equal(&key_a, &key_b));
    g_assert_cmpmem(&key_a, sizeof(key_a), &key_b, sizeof(key_b));
}

static void test_material_sampler_differences(void)
{
    VkSamplerCreateInfo a = test_sampler();
    VkSamplerCreateInfo b = a;
    PGRAPHVkTextureSamplerKey key_a, key_b;
    PGRAPHVkTextureCustomBorderColor border_a = {
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .value = { 1, 2, 3, 4 },
    };
    PGRAPHVkTextureCustomBorderColor border_b = border_a;

    g_assert_true(pgraph_vk_texture_sampler_key_init(&key_a, &a, NULL));
    b.magFilter = VK_FILTER_NEAREST;
    g_assert_true(pgraph_vk_texture_sampler_key_init(&key_b, &b, NULL));
    g_assert_false(pgraph_vk_texture_sampler_key_equal(&key_a, &key_b));

    a.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    a.borderColor = VK_BORDER_COLOR_FLOAT_CUSTOM_EXT;
    b = a;
    g_assert_false(pgraph_vk_texture_sampler_key_init(&key_a, &a, NULL));
    g_assert_true(pgraph_vk_texture_sampler_key_init(&key_a, &a, &border_a));
    g_assert_true(pgraph_vk_texture_sampler_key_init(&key_b, &b, &border_a));
    g_assert_true(pgraph_vk_texture_sampler_key_equal(&key_a, &key_b));

    border_b.value[3]++;
    g_assert_true(pgraph_vk_texture_sampler_key_init(&key_b, &b, &border_b));
    g_assert_false(pgraph_vk_texture_sampler_key_equal(&key_a, &key_b));
}

static void test_sampler_cache_hit_coalescing_and_material_miss(void)
{
    TestSamplerCache cache = { 0 };
    VkSamplerCreateInfo a = test_sampler();
    VkSamplerCreateInfo equivalent = a;
    VkSamplerCreateInfo material = a;
    PGRAPHVkTextureSamplerKey key_a, key_equivalent, key_material;
    TestSamplerCacheEntry *entry_a, *entry_equivalent, *entry_material;

    equivalent.maxAnisotropy = 16.0f; /* Disabled and canonicalized. */
    equivalent.compareOp = VK_COMPARE_OP_NEVER; /* Disabled. */
    material.magFilter = VK_FILTER_NEAREST;
    g_assert_true(pgraph_vk_texture_sampler_key_init(&key_a, &a, NULL));
    g_assert_true(pgraph_vk_texture_sampler_key_init(
        &key_equivalent, &equivalent, NULL));
    g_assert_true(pgraph_vk_texture_sampler_key_init(
        &key_material, &material, NULL));
    g_assert_cmpmem(&key_a, sizeof(key_a),
                    &key_equivalent, sizeof(key_equivalent));

    test_sampler_cache_init(&cache, 2);
    g_assert_true(test_sampler_cache_lookup(&cache, &key_a, &entry_a));
    g_assert_true(test_sampler_cache_lookup(
        &cache, &key_equivalent, &entry_equivalent));
    g_assert_true(entry_a == entry_equivalent);
    g_assert_cmpuint(entry_a->handle, ==, 1);
    g_assert_cmpint(cache.lru.num_used, ==, 1);

    g_assert_true(test_sampler_cache_lookup(
        &cache, &key_material, &entry_material));
    g_assert_true(entry_material != entry_a);
    g_assert_cmpuint(entry_material->handle, ==, 2);
    g_assert_cmpint(cache.lru.num_used, ==, 2);
    g_assert_cmpuint(cache.destroy_count, ==, 0);

    test_sampler_cache_destroy(&cache);
    g_assert_cmpuint(cache.destroy_count, ==, 2);
}

static void test_sampler_cache_synchronous_lifetime(void)
{
    TestSamplerCache cache = { 0 };
    VkSamplerCreateInfo a = test_sampler();
    VkSamplerCreateInfo material = a;
    PGRAPHVkTextureSamplerKey key_a, key_material;
    TestSamplerCacheEntry *entry_a, *entry_material;

    material.magFilter = VK_FILTER_NEAREST;
    g_assert_true(pgraph_vk_texture_sampler_key_init(&key_a, &a, NULL));
    g_assert_true(pgraph_vk_texture_sampler_key_init(
        &key_material, &material, NULL));

    cache.submit_time = 7;
    test_sampler_cache_init(&cache, 1);
    g_assert_true(test_sampler_cache_lookup(&cache, &key_a, &entry_a));

    cache.bound = entry_a;
    g_assert_false(test_sampler_cache_lookup(
        &cache, &key_material, &entry_material));
    g_assert_null(entry_material);

    cache.bound = NULL;
    cache.in_command_buffer = true;
    g_assert_false(test_sampler_cache_lookup(
        &cache, &key_material, &entry_material));
    g_assert_null(entry_material);

    /* The previous synchronous submission is complete at the next serial. */
    cache.submit_time++;
    g_assert_true(test_sampler_cache_lookup(
        &cache, &key_material, &entry_material));
    g_assert_nonnull(entry_material);
    g_assert_cmpuint(cache.destroy_count, ==, 1);

    test_sampler_cache_destroy(&cache);
    g_assert_cmpuint(cache.destroy_count, ==, 2);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/vk-texture-cache-key/storage-mip-count",
                    test_storage_mip_count);
    g_test_add_func("/xbox/vk-texture-cache-key/sampler-lod-image-identity",
                    test_sampler_lod_does_not_change_image_identity);
    g_test_add_func("/xbox/vk-texture-cache-key/material-image-differences",
                    test_material_image_differences);
    g_test_add_func("/xbox/vk-texture-cache-key/equivalent-samplers",
                    test_equivalent_resolved_samplers_coalesce);
    g_test_add_func("/xbox/vk-texture-cache-key/material-sampler-differences",
                    test_material_sampler_differences);
    g_test_add_func("/xbox/vk-texture-cache-key/sampler-cache-hit-miss",
                    test_sampler_cache_hit_coalescing_and_material_miss);
    g_test_add_func("/xbox/vk-texture-cache-key/sampler-cache-lifetime",
                    test_sampler_cache_synchronous_lifetime);
    return g_test_run();
}

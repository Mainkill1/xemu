/*
 * NV2A PGRAPH lazy cache tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/lru.h"
#include "hw/xbox/nv2a/pgraph/lazy-cache.h"

typedef struct TestEntry {
    LruNode node;
    uint32_t key;
} TestEntry;

typedef struct TestCache {
    Lru lru;
    GPtrArray *blocks;
    size_t num_entries;
} TestCache;

enum {
    TEST_MAX_ENTRIES = 4,
    TEST_BLOCK_ENTRIES = 2,
};

static void test_entry_init(Lru *lru, LruNode *node, const void *key)
{
    TestEntry *entry = container_of(node, TestEntry, node);
    entry->key = *(const uint32_t *)key;
}

static bool test_entry_compare(Lru *lru, LruNode *node, const void *key)
{
    TestEntry *entry = container_of(node, TestEntry, node);
    return entry->key != *(const uint32_t *)key;
}

static void test_cache_grow(TestCache *cache, size_t count)
{
    TestEntry *entries = g_new0(TestEntry, count);
    g_ptr_array_add(cache->blocks, entries);
    for (size_t i = 0; i < count; i++) {
        lru_add_free(&cache->lru, &entries[i].node);
    }
    cache->num_entries += count;
}

static TestEntry *test_cache_lookup(TestCache *cache, uint32_t key)
{
    uint64_t hash = key;
    size_t count = pgraph_lazy_cache_growth_for_lookup(
        &cache->lru,
        cache->num_entries, TEST_MAX_ENTRIES, TEST_BLOCK_ENTRIES,
        hash, &key);
    if (count) {
        test_cache_grow(cache, count);
    }
    return container_of(lru_lookup(&cache->lru, hash, &key), TestEntry, node);
}

static void test_cache_init(TestCache *cache)
{
    lru_init(&cache->lru);
    cache->lru.init_node = test_entry_init;
    cache->lru.compare_nodes = test_entry_compare;
    cache->blocks = g_ptr_array_new_with_free_func(g_free);
    cache->num_entries = 0;
}

static void test_cache_destroy(TestCache *cache)
{
    lru_flush(&cache->lru);
    g_assert_null(cache->lru.bin_blocks);
    g_ptr_array_free(cache->blocks, true);
}

static void test_growth_decision(void)
{
    g_assert_cmpuint(pgraph_lazy_cache_growth_count(0, 4, 2, 0, false),
                     ==, 2);
    g_assert_cmpuint(pgraph_lazy_cache_growth_count(2, 4, 2, 1, false),
                     ==, 0);
    g_assert_cmpuint(pgraph_lazy_cache_growth_count(2, 4, 2, 0, true),
                     ==, 0);
    g_assert_cmpuint(pgraph_lazy_cache_growth_count(3, 4, 2, 0, false),
                     ==, 1);
    g_assert_cmpuint(pgraph_lazy_cache_growth_count(4, 4, 2, 0, false),
                     ==, 0);
    g_assert_cmpuint(pgraph_lazy_cache_growth_count(90, 100, 16, 0, false),
                     ==, 10);
    g_assert_cmpuint(pgraph_lazy_cache_growth_count(96, 100, 16, 0, false),
                     ==, 4);
    g_assert_cmpuint(pgraph_lazy_cache_growth_count(0, 100, 0, 0, false),
                     ==, 0);
    g_assert_cmpuint(pgraph_lazy_cache_growth_count(
                         0, 50 * 1024, 256, 0, false),
                     ==, 256);
    g_assert_cmpuint(pgraph_lazy_cache_growth_count(
                         50 * 1024 - 128, 50 * 1024, 256, 0, false),
                     ==, 128);
    g_assert_cmpuint(pgraph_lazy_cache_growth_count(0, 512, 64, 0, false),
                     ==, 64);
}

static void test_stable_blocks_and_hits(void)
{
    TestCache cache;
    test_cache_init(&cache);

    TestEntry *one = test_cache_lookup(&cache, 1);
    g_assert_cmpuint(cache.num_entries, ==, 2);
    g_assert_true(test_cache_lookup(&cache, 1) == one);

    test_cache_lookup(&cache, 2);
    g_assert_cmpuint(cache.lru.num_free, ==, 0);
    g_assert_true(test_cache_lookup(&cache, 1) == one);
    g_assert_cmpuint(cache.num_entries, ==, 2);

    test_cache_lookup(&cache, 3);
    g_assert_cmpuint(cache.num_entries, ==, 4);
    g_assert_true(test_cache_lookup(&cache, 1) == one);

    test_cache_lookup(&cache, 4);
    g_assert_cmpuint(cache.lru.num_free, ==, 0);
    test_cache_lookup(&cache, 5);
    g_assert_cmpuint(cache.num_entries, ==, TEST_MAX_ENTRIES);

    test_cache_destroy(&cache);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/pgraph-lazy-cache/growth-decision",
                    test_growth_decision);
    g_test_add_func("/xbox/pgraph-lazy-cache/stable-blocks-and-hits",
                    test_stable_blocks_and_hits);
    return g_test_run();
}

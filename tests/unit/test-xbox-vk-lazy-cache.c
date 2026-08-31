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
    bool protected;
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

static bool test_entry_can_evict(Lru *lru, LruNode *node)
{
    TestEntry *entry = container_of(node, TestEntry, node);
    return !entry->protected;
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

static bool test_cache_try_lookup(TestCache *cache, uint32_t key,
                                  TestEntry **entry)
{
    uint64_t hash = key;
    size_t count = pgraph_lazy_cache_growth_for_lookup(
        &cache->lru, cache->num_entries, TEST_MAX_ENTRIES,
        TEST_BLOCK_ENTRIES, hash, &key);
    if (count) {
        test_cache_grow(cache, count);
    }

    LruNode *node = NULL;
    bool success = lru_try_lookup(&cache->lru, hash, &key,
                                  LRU_LOOKUP_ALLOW_EVICT, &node);
    *entry = success ? container_of(node, TestEntry, node) : NULL;
    return success;
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

static void test_protected_capacity_retry(void)
{
    TestCache cache;
    TestEntry *entries[TEST_MAX_ENTRIES];
    TestEntry *found = (TestEntry *)0x1;

    test_cache_init(&cache);
    for (uint32_t i = 0; i < TEST_MAX_ENTRIES; i++) {
        entries[i] = test_cache_lookup(&cache, i + 1);
        entries[i]->protected = true;
    }
    cache.lru.pre_node_evict = test_entry_can_evict;

    g_assert_false(test_cache_try_lookup(&cache, 100, &found));
    g_assert_null(found);
    g_assert_cmpuint(cache.num_entries, ==, TEST_MAX_ENTRIES);
    g_assert_cmpint(cache.lru.num_used, ==, TEST_MAX_ENTRIES);
    for (uint32_t i = 0; i < TEST_MAX_ENTRIES; i++) {
        g_assert_true(lru_contains_hash(&cache.lru, i + 1));
    }

    /* Model retirement making one formerly protected entry evictable. */
    entries[0]->protected = false;
    g_assert_true(test_cache_try_lookup(&cache, 100, &found));
    g_assert_true(found == entries[0]);
    g_assert_false(lru_contains_hash(&cache.lru, 1));
    g_assert_true(lru_contains_hash(&cache.lru, 100));
    g_assert_cmpint(cache.lru.num_used, ==, TEST_MAX_ENTRIES);

    cache.lru.pre_node_evict = NULL;
    test_cache_destroy(&cache);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/pgraph-lazy-cache/growth-decision",
                    test_growth_decision);
    g_test_add_func("/xbox/pgraph-lazy-cache/stable-blocks-and-hits",
                    test_stable_blocks_and_hits);
    g_test_add_func("/xbox/pgraph-lazy-cache/protected-capacity-retry",
                    test_protected_capacity_retry);
    return g_test_run();
}

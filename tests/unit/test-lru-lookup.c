/*
 * LRU lookup policy tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qemu/lru.h"

typedef struct TestNode {
    LruNode node;
    uint32_t key;
} TestNode;

static unsigned int eviction_count;

static TestNode *test_node_from_lru(LruNode *node)
{
    return (TestNode *)((char *)node - offsetof(TestNode, node));
}

static void test_node_init(Lru *lru, LruNode *node, const void *key)
{
    TestNode *test_node = test_node_from_lru(node);

    test_node->key = *(const uint32_t *)key;
}

static bool test_node_compare(Lru *lru, LruNode *node, const void *key)
{
    TestNode *test_node = test_node_from_lru(node);

    return test_node->key != *(const uint32_t *)key;
}

static void test_node_post_evict(Lru *lru, LruNode *node)
{
    eviction_count++;
}

static void init_test_lru(Lru *lru, TestNode *nodes, size_t count)
{
    lru_init(lru);
    lru->init_node = test_node_init;
    lru->compare_nodes = test_node_compare;
    lru->post_node_evict = test_node_post_evict;
    for (size_t i = 0; i < count; i++) {
        lru_add_free(lru, &nodes[i].node);
    }
    eviction_count = 0;
}

static LruNode *lookup(Lru *lru, uint32_t key, unsigned int flags,
                       bool expected)
{
    LruNode *node = NULL;
    bool success = lru_try_lookup(lru, key, &key, flags, &node);

    assert(success == expected);
    assert((node != NULL) == expected);
    return node;
}

static void test_no_evict_lookup(void)
{
    Lru lru;
    TestNode nodes[2] = { 0 };
    LruNode *first;

    init_test_lru(&lru, nodes, sizeof(nodes) / sizeof(nodes[0]));
    first = lookup(&lru, 1, 0, true);
    lookup(&lru, 2, 0, true);

    assert(lookup(&lru, 3, 0, false) == NULL);
    assert(eviction_count == 0);
    assert(lru.num_used == 2);
    assert(lru.num_free == 0);

    assert(lookup(&lru, 1, 0, true) == first);
    assert(eviction_count == 0);
}

static void test_explicit_evict_lookup(void)
{
    Lru lru;
    TestNode nodes[2] = { 0 };

    init_test_lru(&lru, nodes, sizeof(nodes) / sizeof(nodes[0]));
    lookup(&lru, 1, 0, true);
    lookup(&lru, 2, 0, true);
    lookup(&lru, 1, 0, true);

    lookup(&lru, 3, LRU_LOOKUP_ALLOW_EVICT, true);
    assert(eviction_count == 1);
    assert(lru.num_used == 2);
    assert(lru.num_free == 0);
    assert(!lru_contains_hash(&lru, 2));
    assert(lru_contains_hash(&lru, 3));
}

int main(void)
{
    test_no_evict_lookup();
    test_explicit_evict_lookup();

    return 0;
}

/*
 * LRU active traversal tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/lru.h"

typedef struct TestNode {
    LruNode node;
    uint64_t key;
} TestNode;

typedef struct VisitState {
    uint64_t keys[8];
    unsigned int count;
    bool evict;
} VisitState;

static void test_lru_init_node(Lru *lru, LruNode *node, const void *key)
{
    TestNode *test_node = container_of(node, TestNode, node);

    test_node->key = *(const uint64_t *)key;
}

static bool test_lru_compare_nodes(Lru *lru, LruNode *node, const void *key)
{
    TestNode *test_node = container_of(node, TestNode, node);

    return test_node->key != *(const uint64_t *)key;
}

static void test_lru_setup(Lru *lru, TestNode *nodes, size_t count)
{
    lru_init(lru);
    lru->init_node = test_lru_init_node;
    lru->compare_nodes = test_lru_compare_nodes;

    for (size_t i = 0; i < count; i++) {
        lru_add_free(lru, &nodes[i].node);
    }
}

static void visit_record_active(Lru *lru, LruNode *node, void *opaque)
{
    VisitState *state = opaque;
    TestNode *test_node = container_of(node, TestNode, node);

    g_assert_true(lru_is_node_in_use(lru, node));
    g_assert_cmpuint(state->count, <, G_N_ELEMENTS(state->keys));
    state->keys[state->count++] = test_node->key;

    if (state->evict) {
        lru_evict_node(lru, node);
    }
}

static void test_visit_active_ignores_free_nodes(void)
{
    Lru lru;
    TestNode nodes[8] = { 0 };
    VisitState state = { 0 };
    uint64_t key10 = 10;
    uint64_t key20 = 20;
    uint64_t key30 = 30;

    test_lru_setup(&lru, nodes, G_N_ELEMENTS(nodes));
    lru_lookup(&lru, key10, &key10);
    lru_lookup(&lru, key20, &key20);
    lru_lookup(&lru, key30, &key30);

    lru_visit_active(&lru, visit_record_active, &state);

    g_assert_cmpuint(state.count, ==, 3);
    g_assert_cmpuint(state.keys[0], ==, key30);
    g_assert_cmpuint(state.keys[1], ==, key20);
    g_assert_cmpuint(state.keys[2], ==, key10);
    g_assert_cmpint(lru.num_used, ==, 3);
    g_assert_cmpint(lru.num_free, ==, 5);
}

static void test_visit_active_handles_same_bin_collisions(void)
{
    Lru lru;
    TestNode nodes[4] = { 0 };
    VisitState state = { 0 };
    uint64_t key10 = 10;
    uint64_t key20 = 20;
    uint64_t shared_hash = 3;

    test_lru_setup(&lru, nodes, G_N_ELEMENTS(nodes));
    lru_lookup(&lru, shared_hash, &key10);
    lru_lookup(&lru, shared_hash, &key20);

    lru_visit_active(&lru, visit_record_active, &state);

    g_assert_cmpuint(state.count, ==, 2);
    g_assert_cmpuint(state.keys[0], ==, key20);
    g_assert_cmpuint(state.keys[1], ==, key10);
}

static void test_visit_active_allows_eviction_by_visitor(void)
{
    Lru lru;
    TestNode nodes[5] = { 0 };
    VisitState state = { .evict = true };
    VisitState after = { 0 };
    uint64_t key1 = 1;
    uint64_t key2 = 2;
    uint64_t key3 = 3;

    test_lru_setup(&lru, nodes, G_N_ELEMENTS(nodes));
    lru_lookup(&lru, key1, &key1);
    lru_lookup(&lru, key2, &key2);
    lru_lookup(&lru, key3, &key3);

    lru_visit_active(&lru, visit_record_active, &state);

    g_assert_cmpuint(state.count, ==, 3);
    g_assert_cmpint(lru.num_used, ==, 0);
    g_assert_cmpint(lru.num_free, ==, 5);

    lru_visit_active(&lru, visit_record_active, &after);
    g_assert_cmpuint(after.count, ==, 0);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/lru/active/ignores-free-nodes",
                    test_visit_active_ignores_free_nodes);
    g_test_add_func("/lru/active/same-bin-collisions",
                    test_visit_active_handles_same_bin_collisions);
    g_test_add_func("/lru/active/visitor-eviction",
                    test_visit_active_allows_eviction_by_visitor);

    return g_test_run();
}

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/texture.c"
static unsigned int finishes;
void pgraph_vk_finish(PGRAPHState *pg, FinishReason reason)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    g_assert_cmpint(reason, ==, VK_FINISH_REASON_NEED_BUFFER_SPACE);
    /* Simulate completed submission, keeping production preparation/binding. */
    r->submit_count++;
    r->in_command_buffer = false;
    r->descriptor_set_index = 0;
    r->storage_buffers[BUFFER_UNIFORM_STAGING].buffer_offset = 0;
    finishes++;
}

static unsigned int evictions;

static void record_cache_eviction(Lru *cache, LruNode *node)
{
    PGRAPHVkState *r = container_of(cache, PGRAPHVkState, texture_cache);
    TextureBinding *binding = container_of(node, TextureBinding, node);

    g_assert_true(!r->in_command_buffer ||
                  binding->submit_time != r->submit_count);
    for (int i = 0; i < NV2A_MAX_TEXTURES; i++) {
        g_assert_true(r->texture_bindings[i] != binding);
    }
    evictions++;
}

static void test_cache_pressure(gconstpointer pending)
{
    g_autofree PGRAPHState *pg = g_new0(PGRAPHState, 1);
    g_autofree PGRAPHVkState *r = g_new0(PGRAPHVkState, 1);
    TextureBinding entries[NV2A_MAX_TEXTURES + 1] = { 0 };
    TextureKey keys[NV2A_MAX_TEXTURES + 2] = { 0 };
    TextureBinding *unbound = NULL;

    pg->vk_renderer_state = r;
    r->submit_count = 7;
    r->in_command_buffer = true;
    lru_init(&r->texture_cache);
    r->texture_cache.compare_nodes = texture_cache_entry_compare;
    r->texture_cache.pre_node_evict = texture_cache_entry_pre_evict;
    r->texture_cache.post_node_evict = record_cache_eviction;
    finishes = evictions = 0;
    for (int i = 0; i < ARRAY_SIZE(entries); i++) {
        lru_add_free(&r->texture_cache, &entries[i].node);
    }
    for (int i = 0; i < ARRAY_SIZE(entries); i++) {
        keys[i].texture_vram_offset = 256 * (i + 1);
        TextureBinding *binding =
            texture_cache_get_or_create(pg, i + 1, &keys[i]);
        binding->key = keys[i];
        binding->submit_time = pending ? 7 : 6;
        if (i < NV2A_MAX_TEXTURES) {
            r->texture_bindings[i] = binding;
        } else {
            unbound = binding;
        }
    }
    g_assert_cmpuint(finishes, ==, 0);
    g_assert_cmpuint(evictions, ==, 0);
    g_assert_cmpint(r->texture_cache.num_free, ==, 0);

    /* Full-cache exact hits must not complete a batch. */
    g_assert_true(texture_cache_get_or_create(pg, 1, &keys[0]) ==
                  r->texture_bindings[0]);
    g_assert_cmpuint(finishes, ==, 0);

    const int replacement = ARRAY_SIZE(entries);
    keys[replacement].texture_vram_offset = 256 * (replacement + 1);
    TextureBinding *binding =
        texture_cache_get_or_create(pg, replacement + 1, &keys[replacement]);
    g_assert_true(binding == unbound);
    g_assert_cmpuint(finishes, ==, pending ? 1 : 0);
    g_assert_cmpuint(evictions, ==, 1);
    g_assert_cmpuint(r->submit_count, ==, pending ? 8 : 7);
    for (int i = 0; i < NV2A_MAX_TEXTURES; i++) {
        g_assert_false(texture_cache_entry_pre_evict(
            &r->texture_cache, &r->texture_bindings[i]->node));
    }
}

static void test_partial_stage_completion(void)
{
    g_autofree PGRAPHState *pg = g_new0(PGRAPHState, 1);
    g_autofree PGRAPHVkState *r = g_new0(PGRAPHVkState, 1);
    TextureBinding entries[NV2A_MAX_TEXTURES + 1] = { 0 };
    TextureKey keys[NV2A_MAX_TEXTURES + 3] = { 0 };
    pg->vk_renderer_state = r;
    r->submit_count = 7;
    r->in_command_buffer = true;
    lru_init(&r->texture_cache);
    r->texture_cache.compare_nodes = texture_cache_entry_compare;
    r->texture_cache.pre_node_evict = texture_cache_entry_pre_evict;
    r->texture_cache.post_node_evict = record_cache_eviction;
    finishes = evictions = 0;
    for (int i = 0; i < ARRAY_SIZE(entries); i++) {
        lru_add_free(&r->texture_cache, &entries[i].node);
    }
    for (int i = 0; i < ARRAY_SIZE(entries); i++) {
        keys[i].texture_vram_offset = 256 * (i + 1);
        TextureBinding *b = texture_cache_get_or_create(pg, i + 1, &keys[i]);
        b->key = keys[i];
        b->submit_time = 7;
        if (i < NV2A_MAX_TEXTURES) {
            r->texture_bindings[i] = b;
        }
    }
    TextureBinding *first = r->texture_bindings[0];
    TextureBinding *old_second = r->texture_bindings[1];
    keys[5].texture_vram_offset = 1536;
    TextureBinding *replacement = texture_cache_get_or_create(pg, 6, &keys[5]);
    replacement->key = keys[5];
    r->texture_bindings[1] = replacement;
    g_assert_cmpuint(finishes, ==, 1);
    g_assert_true(r->texture_bindings[0] == first);
    g_assert_false(
        texture_cache_entry_pre_evict(&r->texture_cache, &first->node));
    /* Later-stage allocation must reuse a completed unbound old stage. */
    keys[6].texture_vram_offset = 1792;
    TextureBinding *later = texture_cache_get_or_create(pg, 7, &keys[6]);
    g_assert_true(later == old_second);
    later->key = keys[6];
    r->texture_bindings[2] = later;
    g_assert_cmpuint(finishes, ==, 1);
    g_assert_true(r->texture_bindings[0] == first);
    /* Real bind_textures ends by stamping every selected stage. */
    update_timestamps(r);
    r->in_command_buffer = true;
    g_assert_cmpuint(first->submit_time, ==, 8);
    r->texture_bindings[0] = NULL;
    g_assert_false(
        texture_cache_entry_pre_evict(&r->texture_cache, &first->node));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_data_func("/vk/texture-cache/evict-completed", NULL,
                         test_cache_pressure);
    g_test_add_data_func("/vk/texture-cache/complete-pending",
                         GINT_TO_POINTER(1), test_cache_pressure);
    g_test_add_func("/vk/texture-cache/partial-stage-completion",
                    test_partial_stage_completion);
    return g_test_run();
}

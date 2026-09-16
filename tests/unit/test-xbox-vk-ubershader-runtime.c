/*
 * NV2A Vulkan ubershader runtime-key regression tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/xbox/nv2a/pgraph/vk/renderer.h"
#include "hw/xbox/nv2a/pgraph/vk/hybrid-ready.h"
#include "hw/xbox/nv2a/pgraph/vk/pipeline-key.h"
#include "hw/xbox/nv2a/pgraph/polygon-offset.h"

static unsigned int probe_inits;
static unsigned int probe_evictions;

static void probe_pipeline_init(Lru *cache, LruNode *node, const void *key)
{
    PipelineBinding *binding = container_of(node, PipelineBinding, node);

    (void)cache;
    probe_inits++;
    binding->key = *(const PipelineKey *)key;
    binding->pipeline = VK_NULL_HANDLE;
}

static bool probe_pipeline_different(Lru *cache, LruNode *node,
                                     const void *key)
{
    PipelineBinding *binding = container_of(node, PipelineBinding, node);

    (void)cache;
    return memcmp(&binding->key, key, sizeof(binding->key)) != 0;
}

static void probe_post_evict(Lru *cache, LruNode *node)
{
    (void)cache;
    (void)node;
    probe_evictions++;
}

static void test_pipeline_ready_probe_is_side_effect_free(void)
{
    static Lru cache;
    PipelineBinding entries[2] = { 0 };
    PipelineKey first = { .fragment_route = PGRAPH_VK_FRAGMENT_SPECIALIZED };
    PipelineKey second = { .fragment_route = PGRAPH_VK_FRAGMENT_UBERSHADER };
    PipelineKey missing = first;
    const uint64_t first_hash = 17;
    const uint64_t second_hash = 18;

    missing.regs[0] = 1;
    probe_inits = probe_evictions = 0;
    lru_init(&cache);
    cache.init_node = probe_pipeline_init;
    cache.compare_nodes = probe_pipeline_different;
    cache.post_node_evict = probe_post_evict;
    for (size_t i = 0; i < ARRAY_SIZE(entries); i++) {
        lru_add_free(&cache, &entries[i].node);
    }
    LruNode *first_node = lru_lookup(&cache, first_hash, &first);
    LruNode *second_node = lru_lookup(&cache, second_hash, &second);
    PipelineBinding *first_binding =
        container_of(first_node, PipelineBinding, node);

    g_assert_null(pgraph_vk_pipeline_cache_find_ready(
        &cache, first_hash, &first));
    first_binding->pipeline = (VkPipeline)(uintptr_t)1;
    g_assert_true(pgraph_vk_pipeline_cache_find_ready(
        &cache, first_hash, &first) == first_binding);
    g_assert_null(pgraph_vk_pipeline_cache_find_ready(
        &cache, first_hash, &missing));
    g_assert_true(QTAILQ_FIRST(&cache.global) == second_node);
    g_assert_cmpint(cache.num_used, ==, 2);
    g_assert_cmpint(cache.num_free, ==, 0);
    g_assert_cmpuint(probe_inits, ==, 2);
    g_assert_cmpuint(probe_evictions, ==, 0);
}

static void test_pipeline_key_distinguishes_vertex_input_counts(void)
{
    static Lru cache;
    PipelineBinding entry = { 0 };
    PipelineKey one = { .binding_description_count = 1,
                        .attribute_description_count = 1 };
    PipelineKey two = one;
    const uint64_t forced_collision = 31;

    /* The unused array entries can remain byte-identical. Count is still
     * part of the Vulkan vertex-input recipe and must change identity. */
    two.binding_description_count = 2;
    two.attribute_description_count = 2;
    lru_init(&cache);
    cache.init_node = probe_pipeline_init;
    cache.compare_nodes = probe_pipeline_different;
    lru_add_free(&cache, &entry.node);
    PipelineBinding *binding = container_of(
        lru_lookup(&cache, forced_collision, &one), PipelineBinding, node);
    binding->pipeline = (VkPipeline)(uintptr_t)1;

    g_assert_true(pgraph_vk_pipeline_cache_find_ready(
        &cache, forced_collision, &one) == binding);
    g_assert_null(pgraph_vk_pipeline_cache_find_ready(
        &cache, forced_collision, &two));
}

static void test_pipeline_key_ignores_uniform_only_register_values(void)
{
    PipelineKey a = { 0 };
    PipelineKey b = { 0 };
    const uint32_t setup_raster =
        NV_PGRAPH_SETUPRASTER_FRONTFACEMODE_FILL |
        NV_PGRAPH_SETUPRASTER_POFFSETFILLENABLE;

    a.regs[0] = b.regs[0] = NV_PGRAPH_BLEND_EN;
    a.regs[1] = 0x12000011;
    b.regs[1] = 0x120000ee;
    a.regs[6] = 0x3f800000;
    b.regs[6] = 0x40000000;
    a.regs[7] = 0x40400000;
    b.regs[7] = 0x40800000;

    g_assert_cmpuint(GET_MASK(a.regs[1], NV_PGRAPH_CONTROL_0_ALPHAREF),
                     !=,
                     GET_MASK(b.regs[1], NV_PGRAPH_CONTROL_0_ALPHAREF));
    PGRAPHPolygonOffsetUniformKey offset_a =
        pgraph_polygon_offset_uniform_key(
            NV097_SET_BEGIN_END_OP_TRIANGLES, setup_raster,
            a.regs[6], a.regs[7]);
    PGRAPHPolygonOffsetUniformKey offset_b =
        pgraph_polygon_offset_uniform_key(
            NV097_SET_BEGIN_END_OP_TRIANGLES, setup_raster,
            b.regs[6], b.regs[7]);
    g_assert_false(pgraph_polygon_offset_uniform_key_equal(
        offset_a, offset_b));

    pgraph_vk_pipeline_key_canonicalize_uniform_regs(&a);
    pgraph_vk_pipeline_key_canonicalize_uniform_regs(&b);
    g_assert_cmpmem(&a, sizeof(a), &b, sizeof(b));

    b.regs[1] ^= NV_PGRAPH_CONTROL_0_Z_PERSPECTIVE_ENABLE;
    pgraph_vk_pipeline_key_canonicalize_uniform_regs(&b);
    g_assert_cmpint(memcmp(&a, &b, sizeof(a)), !=, 0);
}

static void probe_shader_init(Lru *cache, LruNode *node, const void *key)
{
    ShaderBinding *binding = container_of(node, ShaderBinding, node);
    const ShaderBindingKey *shader_key = key;

    (void)cache;
    probe_inits++;
    binding->state = shader_key->state;
    binding->fragment_route = shader_key->fragment_route;
}

static bool probe_shader_different(Lru *cache, LruNode *node,
                                   const void *key)
{
    ShaderBinding *binding = container_of(node, ShaderBinding, node);
    const ShaderBindingKey *shader_key = key;

    (void)cache;
    return binding->fragment_route != shader_key->fragment_route ||
           memcmp(&binding->state, &shader_key->state,
                  sizeof(binding->state)) != 0;
}

static void test_shader_ready_probe_requires_runtime_metadata(void)
{
    static Lru cache;
    ShaderBinding entry = { 0 };
    static ShaderModuleInfo vertex;
    static ShaderModuleInfo fragment;
    static ShaderModuleInfo geometry;
    ShaderBindingKey key = {
        .fragment_route = PGRAPH_VK_FRAGMENT_UBERSHADER,
    };
    ShaderBindingKey other = key;
    const uint64_t hash = 29;

    other.fragment_route = PGRAPH_VK_FRAGMENT_SPECIALIZED;
    probe_inits = probe_evictions = 0;
    lru_init(&cache);
    cache.init_node = probe_shader_init;
    cache.compare_nodes = probe_shader_different;
    cache.post_node_evict = probe_post_evict;
    lru_add_free(&cache, &entry.node);
    ShaderBinding *binding = container_of(
        lru_lookup(&cache, hash, &key), ShaderBinding, node);

    g_assert_null(pgraph_vk_shader_binding_find_ready(
        &cache, hash, &key, false));
    binding->vsh.module_info = &vertex;
    g_assert_null(pgraph_vk_shader_binding_find_ready(
        &cache, hash, &key, false));
    binding->psh.module_info = &fragment;
    g_assert_true(pgraph_vk_shader_binding_find_ready(
        &cache, hash, &key, false) == binding);
    g_assert_null(pgraph_vk_shader_binding_find_ready(
        &cache, hash, &key, true));
    binding->geom.module_info = &geometry;
    g_assert_true(pgraph_vk_shader_binding_find_ready(
        &cache, hash, &key, true) == binding);
    g_assert_null(pgraph_vk_shader_binding_find_ready(
        &cache, hash, &other, false));
    g_assert_cmpint(cache.num_used, ==, 1);
    g_assert_cmpint(cache.num_free, ==, 0);
    g_assert_cmpuint(probe_inits, ==, 1);
    g_assert_cmpuint(probe_evictions, ==, 0);
}

static void test_execution_route_requires_complete_candidate(void)
{
    g_assert_cmpint(pgraph_vk_hybrid_choose_execution_route(
                        true, true, true, true,
                        PGRAPH_VK_FALLBACK_RESOURCES_READY),
                    ==, PGRAPH_VK_EXECUTION_SPECIALIZED);
    g_assert_cmpint(pgraph_vk_hybrid_choose_execution_route(
                        true, false, true, true,
                        PGRAPH_VK_FALLBACK_RESOURCES_READY),
                    ==, PGRAPH_VK_EXECUTION_UBERSHADER);
    g_assert_cmpint(pgraph_vk_hybrid_choose_execution_route(
                        false, false, true, true,
                        PGRAPH_VK_FALLBACK_RESOURCES_READY),
                    ==, PGRAPH_VK_EXECUTION_UBERSHADER);
    g_assert_cmpint(pgraph_vk_hybrid_choose_execution_route(
                        true, false, true, false,
                        PGRAPH_VK_FALLBACK_RESOURCES_READY),
                    ==, PGRAPH_VK_EXECUTION_UNCOVERED);
    g_assert_cmpint(pgraph_vk_hybrid_choose_execution_route(
                        false, false, true, true,
                        PGRAPH_VK_FALLBACK_RESOURCES_NEED_ROLLOVER),
                    ==, PGRAPH_VK_EXECUTION_UBERSHADER_AFTER_ROLLOVER);
    g_assert_cmpint(pgraph_vk_hybrid_choose_execution_route(
                        false, false, true, true,
                        PGRAPH_VK_FALLBACK_RESOURCES_UNAVAILABLE),
                    ==, PGRAPH_VK_EXECUTION_UNCOVERED);
}

static void test_uncovered_build_uses_closer_binding(void)
{
    g_assert_cmpint(pgraph_vk_hybrid_choose_uncovered_route(
                        true, false, true),
                    ==, PGRAPH_VK_FRAGMENT_SPECIALIZED);
    g_assert_cmpint(pgraph_vk_hybrid_choose_uncovered_route(
                        false, true, true),
                    ==, PGRAPH_VK_FRAGMENT_UBERSHADER);
    g_assert_cmpint(pgraph_vk_hybrid_choose_uncovered_route(
                        false, false, true),
                    ==, PGRAPH_VK_FRAGMENT_SPECIALIZED);
    g_assert_cmpint(pgraph_vk_hybrid_choose_uncovered_route(
                        true, true, true),
                    ==, PGRAPH_VK_FRAGMENT_SPECIALIZED);
    g_assert_cmpint(pgraph_vk_hybrid_choose_uncovered_route(
                        false, true, false),
                    ==, PGRAPH_VK_FRAGMENT_SPECIALIZED);
}

static void test_fallback_promotion_probe_has_time_gate(void)
{
    g_assert_true(pgraph_vk_hybrid_promotion_due(100, 0));
    g_assert_false(pgraph_vk_hybrid_promotion_due(115, 116));
    g_assert_true(pgraph_vk_hybrid_promotion_due(116, 116));
    g_assert_true(pgraph_vk_hybrid_promotion_due(117, 116));
}

static bool publication_allow_evict;

static bool publication_pre_evict(Lru *cache, LruNode *node)
{
    (void)cache;
    (void)node;
    return publication_allow_evict;
}

static void test_pipeline_publication_eviction_is_late(void)
{
    static Lru cache;
    PipelineBinding entry = { 0 };
    PipelineKey old_key = { .regs[0] = 1 };
    PipelineKey new_key = { .regs[0] = 2 };

    lru_init(&cache);
    cache.init_node = probe_pipeline_init;
    cache.compare_nodes = probe_pipeline_different;
    cache.pre_node_evict = publication_pre_evict;
    lru_add_free(&cache, &entry.node);
    PipelineBinding *old = container_of(
        lru_lookup(&cache, 1, &old_key), PipelineBinding, node);
    old->pipeline = (VkPipeline)(uintptr_t)1;
    g_assert_cmpint(cache.num_free, ==, 0);

    /* Work may compile while the old executable remains in the LRU. */
    publication_allow_evict = false;
    g_assert_true(pgraph_vk_pipeline_cache_find_ready(
        &cache, 1, &old_key) == old);
    g_assert_null(pgraph_vk_pipeline_cache_publish_slot(
        &cache, 2, &new_key));
    g_assert_true(pgraph_vk_pipeline_cache_find_ready(
        &cache, 1, &old_key) == old);

    publication_allow_evict = true;
    PipelineBinding *published = pgraph_vk_pipeline_cache_publish_slot(
        &cache, 2, &new_key);
    g_assert_true(published == old);
    g_assert_null(pgraph_vk_pipeline_cache_find_ready(
        &cache, 1, &old_key));
    g_assert_cmpuint(published->key.regs[0], ==, 2);
    g_assert_cmpint(cache.num_used, ==, 1);
    g_assert_cmpint(cache.num_free, ==, 0);
}

static void test_fallback_family_queue_deduplicates_and_bounds(void)
{
    PGRAPHVkFallbackFamilyRequest requests[2] = { 0 };
    ShaderState state = { 0 };
    PipelineKey a = { .regs[0] = 1 };
    PipelineKey b = { .regs[0] = 2 };
    PipelineKey c = { .regs[0] = 3 };

    g_assert_true(pgraph_vk_fallback_family_enqueue(
        requests, G_N_ELEMENTS(requests), &a, &state));
    g_assert_true(pgraph_vk_fallback_family_enqueue(
        requests, G_N_ELEMENTS(requests), &a, &state));
    g_assert_true(pgraph_vk_fallback_family_enqueue(
        requests, G_N_ELEMENTS(requests), &b, &state));
    g_assert_false(pgraph_vk_fallback_family_enqueue(
        requests, G_N_ELEMENTS(requests), &c, &state));
    g_assert_cmpuint(requests[0].key.regs[0], ==, 1);
    g_assert_cmpuint(requests[1].key.regs[0], ==, 2);
}

static void test_changed_register_marks_shortcut_dirty(void)
{
    PGRAPHState *pg = g_new0(PGRAPHState, 1);

    g_assert_false(pg->regs_written_since_draw);
    pgraph_reg_w(pg, NV_PGRAPH_CONTROL_0, 0);
    g_assert_false(pg->regs_written_since_draw);
    pgraph_reg_w(pg, NV_PGRAPH_CONTROL_0, 1);
    g_assert_true(pg->regs_written_since_draw);
    g_free(pg);
}

static void test_snapshot_restore_invalidates_execution_hints(void)
{
    PGRAPHState *pg = g_new0(PGRAPHState, 1);

    pgraph_invalidate_all_register_hints(pg);
    g_assert_true(pg->regs_written_since_draw);
    g_assert_true(pg->program_data_dirty);
    g_assert_true(pgraph_is_reg_dirty(pg, NV_PGRAPH_CONTROL_0));
    g_assert_true(pgraph_is_reg_dirty(pg, NV_PGRAPH_ZOFFSETFACTOR));
    g_free(pg);
}

static ShaderState base_state(void)
{
    ShaderState state = { 0 };

    state.psh.shader_stage_program = 0x12345678;
    state.psh.other_stage_input = 0x9abcdef0;
    state.psh.alpha_test = true;
    state.psh.alpha_func = ALPHA_FUNC_LESS;
    return state;
}

static PipelineKey pipeline_key(PGRAPHVkFragmentRoute route,
                                const ShaderState *state)
{
    PipelineKey key = { 0 };

    pgraph_vk_pipeline_key_set_shader(&key, state, route);
    return key;
}

static void test_dynamic_control_binding_matches_packet_abi(void)
{
    g_assert_cmpuint(PGRAPH_VK_PSH_UBER_UBO_BINDING, ==, 6);
    g_assert_cmpuint(sizeof(PGRAPHUberControls), ==, 448);
    g_assert_cmpuint(_Alignof(PGRAPHUberControls), ==, 16);
}

static void test_control_only_upload_does_not_require_descriptor_update(void)
{
    bool descriptor_update =
        pgraph_vk_descriptor_update_needed(false, false, false);

    g_assert_false(descriptor_update);
    g_assert_true(pgraph_vk_reuses_descriptor_set_for_control_update(
        true, descriptor_update));
    g_assert_true(pgraph_vk_descriptor_update_needed(true, false, false));
    g_assert_true(pgraph_vk_descriptor_update_needed(false, true, false));
    g_assert_true(pgraph_vk_descriptor_update_needed(false, false, true));
}

static void test_disabled_runtime_uses_baseline_descriptor_layout(void)
{
    g_assert_cmpuint(pgraph_vk_descriptor_layout_binding_count(false), ==, 6);
    g_assert_cmpuint(pgraph_vk_descriptor_layout_binding_count(true), ==, 7);
    g_assert_cmpuint(pgraph_vk_descriptor_dynamic_offset_count(false), ==, 0);
    g_assert_cmpuint(pgraph_vk_descriptor_dynamic_offset_count(true), ==, 1);
}

static void test_shader_binding_key_equality_requires_route_and_full_state(void)
{
    ShaderBindingKey a = {
        .state = base_state(),
        .fragment_route = PGRAPH_VK_FRAGMENT_SPECIALIZED,
    };
    ShaderBindingKey b = a;

    g_assert_true(pgraph_vk_shader_binding_key_equal(&a, &b));
    g_assert_false(pgraph_vk_shader_binding_key_different(&a, &b));
    b.fragment_route = PGRAPH_VK_FRAGMENT_UBERSHADER;
    g_assert_false(pgraph_vk_shader_binding_key_equal(&a, &b));
    g_assert_true(pgraph_vk_shader_binding_key_different(&a, &b));
    b = a;
    b.state.psh.combiner_control = 1;
    g_assert_false(pgraph_vk_shader_binding_key_equal(&a, &b));
    g_assert_true(pgraph_vk_shader_binding_key_different(&a, &b));
}

static void test_shader_module_key_uses_only_active_stage(void)
{
    ShaderModuleCacheKey a = { 0 };
    ShaderModuleCacheKey b;
    size_t active_size;

    a.kind = VK_SHADER_STAGE_GEOMETRY_BIT;
    a.fragment_route = PGRAPH_VK_FRAGMENT_SPECIALIZED;
    a.geom.glsl_opts.vulkan = true;
    b = a;
    active_size = pgraph_vk_shader_module_key_active_size(&a);
    g_assert_cmpuint(active_size, <, sizeof(a));
    ((uint8_t *)&b)[sizeof(b) - 1] = 0x5a;
    g_assert_true(pgraph_vk_shader_module_key_equal(&a, &b));
    b.geom.glsl_opts.vulkan = false;
    g_assert_false(pgraph_vk_shader_module_key_equal(&a, &b));

    a.kind = VK_SHADER_STAGE_VERTEX_BIT;
    a.vsh.glsl_opts.ubo_binding = 4;
    b = a;
    g_assert_true(pgraph_vk_shader_module_key_equal(&a, &b));
    b.vsh.glsl_opts.ubo_binding = 5;
    g_assert_false(pgraph_vk_shader_module_key_equal(&a, &b));

    a.kind = VK_SHADER_STAGE_FRAGMENT_BIT;
    a.psh.glsl_opts.uber_binding = 6;
    b = a;
    g_assert_true(pgraph_vk_shader_module_key_equal(&a, &b));
    b.psh.glsl_opts.uber_binding = 7;
    g_assert_false(pgraph_vk_shader_module_key_equal(&a, &b));
    b = a;
    b.fragment_route = PGRAPH_VK_FRAGMENT_UBERSHADER;
    g_assert_false(pgraph_vk_shader_module_key_equal(&a, &b));
    b = a;
    b.kind = VK_SHADER_STAGE_VERTEX_BIT;
    g_assert_false(pgraph_vk_shader_module_key_equal(&a, &b));
}

static void test_uber_pipeline_key_ignores_only_combiner_words(void)
{
    ShaderState a = base_state();
    ShaderState b = a;

    a.psh.combiner_control = 1;
    a.psh.rgb_inputs[0] = 0x11111111;
    a.psh.alpha_inputs[0] = 0x22222222;
    a.psh.rgb_outputs[0] = 0x33333333;
    a.psh.alpha_outputs[0] = 0x44444444;
    a.psh.final_inputs_0 = 0x55555555;
    a.psh.final_inputs_1 = 0x66666666;
    b.psh.combiner_control = 8;
    b.psh.rgb_inputs[0] = 0xaaaaaaaa;
    b.psh.alpha_inputs[0] = 0xbbbbbbbb;
    b.psh.rgb_outputs[0] = 0xcccccccc;
    b.psh.alpha_outputs[0] = 0xdddddddd;
    b.psh.final_inputs_0 = 0xeeeeeeee;
    b.psh.final_inputs_1 = 0xffffffff;

    PipelineKey uber_a = pipeline_key(PGRAPH_VK_FRAGMENT_UBERSHADER, &a);
    PipelineKey uber_b = pipeline_key(PGRAPH_VK_FRAGMENT_UBERSHADER, &b);
    PipelineKey specialized_a =
        pipeline_key(PGRAPH_VK_FRAGMENT_SPECIALIZED, &a);
    PipelineKey specialized_b =
        pipeline_key(PGRAPH_VK_FRAGMENT_SPECIALIZED, &b);

    g_assert_cmpmem(&uber_a, sizeof(uber_a), &uber_b, sizeof(uber_b));
    g_assert_cmpint(memcmp(&specialized_a, &specialized_b,
                           sizeof(specialized_a)), !=, 0);
}

static void test_fragment_route_keeps_pipeline_keys_isolated(void)
{
    ShaderState state = base_state();
    PipelineKey specialized =
        pipeline_key(PGRAPH_VK_FRAGMENT_SPECIALIZED, &state);
    PipelineKey uber = pipeline_key(PGRAPH_VK_FRAGMENT_UBERSHADER, &state);

    g_assert_cmpint(memcmp(&specialized, &uber, sizeof(specialized)), !=, 0);
}

static void test_canonicalization_preserves_fragment_shell_state(void)
{
    PshState state = { 0 };

    state.combiner_control = 0xffffffff;
    memset(state.rgb_inputs, 0x11, sizeof(state.rgb_inputs));
    memset(state.alpha_inputs, 0x22, sizeof(state.alpha_inputs));
    memset(state.rgb_outputs, 0x33, sizeof(state.rgb_outputs));
    memset(state.alpha_outputs, 0x44, sizeof(state.alpha_outputs));
    state.final_inputs_0 = 0x55555555;
    state.final_inputs_1 = 0x66666666;
    state.shader_stage_program = 0x77777777;
    state.other_stage_input = 0x88888888;
    state.alpha_test = true;
    state.alpha_func = ALPHA_FUNC_GREATER;

    pgraph_vk_canonicalize_uber_combiner_state(&state);

    g_assert_cmpuint(state.combiner_control, ==, 0);
    g_assert_cmpmem(state.rgb_inputs, sizeof(state.rgb_inputs),
                    (uint32_t[8]) { 0 }, sizeof(state.rgb_inputs));
    g_assert_cmpmem(state.alpha_inputs, sizeof(state.alpha_inputs),
                    (uint32_t[8]) { 0 }, sizeof(state.alpha_inputs));
    g_assert_cmpmem(state.rgb_outputs, sizeof(state.rgb_outputs),
                    (uint32_t[8]) { 0 }, sizeof(state.rgb_outputs));
    g_assert_cmpmem(state.alpha_outputs, sizeof(state.alpha_outputs),
                    (uint32_t[8]) { 0 }, sizeof(state.alpha_outputs));
    g_assert_cmpuint(state.final_inputs_0, ==, 0);
    g_assert_cmpuint(state.final_inputs_1, ==, 0);
    g_assert_cmpuint(state.shader_stage_program, ==, 0x77777777);
    g_assert_cmpuint(state.other_stage_input, ==, 0x88888888);
    g_assert_true(state.alpha_test);
    g_assert_cmpint(state.alpha_func, ==, ALPHA_FUNC_GREATER);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/vk/ubershader/runtime/control-abi",
                    test_dynamic_control_binding_matches_packet_abi);
    g_test_add_func("/xbox/vk/ubershader/runtime/control-only-upload",
                    test_control_only_upload_does_not_require_descriptor_update);
    g_test_add_func("/xbox/vk/ubershader/runtime/baseline-layout",
                    test_disabled_runtime_uses_baseline_descriptor_layout);
    g_test_add_func("/xbox/vk/ubershader/runtime/shader-binding-key",
                    test_shader_binding_key_equality_requires_route_and_full_state);
    g_test_add_func("/xbox/vk/ubershader/runtime/shader-module-key",
                    test_shader_module_key_uses_only_active_stage);
    g_test_add_func("/xbox/vk/ubershader/runtime/canonical-key",
                    test_uber_pipeline_key_ignores_only_combiner_words);
    g_test_add_func("/xbox/vk/ubershader/runtime/route-isolation",
                    test_fragment_route_keeps_pipeline_keys_isolated);
    g_test_add_func("/xbox/vk/ubershader/runtime/shell-state",
                    test_canonicalization_preserves_fragment_shell_state);
    g_test_add_func("/xbox/vk/ubershader/runtime/pipeline-ready-probe",
                    test_pipeline_ready_probe_is_side_effect_free);
    g_test_add_func("/xbox/vk/ubershader/runtime/vertex-input-count-key",
                    test_pipeline_key_distinguishes_vertex_input_counts);
    g_test_add_func("/xbox/vk/ubershader/runtime/uniform-register-key",
                    test_pipeline_key_ignores_uniform_only_register_values);
    g_test_add_func("/xbox/vk/ubershader/runtime/shader-ready-probe",
                    test_shader_ready_probe_requires_runtime_metadata);
    g_test_add_func("/xbox/vk/ubershader/runtime/complete-route",
                    test_execution_route_requires_complete_candidate);
    g_test_add_func("/xbox/vk/ubershader/runtime/uncovered-choice",
                    test_uncovered_build_uses_closer_binding);
    g_test_add_func("/xbox/vk/ubershader/runtime/promotion-time-gate",
                    test_fallback_promotion_probe_has_time_gate);
    g_test_add_func("/xbox/vk/ubershader/runtime/pipeline-late-publication",
                    test_pipeline_publication_eviction_is_late);
    g_test_add_func("/xbox/vk/ubershader/runtime/fallback-family-queue",
                    test_fallback_family_queue_deduplicates_and_bounds);
    g_test_add_func("/xbox/vk/ubershader/runtime/register-shortcut-dirty",
                    test_changed_register_marks_shortcut_dirty);
    g_test_add_func("/xbox/vk/ubershader/runtime/snapshot-invalidation",
                    test_snapshot_restore_invalidates_execution_hints);
    return g_test_run();
}

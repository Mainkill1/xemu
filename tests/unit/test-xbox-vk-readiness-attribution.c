/*
 * NV2A Vulkan first-demand readiness attribution tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/xbox/nv2a/pgraph/vk/readiness-attribution.h"

static PipelineKey test_key(uint32_t identity)
{
    PipelineKey key = { 0 };
    key.fragment_route = PGRAPH_VK_FRAGMENT_SPECIALIZED;
    key.regs[0] = identity;
    key.render_pass_state.color_format = VK_FORMAT_B8G8R8A8_UNORM;
    return key;
}

static void test_each_class_is_recorded_once(void)
{
    PGRAPHVkReadinessAttributionState state;
    pgraph_vk_readiness_attribution_init(&state);

    for (unsigned int i = 0; i < PGRAPH_VK_READINESS_CLASS_COUNT; i++) {
        PipelineKey key = test_key(i + 1);
        g_assert_true(pgraph_vk_readiness_note_first_demand(
            &state, &key, 7, 100 + i, (PGRAPHVkReadinessClass)i));
        g_assert_cmpuint(state.telemetry.classified[i], ==, 1);
        const PGRAPHVkReadinessRecord *record =
            pgraph_vk_readiness_find(&state, &key, 7);
        g_assert_nonnull(record);
        g_assert_cmpint(record->classification, ==, i);
    }
}

static void test_miss_classification_precedence(void)
{
    g_assert_cmpint(pgraph_vk_readiness_classify_miss(false, false, false), ==,
                    PGRAPH_VK_READINESS_MISSED);
    g_assert_cmpint(pgraph_vk_readiness_classify_miss(true, false, false), ==,
                    PGRAPH_VK_READINESS_TOO_LATE);
    g_assert_cmpint(pgraph_vk_readiness_classify_miss(false, false, true), ==,
                    PGRAPH_VK_READINESS_UNSUPPORTED);
    g_assert_cmpint(pgraph_vk_readiness_classify_miss(true, true, true), ==,
                    PGRAPH_VK_READINESS_QUEUE_DEFERRED);
}

static void test_duplicate_keeps_first_classification(void)
{
    PGRAPHVkReadinessAttributionState state;
    pgraph_vk_readiness_attribution_init(&state);
    PipelineKey key = test_key(10);

    g_assert_true(pgraph_vk_readiness_note_first_demand(
        &state, &key, 1, 100, PGRAPH_VK_READINESS_TOO_LATE));
    g_assert_false(pgraph_vk_readiness_note_first_demand(
        &state, &key, 1, 200, PGRAPH_VK_READINESS_HIT));

    const PGRAPHVkReadinessRecord *record =
        pgraph_vk_readiness_find(&state, &key, 1);
    g_assert_nonnull(record);
    g_assert_cmpint(record->classification, ==,
                    PGRAPH_VK_READINESS_TOO_LATE);
    g_assert_cmpuint(record->demand_count, ==, 2);
    g_assert_cmpuint(state.telemetry.classified[PGRAPH_VK_READINESS_HIT], ==,
                     0);
    g_assert_cmpuint(state.telemetry.duplicate_demands, ==, 1);
}

static void test_generation_reclassifies_exact_key(void)
{
    PGRAPHVkReadinessAttributionState state;
    pgraph_vk_readiness_attribution_init(&state);
    PipelineKey key = test_key(11);

    g_assert_true(pgraph_vk_readiness_note_first_demand(
        &state, &key, 2, 100, PGRAPH_VK_READINESS_MISSED));
    g_assert_true(pgraph_vk_readiness_note_first_demand(
        &state, &key, 3, 200, PGRAPH_VK_READINESS_HIT));
    g_assert_null(pgraph_vk_readiness_find(&state, &key, 2));
    g_assert_nonnull(pgraph_vk_readiness_find(&state, &key, 3));
    g_assert_cmpuint(state.telemetry.generation_reclassifications, ==, 1);
}

static void test_tracker_is_bounded_and_reports_eviction(void)
{
    PGRAPHVkReadinessAttributionState state;
    pgraph_vk_readiness_attribution_init(&state);

    for (uint32_t i = 0; i < PGRAPH_VK_MAX_READINESS_RECORDS; i++) {
        PipelineKey key = test_key(i + 1);
        g_assert_true(pgraph_vk_readiness_note_first_demand(
            &state, &key, 1, i + 1, PGRAPH_VK_READINESS_MISSED));
    }
    PipelineKey overflow = test_key(PGRAPH_VK_MAX_READINESS_RECORDS + 1);
    g_assert_true(pgraph_vk_readiness_note_first_demand(
        &state, &overflow, 1, 1000, PGRAPH_VK_READINESS_QUEUE_DEFERRED));
    g_assert_cmpuint(state.telemetry.tracker_evictions, ==, 1);
    g_assert_nonnull(pgraph_vk_readiness_find(&state, &overflow, 1));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/vk/readiness/classes",
                    test_each_class_is_recorded_once);
    g_test_add_func("/xbox/vk/readiness/miss-precedence",
                    test_miss_classification_precedence);
    g_test_add_func("/xbox/vk/readiness/duplicate",
                    test_duplicate_keeps_first_classification);
    g_test_add_func("/xbox/vk/readiness/generation",
                    test_generation_reclassifies_exact_key);
    g_test_add_func("/xbox/vk/readiness/bounded",
                    test_tracker_is_bounded_and_reports_eviction);
    return g_test_run();
}

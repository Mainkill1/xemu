/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/shader-timing-policy.h"

static void test_timestamp_delta_handles_valid_bit_wrap(void)
{
    g_assert_cmpuint(
        pgraph_vk_shader_timing_delta(UINT64_C(0xffffffffe), 3, 36), ==, 5);
    g_assert_cmpuint(pgraph_vk_shader_timing_delta(UINT64_MAX - 1, 3, 64), ==,
                     5);
    g_assert_cmpuint(pgraph_vk_shader_timing_delta(17, 42, 64), ==, 25);
    g_assert_cmpuint(pgraph_vk_shader_timing_delta(42, 42, 36), ==, 0);
    g_assert_cmpuint(pgraph_vk_shader_timing_delta(42, 42, 64), ==, 0);
}

static void test_timestamp_delta_rejects_invalid_bit_counts(void)
{
    g_assert_cmpuint(pgraph_vk_shader_timing_delta(17, 42, 0), ==, 0);
    g_assert_cmpuint(pgraph_vk_shader_timing_delta(17, 42, 35), ==, 0);
    g_assert_cmpuint(pgraph_vk_shader_timing_delta(17, 42, 65), ==, 0);
}

static void test_pipeline_variants_remain_distinct(void)
{
    /* Same shader-binding bytes; only the render-pass byte differs. */
    uint8_t first[16] = { 17, 0, 0, 0, 0, 0, 0, 0, 3 };
    uint8_t second[16] = { 17, 0, 0, 0, 0, 0, 0, 0, 4 };

    uint64_t first_id =
        pgraph_vk_shader_timing_pipeline_variant_id(&first, sizeof(first));
    uint64_t second_id =
        pgraph_vk_shader_timing_pipeline_variant_id(&second, sizeof(second));
    g_assert_cmpuint(first_id, !=, second_id);
    g_assert_cmpuint(
        first_id, ==,
        pgraph_vk_shader_timing_pipeline_variant_id(&first, sizeof(first)));
}

static void test_query_admission_rejects_clear_and_full_ring(void)
{
    PGRAPHVkShaderTimingInput input = {
        .requested = true,
        .query_pool_ready = true,
        .pipeline_ready = true,
        .shader_ready = true,
        .capacity = 2,
    };

    g_assert_cmpuint(pgraph_vk_shader_timing_decide(&input), ==,
                     PGRAPH_VK_SHADER_TIMING_RECORD);
    input.clearing = true;
    g_assert_cmpuint(pgraph_vk_shader_timing_decide(&input), ==,
                     PGRAPH_VK_SHADER_TIMING_SKIP);
    input.clearing = false;
    input.used = input.capacity;
    g_assert_cmpuint(pgraph_vk_shader_timing_decide(&input), ==,
                     PGRAPH_VK_SHADER_TIMING_DROP);
    input.requested = false;
    g_assert_cmpuint(pgraph_vk_shader_timing_decide(&input), ==,
                     PGRAPH_VK_SHADER_TIMING_SKIP);
    input.requested = true;
    input.used = 0;
    input.query_pool_ready = false;
    g_assert_cmpuint(pgraph_vk_shader_timing_decide(&input), ==,
                     PGRAPH_VK_SHADER_TIMING_SKIP);
    input.query_pool_ready = true;
    input.pipeline_ready = false;
    g_assert_cmpuint(pgraph_vk_shader_timing_decide(&input), ==,
                     PGRAPH_VK_SHADER_TIMING_SKIP);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/vk/shader-timing/timestamp-wrap",
                    test_timestamp_delta_handles_valid_bit_wrap);
    g_test_add_func("/xbox/vk/shader-timing/invalid-bit-counts",
                    test_timestamp_delta_rejects_invalid_bit_counts);
    g_test_add_func("/xbox/vk/shader-timing/pipeline-variants",
                    test_pipeline_variants_remain_distinct);
    g_test_add_func("/xbox/vk/shader-timing/query-admission",
                    test_query_admission_rejects_clear_and_full_ring);
    return g_test_run();
}

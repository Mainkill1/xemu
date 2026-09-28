/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <assert.h>
#include <stdint.h>
#include "hw/xbox/nv2a/pgraph/vk/shader-timing-policy.h"

static void test_timestamp_delta_handles_valid_bit_wrap(void)
{
    assert(pgraph_vk_shader_timing_delta(UINT64_C(0xffffffffe), 3, 36) == 5);
    assert(pgraph_vk_shader_timing_delta(UINT64_MAX - 1, 3, 64) == 5);
    assert(pgraph_vk_shader_timing_delta(17, 42, 64) == 25);
    assert(pgraph_vk_shader_timing_delta(42, 42, 36) == 0);
    assert(pgraph_vk_shader_timing_delta(42, 42, 64) == 0);
}

static void test_timestamp_delta_rejects_invalid_bit_counts(void)
{
    assert(pgraph_vk_shader_timing_delta(17, 42, 0) == 0);
    assert(pgraph_vk_shader_timing_delta(17, 42, 35) == 0);
    assert(pgraph_vk_shader_timing_delta(17, 42, 65) == 0);
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
    assert(first_id != second_id);
    assert(first_id ==
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

    assert(pgraph_vk_shader_timing_decide(&input) ==
           PGRAPH_VK_SHADER_TIMING_RECORD);
    input.clearing = true;
    assert(pgraph_vk_shader_timing_decide(&input) ==
           PGRAPH_VK_SHADER_TIMING_SKIP);
    input.clearing = false;
    input.used = input.capacity;
    assert(pgraph_vk_shader_timing_decide(&input) ==
           PGRAPH_VK_SHADER_TIMING_DROP);
    input.requested = false;
    assert(pgraph_vk_shader_timing_decide(&input) ==
           PGRAPH_VK_SHADER_TIMING_SKIP);
    input.requested = true;
    input.used = 0;
    input.query_pool_ready = false;
    assert(pgraph_vk_shader_timing_decide(&input) ==
           PGRAPH_VK_SHADER_TIMING_SKIP);
    input.query_pool_ready = true;
    input.pipeline_ready = false;
    assert(pgraph_vk_shader_timing_decide(&input) ==
           PGRAPH_VK_SHADER_TIMING_SKIP);
}

int main(void)
{
    test_timestamp_delta_handles_valid_bit_wrap();
    test_timestamp_delta_rejects_invalid_bit_counts();
    test_pipeline_variants_remain_distinct();
    test_query_admission_rejects_clear_and_full_ring();
    return 0;
}

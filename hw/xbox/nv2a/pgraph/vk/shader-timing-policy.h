/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef HW_XBOX_NV2A_PGRAPH_VK_SHADER_TIMING_POLICY_H
#define HW_XBOX_NV2A_PGRAPH_VK_SHADER_TIMING_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qemu/fast-hash.h"
#include "hw/xbox/nv2a/pgraph/glsl/shader-browser-observation.h"

typedef struct PGRAPHVkShaderTimingInput {
    bool requested;
    bool query_pool_ready;
    bool clearing;
    bool pipeline_ready;
    bool shader_ready;
    uint32_t used;
    uint32_t capacity;
} PGRAPHVkShaderTimingInput;

typedef enum PGRAPHVkShaderTimingDecision {
    PGRAPH_VK_SHADER_TIMING_SKIP,
    PGRAPH_VK_SHADER_TIMING_RECORD,
    PGRAPH_VK_SHADER_TIMING_DROP,
} PGRAPHVkShaderTimingDecision;

static inline uint64_t
pgraph_vk_shader_timing_delta(uint64_t start, uint64_t end, uint32_t valid_bits)
{
    if (valid_bits < 36 || valid_bits > 64) {
        return 0;
    }
    uint64_t delta = end - start;
    return valid_bits == 64 ? delta : delta & ((UINT64_C(1) << valid_bits) - 1);
}

static inline PGRAPHShaderBrowserSampleDecision
pgraph_vk_shader_timing_choose_sample(PGRAPHShaderBrowserSampler *sampler,
                                      uint64_t frame, bool timestamp_supported,
                                      bool in_command_buffer,
                                      bool query_reset_in_command_buffer)
{
    /* A fresh command buffer will reset the pool before the draw. An open
     * buffer without a reset cannot accept a timestamp if GPU timing was
     * enabled after that buffer began. */
    bool query_available = !in_command_buffer || query_reset_in_command_buffer;
    return pgraph_shader_browser_choose_sample(
        sampler, frame, timestamp_supported && query_available);
}

static inline uint64_t
pgraph_vk_shader_timing_pipeline_variant_id(const void *pipeline_key,
                                            size_t key_size)
{
    return fast_hash((const uint8_t *)pipeline_key, key_size);
}

static inline PGRAPHVkShaderTimingDecision
pgraph_vk_shader_timing_decide(const PGRAPHVkShaderTimingInput *input)
{
    if (!input->requested || !input->query_pool_ready || input->clearing ||
        !input->pipeline_ready || !input->shader_ready) {
        return PGRAPH_VK_SHADER_TIMING_SKIP;
    }
    return input->used < input->capacity ? PGRAPH_VK_SHADER_TIMING_RECORD :
                                           PGRAPH_VK_SHADER_TIMING_DROP;
}

#endif

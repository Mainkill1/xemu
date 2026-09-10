/*
 * Geforce NV2A PGRAPH Vulkan shader identity instrumentation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_SHADER_IDENTITY_H
#define HW_XBOX_NV2A_PGRAPH_VK_SHADER_IDENTITY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PGRAPH_VK_SHADER_IDENTITY_MAX_ENTRIES 1024U
#define PGRAPH_VK_SHADER_IDENTITY_MAX_BYTES   (2U * 1024U * 1024U)
#define PGRAPH_VK_SHADER_IDENTITY_MAX_RECORDS 4096U

typedef enum PGRAPHVkShaderSourceClass {
    PGRAPH_VK_SHADER_SOURCE_FIRST,
    PGRAPH_VK_SHADER_SOURCE_REPEAT,
    PGRAPH_VK_SHADER_SOURCE_SATURATED,
} PGRAPHVkShaderSourceClass;

typedef struct PGRAPHVkShaderIdentityRecord {
    uint64_t key_hash;
    uint64_t source_hash;
    uint32_t stage;
    uint32_t profile_frame;
    PGRAPHVkShaderSourceClass source_class;
    int64_t generation_us;
    int64_t compile_us;
    int64_t module_create_us;
    int64_t reflection_us;
} PGRAPHVkShaderIdentityRecord;

typedef struct PGRAPHVkShaderIdentityTracker {
    void *entries;
    size_t capacity;
    size_t count;
    size_t max_source_bytes;
    size_t source_bytes;
    PGRAPHVkShaderIdentityRecord *records;
    size_t record_capacity;
    size_t record_count;
    bool records_saturated;
    bool flushed;
} PGRAPHVkShaderIdentityTracker;

bool pgraph_vk_shader_identity_tracker_init(
    PGRAPHVkShaderIdentityTracker *tracker, size_t capacity,
    size_t record_capacity, size_t max_source_bytes);
void pgraph_vk_shader_identity_tracker_destroy(
    PGRAPHVkShaderIdentityTracker *tracker);
PGRAPHVkShaderSourceClass pgraph_vk_shader_identity_observe(
    PGRAPHVkShaderIdentityTracker *tracker, uint32_t stage,
    const void *source, size_t source_size, uint64_t source_hash,
    uint64_t key_hash, uint32_t profile_frame, int64_t generation_us,
    int64_t compile_us, int64_t module_create_us, int64_t reflection_us);
const PGRAPHVkShaderIdentityRecord *pgraph_vk_shader_identity_records(
    const PGRAPHVkShaderIdentityTracker *tracker, size_t *record_count);
bool pgraph_vk_shader_identity_records_saturated(
    const PGRAPHVkShaderIdentityTracker *tracker);
bool pgraph_vk_shader_identity_begin_flush(
    PGRAPHVkShaderIdentityTracker *tracker);

#endif

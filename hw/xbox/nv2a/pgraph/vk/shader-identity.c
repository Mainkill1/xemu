/*
 * Geforce NV2A PGRAPH Vulkan shader identity instrumentation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "hw/xbox/nv2a/pgraph/vk/shader-identity.h"

#include <stdlib.h>
#include <string.h>

typedef struct PGRAPHVkShaderIdentityEntry {
    uint64_t source_hash;
    uint32_t stage;
    size_t source_size;
    uint8_t *source;
} PGRAPHVkShaderIdentityEntry;

bool pgraph_vk_shader_identity_tracker_init(
    PGRAPHVkShaderIdentityTracker *tracker, size_t capacity,
    size_t record_capacity, size_t max_source_bytes)
{
    if (!tracker || !capacity || !record_capacity || !max_source_bytes ||
        capacity > SIZE_MAX / sizeof(PGRAPHVkShaderIdentityEntry) ||
        record_capacity > SIZE_MAX / sizeof(PGRAPHVkShaderIdentityRecord)) {
        return false;
    }

    PGRAPHVkShaderIdentityEntry *entries = calloc(capacity, sizeof(*entries));
    if (!entries) {
        return false;
    }
    PGRAPHVkShaderIdentityRecord *records =
        calloc(record_capacity, sizeof(*records));
    if (!records) {
        free(entries);
        return false;
    }

    *tracker = (PGRAPHVkShaderIdentityTracker) {
        .entries = entries,
        .capacity = capacity,
        .max_source_bytes = max_source_bytes,
        .records = records,
        .record_capacity = record_capacity,
    };
    return true;
}

void pgraph_vk_shader_identity_tracker_destroy(
    PGRAPHVkShaderIdentityTracker *tracker)
{
    if (!tracker) {
        return;
    }

    PGRAPHVkShaderIdentityEntry *entries = tracker->entries;
    for (size_t i = 0; i < tracker->count; i++) {
        free(entries[i].source);
    }
    free(entries);
    free(tracker->records);
    *tracker = (PGRAPHVkShaderIdentityTracker) { 0 };
}

PGRAPHVkShaderSourceClass pgraph_vk_shader_identity_observe(
    PGRAPHVkShaderIdentityTracker *tracker, uint32_t stage,
    const void *source, size_t source_size, uint64_t source_hash,
    uint64_t key_hash, uint32_t profile_frame, int64_t generation_us,
    int64_t compile_us, int64_t module_create_us, int64_t reflection_us)
{
    if (!tracker || !tracker->entries || !source || !source_size) {
        return PGRAPH_VK_SHADER_SOURCE_SATURATED;
    }

    PGRAPHVkShaderSourceClass source_class = PGRAPH_VK_SHADER_SOURCE_FIRST;
    PGRAPHVkShaderIdentityEntry *entries = tracker->entries;
    for (size_t i = 0; i < tracker->count; i++) {
        if (entries[i].source_hash == source_hash &&
            entries[i].stage == stage &&
            entries[i].source_size == source_size &&
            !memcmp(entries[i].source, source, source_size)) {
            source_class = PGRAPH_VK_SHADER_SOURCE_REPEAT;
            break;
        }
    }

    if (source_class == PGRAPH_VK_SHADER_SOURCE_FIRST) {
        if (tracker->count == tracker->capacity ||
            source_size > tracker->max_source_bytes - tracker->source_bytes) {
            source_class = PGRAPH_VK_SHADER_SOURCE_SATURATED;
        } else {
            uint8_t *copy = malloc(source_size);
            if (!copy) {
                source_class = PGRAPH_VK_SHADER_SOURCE_SATURATED;
            } else {
                memcpy(copy, source, source_size);
                entries[tracker->count++] = (PGRAPHVkShaderIdentityEntry) {
                    .source_hash = source_hash,
                    .stage = stage,
                    .source_size = source_size,
                    .source = copy,
                };
                tracker->source_bytes += source_size;
            }
        }
    }

    if (tracker->record_count < tracker->record_capacity) {
        tracker->records[tracker->record_count++] =
            (PGRAPHVkShaderIdentityRecord) {
                .key_hash = key_hash,
                .source_hash = source_hash,
                .stage = stage,
                .profile_frame = profile_frame,
                .source_class = source_class,
                .generation_us = generation_us,
                .compile_us = compile_us,
                .module_create_us = module_create_us,
                .reflection_us = reflection_us,
            };
    } else {
        tracker->records_saturated = true;
    }
    return source_class;
}

const PGRAPHVkShaderIdentityRecord *pgraph_vk_shader_identity_records(
    const PGRAPHVkShaderIdentityTracker *tracker, size_t *record_count)
{
    if (record_count) {
        *record_count = tracker ? tracker->record_count : 0;
    }
    return tracker ? tracker->records : NULL;
}

bool pgraph_vk_shader_identity_records_saturated(
    const PGRAPHVkShaderIdentityTracker *tracker)
{
    return tracker && tracker->records_saturated;
}

/*
 * NV2A Vulkan first-demand readiness attribution
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "readiness-attribution.h"

static uint64_t readiness_key_hash(const PipelineKey *key)
{
    const uint8_t *bytes = (const uint8_t *)key;
    uint64_t hash = UINT64_C(1469598103934665603);

    for (size_t i = 0; i < sizeof(*key); i++) {
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static PGRAPHVkReadinessRecord *find_record(
    PGRAPHVkReadinessAttributionState *state, uint64_t hash,
    const PipelineKey *key)
{
    for (size_t i = 0; i < G_N_ELEMENTS(state->records); i++) {
        PGRAPHVkReadinessRecord *record = &state->records[i];
        if (record->in_use && record->key_hash == hash &&
            memcmp(&record->key, key, sizeof(*key)) == 0) {
            return record;
        }
    }
    return NULL;
}

static PGRAPHVkReadinessRecord *allocate_record(
    PGRAPHVkReadinessAttributionState *state)
{
    PGRAPHVkReadinessRecord *oldest = NULL;

    for (size_t i = 0; i < G_N_ELEMENTS(state->records); i++) {
        PGRAPHVkReadinessRecord *record = &state->records[i];
        if (!record->in_use) {
            return record;
        }
        if (!oldest || record->last_demand_us < oldest->last_demand_us) {
            oldest = record;
        }
    }
    state->telemetry.tracker_evictions++;
    return oldest;
}

void pgraph_vk_readiness_attribution_init(
    PGRAPHVkReadinessAttributionState *state)
{
    memset(state, 0, sizeof(*state));
}

PGRAPHVkReadinessClass pgraph_vk_readiness_classify_miss(
    bool predicted, bool queue_deferred, bool unsupported)
{
    if (queue_deferred) {
        return PGRAPH_VK_READINESS_QUEUE_DEFERRED;
    }
    if (unsupported) {
        return PGRAPH_VK_READINESS_UNSUPPORTED;
    }
    return predicted ? PGRAPH_VK_READINESS_TOO_LATE :
                       PGRAPH_VK_READINESS_MISSED;
}

bool pgraph_vk_readiness_note_first_demand(
    PGRAPHVkReadinessAttributionState *state, const PipelineKey *key,
    uint64_t generation, uint64_t demand_us,
    PGRAPHVkReadinessClass classification)
{
    if (!state || !key || key->clear ||
        classification >= PGRAPH_VK_READINESS_CLASS_COUNT) {
        return false;
    }

    uint64_t hash = readiness_key_hash(key);
    PGRAPHVkReadinessRecord *record = find_record(state, hash, key);
    if (record && record->generation == generation) {
        record->last_demand_us = demand_us;
        record->demand_count++;
        state->telemetry.duplicate_demands++;
        return false;
    }
    if (record) {
        state->telemetry.generation_reclassifications++;
    } else {
        record = allocate_record(state);
    }

    *record = (PGRAPHVkReadinessRecord) {
        .in_use = true,
        .key = *key,
        .key_hash = hash,
        .generation = generation,
        .first_demand_us = demand_us,
        .last_demand_us = demand_us,
        .demand_count = 1,
        .classification = classification,
    };
    state->telemetry.classified[classification]++;
    return true;
}

const PGRAPHVkReadinessRecord *pgraph_vk_readiness_find(
    const PGRAPHVkReadinessAttributionState *state, const PipelineKey *key,
    uint64_t generation)
{
    if (!state || !key) {
        return NULL;
    }
    uint64_t hash = readiness_key_hash(key);
    for (size_t i = 0; i < G_N_ELEMENTS(state->records); i++) {
        const PGRAPHVkReadinessRecord *record = &state->records[i];
        if (record->in_use && record->generation == generation &&
            record->key_hash == hash &&
            memcmp(&record->key, key, sizeof(*key)) == 0) {
            return record;
        }
    }
    return NULL;
}

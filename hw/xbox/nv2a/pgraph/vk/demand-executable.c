/*
 * NV2A Vulkan retained demand-executable state machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "demand-executable.h"

static uint64_t demand_key_hash(const PipelineKey *key)
{
    const uint8_t *bytes = (const uint8_t *)key;
    uint64_t hash = UINT64_C(1469598103934665603);

    for (size_t i = 0; i < sizeof(*key); i++) {
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static bool
demand_record_is_pending(const PGRAPHVkDemandExecutableRecord *record)
{
    return record->in_use && record->status != PGRAPH_VK_DEMAND_READY &&
           record->status != PGRAPH_VK_DEMAND_FAILED_PERMANENT;
}

static PGRAPHVkDemandExecutableRecord *
find_mutable_record(PGRAPHVkDemandExecutableState *state, uint64_t hash,
                    const PipelineKey *key)
{
    for (size_t i = 0; i < G_N_ELEMENTS(state->records); i++) {
        PGRAPHVkDemandExecutableRecord *record = &state->records[i];
        state->telemetry.record_scans++;
        if (!record->in_use || record->key_hash != hash) {
            continue;
        }
        state->telemetry.full_key_comparisons++;
        if (memcmp(&record->key, key, sizeof(*key)) == 0) {
            return record;
        }
    }
    return NULL;
}

static void finish_pending_record(PGRAPHVkDemandExecutableState *state,
                                  PGRAPHVkDemandExecutableRecord *record)
{
    if (demand_record_is_pending(record)) {
        assert(state->telemetry.pending_demand_executables > 0);
        state->telemetry.pending_demand_executables--;
    }
}

static void mark_permanent_failure(PGRAPHVkDemandExecutableState *state,
                                   PGRAPHVkDemandExecutableRecord *record)
{
    finish_pending_record(state, record);
    if (record->status != PGRAPH_VK_DEMAND_FAILED_PERMANENT) {
        state->telemetry.permanent_failures++;
    }
    record->status = PGRAPH_VK_DEMAND_FAILED_PERMANENT;
    record->retry_after_us = 0;
}

static void mark_ready(PGRAPHVkDemandExecutableState *state,
                       PGRAPHVkDemandExecutableRecord *record, uint64_t now_us)
{
    finish_pending_record(state, record);
    uint64_t latency = now_us >= record->first_demand_us ?
                           now_us - record->first_demand_us :
                           0;
    state->telemetry.first_demand_to_ready_us_total += latency;
    state->telemetry.first_demand_to_ready_us_max =
        MAX(state->telemetry.first_demand_to_ready_us_max, latency);
    record->status = PGRAPH_VK_DEMAND_READY;
    record->retry_after_us = 0;
}

static PGRAPHVkDemandExecutableRecord *
allocate_record(PGRAPHVkDemandExecutableState *state)
{
    PGRAPHVkDemandExecutableRecord *oldest_terminal = NULL;

    for (size_t i = 0; i < G_N_ELEMENTS(state->records); i++) {
        PGRAPHVkDemandExecutableRecord *record = &state->records[i];
        state->telemetry.record_scans++;
        if (!record->in_use) {
            return record;
        }
        if (!demand_record_is_pending(record) &&
            (!oldest_terminal ||
             record->last_demand_us < oldest_terminal->last_demand_us)) {
            oldest_terminal = record;
        }
    }
    return oldest_terminal;
}

void pgraph_vk_demand_executable_state_init(
    PGRAPHVkDemandExecutableState *state)
{
    memset(state, 0, sizeof(*state));
}

PGRAPHVkDemandExecutableResult
pgraph_vk_demand_executable_request(PGRAPHVkDemandExecutableState *state,
                                    const PipelineKey *key, uint64_t generation,
                                    uint64_t first_demand_us, uint64_t now_us)
{
    if (!state || !key || key->clear) {
        return PGRAPH_VK_DEMAND_EXECUTABLE_FAILED;
    }

    state->telemetry.demanded_executables++;
    uint64_t hash = demand_key_hash(key);
    PGRAPHVkDemandExecutableRecord *record =
        find_mutable_record(state, hash, key);
    if (record) {
        state->telemetry.deduplicated_demands++;
        record->last_demand_us = now_us;
        record->demand_count++;
        if (record->generation == generation &&
            demand_record_is_pending(record)) {
            return PGRAPH_VK_DEMAND_EXECUTABLE_QUEUED;
        }
        if (record->generation == generation &&
            record->status == PGRAPH_VK_DEMAND_FAILED_PERMANENT) {
            return PGRAPH_VK_DEMAND_EXECUTABLE_FAILED;
        }
        if (record->generation != generation) {
            finish_pending_record(state, record);
            state->telemetry.stale_generation_discards++;
        }
        record->generation = generation;
        record->first_demand_us = first_demand_us;
        record->demand_count = 1;
        record->pipeline_attempts = 0;
        record->retry_after_us = 0;
        record->status = PGRAPH_VK_DEMAND_WAITING_FOR_MODULES;
        state->telemetry.pending_demand_executables++;
        return PGRAPH_VK_DEMAND_EXECUTABLE_QUEUED;
    }

    record = allocate_record(state);
    if (!record) {
        state->telemetry.deferred_demands++;
        return PGRAPH_VK_DEMAND_EXECUTABLE_DEFERRED;
    }

    *record = (PGRAPHVkDemandExecutableRecord){
        .in_use = true,
        .key = *key,
        .key_hash = hash,
        .generation = generation,
        .first_demand_us = first_demand_us,
        .last_demand_us = now_us,
        .demand_count = 1,
        .status = PGRAPH_VK_DEMAND_WAITING_FOR_MODULES,
    };
    state->telemetry.pending_demand_executables++;
    return PGRAPH_VK_DEMAND_EXECUTABLE_QUEUED;
}

static bool service_missing_modules(PGRAPHVkDemandExecutableState *state,
                                    PGRAPHVkDemandExecutableRecord *record,
                                    uint64_t now_us,
                                    const PGRAPHVkDemandExecutableOps *ops,
                                    void *opaque, uint32_t missing)
{
    bool deferred = false;

    for (unsigned int stage = 0; stage < PGRAPH_VK_DEMAND_STAGE_COUNT;
         stage++) {
        if (!(missing & (1U << stage))) {
            continue;
        }
        PGRAPHVkAsyncModuleRequestResult result = ops->request_module(
            opaque, &record->key, (PGRAPHVkDemandShaderStage)stage);
        switch (result) {
        case PGRAPH_VK_ASYNC_MODULE_READY:
        case PGRAPH_VK_ASYNC_MODULE_ACCEPTED:
        case PGRAPH_VK_ASYNC_MODULE_DUPLICATE:
            break;
        case PGRAPH_VK_ASYNC_MODULE_DEFERRED:
            deferred = true;
            break;
        case PGRAPH_VK_ASYNC_MODULE_FAILED:
            mark_permanent_failure(state, record);
            return false;
        default:
            g_assert_not_reached();
        }
    }

    missing = ops->missing_modules(opaque, &record->key);
    if (!missing) {
        return true;
    }
    record->status = deferred ? PGRAPH_VK_DEMAND_DEFERRED :
                                PGRAPH_VK_DEMAND_WAITING_FOR_MODULES;
    record->retry_after_us = deferred ?
        now_us + PGRAPH_VK_DEMAND_RETRY_US : 0;
    if (deferred) {
        state->telemetry.deferred_demands++;
    }
    return false;
}

void pgraph_vk_demand_executable_service(PGRAPHVkDemandExecutableState *state,
                                         uint64_t generation, uint64_t now_us,
                                         const PGRAPHVkDemandExecutableOps *ops,
                                         void *opaque)
{
    if (!state || !ops || !ops->missing_modules || !ops->request_module ||
        !ops->prepare_binding || !ops->pipeline_ready ||
        !ops->submit_pipeline) {
        return;
    }

    unsigned int visited = 0;
    unsigned int processed = 0;
    while (visited < G_N_ELEMENTS(state->records) && processed < 2) {
        size_t index = state->service_cursor++ % G_N_ELEMENTS(state->records);
        PGRAPHVkDemandExecutableRecord *record = &state->records[index];
        visited++;
        state->telemetry.service_record_scans++;
        if (!record->in_use) {
            continue;
        }
        if (record->generation != generation) {
            finish_pending_record(state, record);
            memset(record, 0, sizeof(*record));
            state->telemetry.stale_generation_discards++;
            continue;
        }
        if (!demand_record_is_pending(record)) {
            continue;
        }
        if (ops->pipeline_ready(opaque, &record->key)) {
            processed++;
            mark_ready(state, record, now_us);
            continue;
        }
        if (now_us < record->retry_after_us) {
            continue;
        }
        if (record->status == PGRAPH_VK_DEMAND_PIPELINE_PENDING) {
            continue;
        }
        processed++;

        uint32_t missing = ops->missing_modules(opaque, &record->key);
        if (missing && !service_missing_modules(state, record, now_us, ops,
                                                opaque, missing)) {
            continue;
        }

        ShaderBinding *binding = NULL;
        switch (ops->prepare_binding(opaque, &record->key, &binding)) {
        case PGRAPH_VK_DEMAND_BINDING_READY:
            if (!binding) {
                mark_permanent_failure(state, record);
                continue;
            }
            break;
        case PGRAPH_VK_DEMAND_BINDING_DEFERRED:
            record->status = PGRAPH_VK_DEMAND_WAITING_FOR_BINDING;
            record->retry_after_us = now_us + PGRAPH_VK_DEMAND_RETRY_US;
            continue;
        case PGRAPH_VK_DEMAND_BINDING_FAILED:
            mark_permanent_failure(state, record);
            continue;
        default:
            g_assert_not_reached();
        }

        if (ops->pipeline_ready(opaque, &record->key)) {
            mark_ready(state, record, now_us);
            continue;
        }
        switch (ops->submit_pipeline(opaque, &record->key, binding)) {
        case PGRAPH_VK_HYBRID_PIPELINE_ACCEPTED:
            record->pipeline_attempts++;
            record->status = PGRAPH_VK_DEMAND_PIPELINE_PENDING;
            record->retry_after_us = 0;
            break;
        case PGRAPH_VK_HYBRID_PIPELINE_QUEUE_FULL:
            record->status = PGRAPH_VK_DEMAND_DEFERRED;
            record->retry_after_us = now_us + PGRAPH_VK_DEMAND_RETRY_US;
            state->telemetry.deferred_demands++;
            break;
        case PGRAPH_VK_HYBRID_PIPELINE_STOPPED:
        case PGRAPH_VK_HYBRID_PIPELINE_UNSUPPORTED_RECIPE:
            mark_permanent_failure(state, record);
            break;
        default:
            g_assert_not_reached();
        }
    }
}

void pgraph_vk_demand_executable_note_pipeline_failure(
    PGRAPHVkDemandExecutableState *state, const PipelineKey *key,
    uint64_t generation, uint64_t now_us)
{
    if (!state || !key) {
        return;
    }
    PGRAPHVkDemandExecutableRecord *record =
        find_mutable_record(state, demand_key_hash(key), key);
    if (!record || record->generation != generation ||
        record->status != PGRAPH_VK_DEMAND_PIPELINE_PENDING) {
        return;
    }
    if (record->pipeline_attempts >= PGRAPH_VK_DEMAND_PIPELINE_MAX_ATTEMPTS) {
        mark_permanent_failure(state, record);
        return;
    }
    record->status = PGRAPH_VK_DEMAND_DEFERRED;
    record->retry_after_us = now_us + PGRAPH_VK_DEMAND_RETRY_US;
    state->telemetry.deferred_demands++;
}

const PGRAPHVkDemandExecutableRecord *
pgraph_vk_demand_executable_find(const PGRAPHVkDemandExecutableState *state,
                                 const PipelineKey *key)
{
    if (!state || !key) {
        return NULL;
    }
    uint64_t hash = demand_key_hash(key);
    for (size_t i = 0; i < G_N_ELEMENTS(state->records); i++) {
        const PGRAPHVkDemandExecutableRecord *record = &state->records[i];
        if (record->in_use && record->key_hash == hash &&
            memcmp(&record->key, key, sizeof(*key)) == 0) {
            return record;
        }
    }
    return NULL;
}

bool pgraph_vk_demand_executable_has_pending(
    const PGRAPHVkDemandExecutableState *state)
{
    return state && state->telemetry.pending_demand_executables != 0;
}

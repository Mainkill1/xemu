/*
 * NV2A Vulkan retained demand-executable renderer integration
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/atomic.h"
#include "qemu/fast-hash.h"

#include "demand-executable.h"
#include "hybrid-ready.h"

static VkShaderStageFlagBits demand_stage_to_vk(PGRAPHVkDemandShaderStage stage)
{
    switch (stage) {
    case PGRAPH_VK_DEMAND_STAGE_VERTEX:
        return VK_SHADER_STAGE_VERTEX_BIT;
    case PGRAPH_VK_DEMAND_STAGE_GEOMETRY:
        return VK_SHADER_STAGE_GEOMETRY_BIT;
    case PGRAPH_VK_DEMAND_STAGE_FRAGMENT:
        return VK_SHADER_STAGE_FRAGMENT_BIT;
    default:
        return 0;
    }
}

static uint32_t renderer_missing_modules(void *opaque, const PipelineKey *key)
{
    PGRAPHState *pg = opaque;
    uint32_t missing = 0;

    if (!pgraph_vk_shader_state_module_ready(pg, &key->shader_state,
                                             key->fragment_route,
                                             VK_SHADER_STAGE_VERTEX_BIT)) {
        missing |= PGRAPH_VK_DEMAND_STAGE_VERTEX_BIT;
    }
    if (pgraph_glsl_need_geom(&key->shader_state.geom) &&
        !pgraph_vk_shader_state_module_ready(pg, &key->shader_state,
                                             key->fragment_route,
                                             VK_SHADER_STAGE_GEOMETRY_BIT)) {
        missing |= PGRAPH_VK_DEMAND_STAGE_GEOMETRY_BIT;
    }
    if (!pgraph_vk_shader_state_module_ready(pg, &key->shader_state,
                                             key->fragment_route,
                                             VK_SHADER_STAGE_FRAGMENT_BIT)) {
        missing |= PGRAPH_VK_DEMAND_STAGE_FRAGMENT_BIT;
    }
    return missing;
}

static PGRAPHVkAsyncModuleRequestResult
renderer_request_module(void *opaque, const PipelineKey *key,
                        PGRAPHVkDemandShaderStage stage)
{
    VkShaderStageFlagBits vk_stage = demand_stage_to_vk(stage);
    if (!vk_stage) {
        return PGRAPH_VK_ASYNC_MODULE_FAILED;
    }
    return pgraph_vk_request_shader_state_module_async(
        opaque, &key->shader_state, key->fragment_route, vk_stage);
}

static PGRAPHVkDemandBindingResult
renderer_prepare_binding(void *opaque, const PipelineKey *key,
                         ShaderBinding **binding)
{
    *binding = pgraph_vk_prepare_binding_from_ready_modules(
        opaque, &key->shader_state, key->fragment_route);
    return *binding ? PGRAPH_VK_DEMAND_BINDING_READY :
                      PGRAPH_VK_DEMAND_BINDING_DEFERRED;
}

static bool renderer_pipeline_ready(void *opaque, const PipelineKey *key)
{
    PGRAPHState *pg = opaque;
    PGRAPHVkState *r = pg->vk_renderer_state;
    uint64_t hash = fast_hash((const uint8_t *)key, sizeof(*key));
    return pgraph_vk_pipeline_cache_find_ready(&r->pipeline_cache, hash, key) !=
           NULL;
}

static PGRAPHVkHybridPipelineSubmitResult
renderer_submit_pipeline(void *opaque, const PipelineKey *key,
                         ShaderBinding *binding)
{
    return pgraph_vk_request_retained_demand_pipeline(opaque, key, binding);
}

static const PGRAPHVkDemandExecutableOps renderer_demand_ops = {
    .missing_modules = renderer_missing_modules,
    .request_module = renderer_request_module,
    .prepare_binding = renderer_prepare_binding,
    .pipeline_ready = renderer_pipeline_ready,
    .submit_pipeline = renderer_submit_pipeline,
};

static void sync_atomic_snapshot(PGRAPHVkState *r)
{
    const PGRAPHVkDemandExecutableTelemetry *telemetry =
        &r->demand_executables->telemetry;
    qatomic_set(&r->demand_executable_snapshot.demanded_executables,
                telemetry->demanded_executables);
    qatomic_set(&r->demand_executable_snapshot.deduplicated_demands,
                telemetry->deduplicated_demands);
    qatomic_set(&r->demand_executable_snapshot.deferred_demands,
                telemetry->deferred_demands);
    qatomic_set(&r->demand_executable_snapshot.permanent_failures,
                telemetry->permanent_failures);
    qatomic_set(&r->demand_executable_snapshot.first_demand_to_ready_us_total,
                telemetry->first_demand_to_ready_us_total);
    qatomic_set(&r->demand_executable_snapshot.first_demand_to_ready_us_max,
                telemetry->first_demand_to_ready_us_max);
    qatomic_set(&r->demand_executable_snapshot.pending_demand_executables,
                telemetry->pending_demand_executables);
}

void pgraph_vk_init_demand_executables(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    r->demand_executables = g_new0(PGRAPHVkDemandExecutableState, 1);
    pgraph_vk_demand_executable_state_init(r->demand_executables);
    sync_atomic_snapshot(r);
}

void pgraph_vk_finalize_demand_executables(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    g_clear_pointer(&r->demand_executables, g_free);
}

PGRAPHVkDemandExecutableResult
pgraph_vk_request_demand_executable(PGRAPHState *pg, const PipelineKey *key,
                                    uint64_t first_demand_us)
{
    if (!pg || !pg->vk_renderer_state || !key) {
        return PGRAPH_VK_DEMAND_EXECUTABLE_FAILED;
    }
    PGRAPHVkState *r = pg->vk_renderer_state;
    if (!r->demand_executables || !r->hybrid_compiler_initialized ||
        !r->hybrid_pipeline_builder_initialized) {
        return PGRAPH_VK_DEMAND_EXECUTABLE_FAILED;
    }
    if (renderer_pipeline_ready(pg, key)) {
        return PGRAPH_VK_DEMAND_EXECUTABLE_READY;
    }

    uint64_t now_us = g_get_monotonic_time();
    PGRAPHVkDemandExecutableResult result = pgraph_vk_demand_executable_request(
        r->demand_executables, key, r->hybrid_generation, first_demand_us,
        now_us);
    if (result == PGRAPH_VK_DEMAND_EXECUTABLE_QUEUED) {
        pgraph_vk_demand_executable_service(r->demand_executables,
                                            r->hybrid_generation, now_us,
                                            &renderer_demand_ops, pg);
        const PGRAPHVkDemandExecutableRecord *record =
            pgraph_vk_demand_executable_find(r->demand_executables, key);
        if (record && record->status == PGRAPH_VK_DEMAND_READY) {
            result = PGRAPH_VK_DEMAND_EXECUTABLE_READY;
        } else if (record &&
                   record->status == PGRAPH_VK_DEMAND_FAILED_PERMANENT) {
            result = PGRAPH_VK_DEMAND_EXECUTABLE_FAILED;
        }
    }
    sync_atomic_snapshot(r);
    pgraph_vk_blackout_runtime_sync_demand(pg);
    return result;
}

void pgraph_vk_service_demand_executables(PGRAPHState *pg)
{
    if (!pg || !pg->vk_renderer_state) {
        return;
    }
    PGRAPHVkState *r = pg->vk_renderer_state;
    if (!r->demand_executables || !r->hybrid_compiler_initialized ||
        !r->hybrid_pipeline_builder_initialized) {
        return;
    }
    pgraph_vk_demand_executable_service(
        r->demand_executables, r->hybrid_generation, g_get_monotonic_time(),
        &renderer_demand_ops, pg);
    sync_atomic_snapshot(r);
    pgraph_vk_blackout_runtime_sync_demand(pg);
}

void pgraph_vk_note_demand_pipeline_failure(PGRAPHState *pg,
                                            const PipelineKey *key,
                                            uint64_t generation,
                                            uint64_t now_us)
{
    if (!pg || !pg->vk_renderer_state || !key) {
        return;
    }
    PGRAPHVkState *r = pg->vk_renderer_state;
    if (!r->demand_executables) {
        return;
    }
    pgraph_vk_demand_executable_note_pipeline_failure(r->demand_executables,
                                                      key, generation, now_us);
    sync_atomic_snapshot(r);
    pgraph_vk_blackout_runtime_sync_demand(pg);
}

bool pgraph_vk_demand_work_pending(PGRAPHState *pg)
{
    return pg && pg->vk_renderer_state &&
           pgraph_vk_demand_executable_has_pending(
               pg->vk_renderer_state->demand_executables);
}

PGRAPHVkDemandExecutableTelemetry
pgraph_vk_demand_executable_telemetry_snapshot(PGRAPHState *pg)
{
    PGRAPHVkDemandExecutableTelemetry snapshot = { 0 };
    if (!pg || !pg->vk_renderer_state) {
        return snapshot;
    }
    PGRAPHVkDemandExecutableAtomicSnapshot *atomic =
        &pg->vk_renderer_state->demand_executable_snapshot;
    snapshot.demanded_executables =
        qatomic_read_u64(&atomic->demanded_executables);
    snapshot.deduplicated_demands =
        qatomic_read_u64(&atomic->deduplicated_demands);
    snapshot.deferred_demands = qatomic_read_u64(&atomic->deferred_demands);
    snapshot.permanent_failures = qatomic_read_u64(&atomic->permanent_failures);
    snapshot.first_demand_to_ready_us_total =
        qatomic_read_u64(&atomic->first_demand_to_ready_us_total);
    snapshot.first_demand_to_ready_us_max =
        qatomic_read_u64(&atomic->first_demand_to_ready_us_max);
    snapshot.pending_demand_executables =
        qatomic_read(&atomic->pending_demand_executables);
    return snapshot;
}

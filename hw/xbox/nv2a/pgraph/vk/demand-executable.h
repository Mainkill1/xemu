/*
 * NV2A Vulkan retained demand-executable state machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_XBOX_NV2A_PGRAPH_VK_DEMAND_EXECUTABLE_H
#define HW_XBOX_NV2A_PGRAPH_VK_DEMAND_EXECUTABLE_H

#include "hw/xbox/nv2a/pgraph/vk/renderer.h"

#define PGRAPH_VK_MAX_DEMAND_EXECUTABLES 64U
#define PGRAPH_VK_DEMAND_PIPELINE_MAX_ATTEMPTS 3U
#define PGRAPH_VK_DEMAND_RETRY_US 16000U

typedef enum PGRAPHVkDemandExecutableStatus {
    PGRAPH_VK_DEMAND_WAITING_FOR_MODULES,
    PGRAPH_VK_DEMAND_WAITING_FOR_BINDING,
    PGRAPH_VK_DEMAND_PIPELINE_PENDING,
    PGRAPH_VK_DEMAND_READY,
    PGRAPH_VK_DEMAND_DEFERRED,
    PGRAPH_VK_DEMAND_FAILED_PERMANENT,
} PGRAPHVkDemandExecutableStatus;

typedef enum PGRAPHVkDemandExecutableResult {
    PGRAPH_VK_DEMAND_EXECUTABLE_READY,
    PGRAPH_VK_DEMAND_EXECUTABLE_QUEUED,
    PGRAPH_VK_DEMAND_EXECUTABLE_DEFERRED,
    PGRAPH_VK_DEMAND_EXECUTABLE_FAILED,
} PGRAPHVkDemandExecutableResult;

typedef enum PGRAPHVkDemandShaderStage {
    PGRAPH_VK_DEMAND_STAGE_VERTEX,
    PGRAPH_VK_DEMAND_STAGE_GEOMETRY,
    PGRAPH_VK_DEMAND_STAGE_FRAGMENT,
    PGRAPH_VK_DEMAND_STAGE_COUNT,
} PGRAPHVkDemandShaderStage;

typedef enum PGRAPHVkDemandShaderStageBits {
    PGRAPH_VK_DEMAND_STAGE_VERTEX_BIT = 1U << PGRAPH_VK_DEMAND_STAGE_VERTEX,
    PGRAPH_VK_DEMAND_STAGE_GEOMETRY_BIT = 1U << PGRAPH_VK_DEMAND_STAGE_GEOMETRY,
    PGRAPH_VK_DEMAND_STAGE_FRAGMENT_BIT = 1U << PGRAPH_VK_DEMAND_STAGE_FRAGMENT,
} PGRAPHVkDemandShaderStageBits;

typedef enum PGRAPHVkDemandBindingResult {
    PGRAPH_VK_DEMAND_BINDING_READY,
    PGRAPH_VK_DEMAND_BINDING_DEFERRED,
    PGRAPH_VK_DEMAND_BINDING_FAILED,
} PGRAPHVkDemandBindingResult;

typedef struct PGRAPHVkDemandExecutableRecord {
    bool in_use;
    PipelineKey key;
    uint64_t key_hash;
    uint64_t generation;
    uint64_t first_demand_us;
    uint64_t last_demand_us;
    uint64_t demand_count;
    uint64_t retry_after_us;
    uint32_t pipeline_attempts;
    PGRAPHVkDemandExecutableStatus status;
} PGRAPHVkDemandExecutableRecord;

typedef struct PGRAPHVkDemandExecutableTelemetry {
    uint64_t demanded_executables;
    uint64_t deduplicated_demands;
    uint64_t deferred_demands;
    uint64_t permanent_failures;
    uint64_t stale_generation_discards;
    uint64_t first_demand_to_ready_us_total;
    uint64_t first_demand_to_ready_us_max;
    uint32_t pending_demand_executables;
} PGRAPHVkDemandExecutableTelemetry;

typedef struct PGRAPHVkDemandExecutableState {
    PGRAPHVkDemandExecutableRecord records[PGRAPH_VK_MAX_DEMAND_EXECUTABLES];
    PGRAPHVkDemandExecutableTelemetry telemetry;
    uint32_t service_cursor;
} PGRAPHVkDemandExecutableState;

typedef struct PGRAPHVkDemandExecutableOps {
    uint32_t (*missing_modules)(void *opaque, const PipelineKey *key);
    PGRAPHVkAsyncModuleRequestResult (*request_module)(
        void *opaque, const PipelineKey *key, PGRAPHVkDemandShaderStage stage);
    PGRAPHVkDemandBindingResult (*prepare_binding)(void *opaque,
                                                   const PipelineKey *key,
                                                   ShaderBinding **binding);
    bool (*pipeline_ready)(void *opaque, const PipelineKey *key);
    PGRAPHVkHybridPipelineSubmitResult (*submit_pipeline)(
        void *opaque, const PipelineKey *key, ShaderBinding *binding);
} PGRAPHVkDemandExecutableOps;

void pgraph_vk_demand_executable_state_init(
    PGRAPHVkDemandExecutableState *state);
PGRAPHVkDemandExecutableResult
pgraph_vk_demand_executable_request(PGRAPHVkDemandExecutableState *state,
                                    const PipelineKey *key, uint64_t generation,
                                    uint64_t first_demand_us, uint64_t now_us);
void pgraph_vk_demand_executable_service(PGRAPHVkDemandExecutableState *state,
                                         uint64_t generation, uint64_t now_us,
                                         const PGRAPHVkDemandExecutableOps *ops,
                                         void *opaque);
void pgraph_vk_demand_executable_note_pipeline_failure(
    PGRAPHVkDemandExecutableState *state, const PipelineKey *key,
    uint64_t generation, uint64_t now_us);
const PGRAPHVkDemandExecutableRecord *
pgraph_vk_demand_executable_find(const PGRAPHVkDemandExecutableState *state,
                                 const PipelineKey *key);
bool pgraph_vk_demand_executable_has_pending(
    const PGRAPHVkDemandExecutableState *state);

void pgraph_vk_init_demand_executables(PGRAPHState *pg);
void pgraph_vk_finalize_demand_executables(PGRAPHState *pg);
PGRAPHVkDemandExecutableResult
pgraph_vk_request_demand_executable(PGRAPHState *pg, const PipelineKey *key,
                                    uint64_t first_demand_us);
void pgraph_vk_service_demand_executables(PGRAPHState *pg);
void pgraph_vk_note_demand_pipeline_failure(PGRAPHState *pg,
                                            const PipelineKey *key,
                                            uint64_t generation,
                                            uint64_t now_us);
bool pgraph_vk_demand_work_pending(PGRAPHState *pg);
PGRAPHVkDemandExecutableTelemetry
pgraph_vk_demand_executable_telemetry_snapshot(PGRAPHState *pg);

#endif

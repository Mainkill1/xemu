/*
 * NV2A Vulkan retained demand-executable tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/xbox/nv2a/pgraph/vk/demand-executable.h"

typedef struct DemandFixture {
    uint32_t missing_modules;
    PGRAPHVkAsyncModuleRequestResult module_result[3];
    PGRAPHVkDemandBindingResult binding_result;
    ShaderBinding binding;
    bool pipeline_ready;
    PGRAPHVkHybridPipelineSubmitResult submit_result;
    unsigned int module_calls[3];
    unsigned int binding_calls;
    unsigned int pipeline_ready_calls;
    unsigned int submit_calls;
} DemandFixture;

static uint32_t missing_modules(void *opaque, const PipelineKey *key)
{
    DemandFixture *fixture = opaque;
    (void)key;
    return fixture->missing_modules;
}

static PGRAPHVkAsyncModuleRequestResult
request_module(void *opaque, const PipelineKey *key,
               PGRAPHVkDemandShaderStage stage)
{
    DemandFixture *fixture = opaque;
    (void)key;
    fixture->module_calls[stage]++;
    return fixture->module_result[stage];
}

static PGRAPHVkDemandBindingResult
prepare_binding(void *opaque, const PipelineKey *key, ShaderBinding **binding)
{
    DemandFixture *fixture = opaque;
    (void)key;
    fixture->binding_calls++;
    *binding = fixture->binding_result == PGRAPH_VK_DEMAND_BINDING_READY ?
                   &fixture->binding :
                   NULL;
    return fixture->binding_result;
}

static bool pipeline_ready(void *opaque, const PipelineKey *key)
{
    DemandFixture *fixture = opaque;
    (void)key;
    fixture->pipeline_ready_calls++;
    return fixture->pipeline_ready;
}

static PGRAPHVkHybridPipelineSubmitResult
submit_pipeline(void *opaque, const PipelineKey *key, ShaderBinding *binding)
{
    DemandFixture *fixture = opaque;
    (void)key;
    g_assert_true(binding == &fixture->binding);
    fixture->submit_calls++;
    return fixture->submit_result;
}

static const PGRAPHVkDemandExecutableOps demand_ops = {
    .missing_modules = missing_modules,
    .request_module = request_module,
    .prepare_binding = prepare_binding,
    .pipeline_ready = pipeline_ready,
    .submit_pipeline = submit_pipeline,
};

static PipelineKey test_key(uint32_t identity)
{
    PipelineKey key = { 0 };
    key.fragment_route = PGRAPH_VK_FRAGMENT_SPECIALIZED;
    key.regs[0] = identity;
    key.render_pass_state.color_format = VK_FORMAT_B8G8R8A8_UNORM;
    return key;
}

static DemandFixture default_fixture(void)
{
    return (DemandFixture) {
        .module_result = {
            PGRAPH_VK_ASYNC_MODULE_ACCEPTED,
            PGRAPH_VK_ASYNC_MODULE_ACCEPTED,
            PGRAPH_VK_ASYNC_MODULE_ACCEPTED,
        },
        .binding_result = PGRAPH_VK_DEMAND_BINDING_READY,
        .submit_result = PGRAPH_VK_HYBRID_PIPELINE_ACCEPTED,
    };
}

static const PGRAPHVkDemandExecutableRecord *
find_record(const PGRAPHVkDemandExecutableState *state, const PipelineKey *key)
{
    const PGRAPHVkDemandExecutableRecord *record =
        pgraph_vk_demand_executable_find(state, key);
    g_assert_nonnull(record);
    return record;
}

static void test_stage_progression_without_flip(void)
{
    PGRAPHVkDemandExecutableState state;
    pgraph_vk_demand_executable_state_init(&state);
    PipelineKey key = test_key(1);
    DemandFixture fixture = default_fixture();
    fixture.missing_modules = PGRAPH_VK_DEMAND_STAGE_VERTEX_BIT;

    g_assert_cmpint(
        pgraph_vk_demand_executable_request(&state, &key, 7, 100, 100), ==,
        PGRAPH_VK_DEMAND_EXECUTABLE_QUEUED);
    pgraph_vk_demand_executable_service(&state, 7, 100, &demand_ops, &fixture);
    g_assert_cmpuint(fixture.module_calls[PGRAPH_VK_DEMAND_STAGE_VERTEX], ==,
                     1);
    g_assert_cmpint(find_record(&state, &key)->status, ==,
                    PGRAPH_VK_DEMAND_WAITING_FOR_MODULES);

    fixture.missing_modules = PGRAPH_VK_DEMAND_STAGE_GEOMETRY_BIT |
                              PGRAPH_VK_DEMAND_STAGE_FRAGMENT_BIT;
    pgraph_vk_demand_executable_service(&state, 7, 20000, &demand_ops,
                                        &fixture);
    g_assert_cmpuint(fixture.module_calls[PGRAPH_VK_DEMAND_STAGE_GEOMETRY], ==,
                     1);
    g_assert_cmpuint(fixture.module_calls[PGRAPH_VK_DEMAND_STAGE_FRAGMENT], ==,
                     1);

    fixture.missing_modules = 0;
    fixture.binding_result = PGRAPH_VK_DEMAND_BINDING_DEFERRED;
    pgraph_vk_demand_executable_service(&state, 7, 40000, &demand_ops,
                                        &fixture);
    g_assert_cmpint(find_record(&state, &key)->status, ==,
                    PGRAPH_VK_DEMAND_WAITING_FOR_BINDING);

    fixture.binding_result = PGRAPH_VK_DEMAND_BINDING_READY;
    pgraph_vk_demand_executable_service(&state, 7, 60000, &demand_ops,
                                        &fixture);
    g_assert_cmpuint(fixture.submit_calls, ==, 1);
    g_assert_cmpint(find_record(&state, &key)->status, ==,
                    PGRAPH_VK_DEMAND_PIPELINE_PENDING);

    fixture.pipeline_ready = true;
    pgraph_vk_demand_executable_service(&state, 7, 70000, &demand_ops,
                                        &fixture);
    g_assert_cmpuint(fixture.submit_calls, ==, 1);
    g_assert_cmpint(find_record(&state, &key)->status, ==,
                    PGRAPH_VK_DEMAND_READY);
    g_assert_cmpuint(state.telemetry.pending_demand_executables, ==, 0);
    g_assert_cmpuint(state.telemetry.first_demand_to_ready_us_total, ==, 69900);
}

static void test_pending_key_deduplicates(void)
{
    PGRAPHVkDemandExecutableState state;
    pgraph_vk_demand_executable_state_init(&state);
    PipelineKey key = test_key(2);
    DemandFixture fixture = default_fixture();

    g_assert_cmpint(
        pgraph_vk_demand_executable_request(&state, &key, 1, 10, 10), ==,
        PGRAPH_VK_DEMAND_EXECUTABLE_QUEUED);
    pgraph_vk_demand_executable_service(&state, 1, 10, &demand_ops, &fixture);
    g_assert_cmpuint(fixture.submit_calls, ==, 1);

    g_assert_cmpint(
        pgraph_vk_demand_executable_request(&state, &key, 1, 10, 20), ==,
        PGRAPH_VK_DEMAND_EXECUTABLE_QUEUED);
    pgraph_vk_demand_executable_service(&state, 1, 20, &demand_ops, &fixture);
    g_assert_cmpuint(fixture.submit_calls, ==, 1);
    g_assert_cmpuint(find_record(&state, &key)->demand_count, ==, 2);
    g_assert_cmpuint(state.telemetry.deduplicated_demands, ==, 1);
}

static void test_full_pending_table_defers_without_callbacks(void)
{
    PGRAPHVkDemandExecutableState state;
    pgraph_vk_demand_executable_state_init(&state);

    for (uint32_t i = 0; i < PGRAPH_VK_MAX_DEMAND_EXECUTABLES; i++) {
        PipelineKey key = test_key(i + 1);
        g_assert_cmpint(
            pgraph_vk_demand_executable_request(&state, &key, 1, i, i), ==,
            PGRAPH_VK_DEMAND_EXECUTABLE_QUEUED);
    }
    PipelineKey overflow = test_key(PGRAPH_VK_MAX_DEMAND_EXECUTABLES + 1);
    g_assert_cmpint(
        pgraph_vk_demand_executable_request(&state, &overflow, 1, 100, 100), ==,
        PGRAPH_VK_DEMAND_EXECUTABLE_DEFERRED);
    g_assert_cmpuint(state.telemetry.pending_demand_executables, ==,
                     PGRAPH_VK_MAX_DEMAND_EXECUTABLES);
    g_assert_cmpuint(state.telemetry.deferred_demands, ==, 1);
}

static void test_permanent_module_failure(void)
{
    PGRAPHVkDemandExecutableState state;
    pgraph_vk_demand_executable_state_init(&state);
    PipelineKey key = test_key(3);
    DemandFixture fixture = default_fixture();
    fixture.missing_modules = PGRAPH_VK_DEMAND_STAGE_FRAGMENT_BIT;
    fixture.module_result[PGRAPH_VK_DEMAND_STAGE_FRAGMENT] =
        PGRAPH_VK_ASYNC_MODULE_FAILED;

    pgraph_vk_demand_executable_request(&state, &key, 1, 0, 0);
    pgraph_vk_demand_executable_service(&state, 1, 0, &demand_ops, &fixture);
    g_assert_cmpint(find_record(&state, &key)->status, ==,
                    PGRAPH_VK_DEMAND_FAILED_PERMANENT);
    g_assert_cmpuint(state.telemetry.permanent_failures, ==, 1);
    g_assert_cmpuint(state.telemetry.pending_demand_executables, ==, 0);
    g_assert_cmpuint(fixture.submit_calls, ==, 0);
}

static void test_pipeline_queue_full_defers_without_retrying_early(void)
{
    PGRAPHVkDemandExecutableState state;
    pgraph_vk_demand_executable_state_init(&state);
    PipelineKey key = test_key(6);
    DemandFixture fixture = default_fixture();
    fixture.submit_result = PGRAPH_VK_HYBRID_PIPELINE_QUEUE_FULL;

    pgraph_vk_demand_executable_request(&state, &key, 1, 0, 0);
    pgraph_vk_demand_executable_service(&state, 1, 0, &demand_ops, &fixture);
    g_assert_cmpuint(fixture.submit_calls, ==, 1);
    g_assert_cmpint(find_record(&state, &key)->status, ==,
                    PGRAPH_VK_DEMAND_DEFERRED);
    g_assert_cmpuint(find_record(&state, &key)->pipeline_attempts, ==, 0);

    pgraph_vk_demand_executable_service(&state, 1, 1000, &demand_ops, &fixture);
    g_assert_cmpuint(fixture.submit_calls, ==, 1);
    pgraph_vk_demand_executable_service(&state, 1, 20000, &demand_ops,
                                        &fixture);
    g_assert_cmpuint(fixture.submit_calls, ==, 2);
}

static void test_pipeline_failure_retries_are_bounded(void)
{
    PGRAPHVkDemandExecutableState state;
    pgraph_vk_demand_executable_state_init(&state);
    PipelineKey key = test_key(4);
    DemandFixture fixture = default_fixture();

    pgraph_vk_demand_executable_request(&state, &key, 1, 0, 0);
    for (unsigned int attempt = 0; attempt < 3; attempt++) {
        uint64_t now_us = attempt * 20000;
        pgraph_vk_demand_executable_service(&state, 1, now_us, &demand_ops,
                                            &fixture);
        g_assert_cmpuint(fixture.submit_calls, ==, attempt + 1);
        pgraph_vk_demand_executable_note_pipeline_failure(&state, &key, 1,
                                                          now_us);
    }
    g_assert_cmpint(find_record(&state, &key)->status, ==,
                    PGRAPH_VK_DEMAND_FAILED_PERMANENT);
    g_assert_cmpuint(state.telemetry.permanent_failures, ==, 1);
}

static void test_stale_generation_is_discarded(void)
{
    PGRAPHVkDemandExecutableState state;
    pgraph_vk_demand_executable_state_init(&state);
    PipelineKey key = test_key(5);
    DemandFixture fixture = default_fixture();

    pgraph_vk_demand_executable_request(&state, &key, 8, 0, 0);
    pgraph_vk_demand_executable_service(&state, 9, 0, &demand_ops, &fixture);
    g_assert_null(pgraph_vk_demand_executable_find(&state, &key));
    g_assert_cmpuint(state.telemetry.stale_generation_discards, ==, 1);
    g_assert_cmpuint(state.telemetry.pending_demand_executables, ==, 0);
    g_assert_cmpuint(fixture.submit_calls, ==, 0);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/vk/demand-executable/stage-progression",
                    test_stage_progression_without_flip);
    g_test_add_func("/xbox/vk/demand-executable/deduplicate",
                    test_pending_key_deduplicates);
    g_test_add_func("/xbox/vk/demand-executable/full-table",
                    test_full_pending_table_defers_without_callbacks);
    g_test_add_func("/xbox/vk/demand-executable/module-failure",
                    test_permanent_module_failure);
    g_test_add_func("/xbox/vk/demand-executable/pipeline-queue-full",
                    test_pipeline_queue_full_defers_without_retrying_early);
    g_test_add_func("/xbox/vk/demand-executable/pipeline-retry",
                    test_pipeline_failure_retries_are_bounded);
    g_test_add_func("/xbox/vk/demand-executable/stale-generation",
                    test_stale_generation_is_discarded);
    return g_test_run();
}

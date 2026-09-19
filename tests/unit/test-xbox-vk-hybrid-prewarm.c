/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"

#include "hw/xbox/nv2a/pgraph/vk/hybrid-prewarm.h"

typedef struct AttemptFixture {
    PGRAPHVkHybridPrewarmAttemptResult result;
    unsigned int calls;
} AttemptFixture;

static PGRAPHVkHybridPrewarmAttemptResult attempt_one(void *opaque)
{
    AttemptFixture *fixture = opaque;
    fixture->calls++;
    return fixture->result;
}

static void test_demand_has_priority(void)
{
    PGRAPHVkHybridPrewarmState state = { .enabled = true };
    AttemptFixture fixture = {
        .result = PGRAPH_VK_HYBRID_PREWARM_SUBMITTED,
    };

    g_assert_cmpint(pgraph_vk_hybrid_prewarm_service(
                        &state, true, attempt_one, &fixture),
                    ==, PGRAPH_VK_HYBRID_PREWARM_IDLE);
    g_assert_cmpuint(fixture.calls, ==, 0);
    g_assert_cmpuint(state.attempted, ==, 0);
}

static void test_one_candidate_per_service(void)
{
    PGRAPHVkHybridPrewarmState state = { .enabled = true };
    AttemptFixture fixture = {
        .result = PGRAPH_VK_HYBRID_PREWARM_SUBMITTED,
    };

    g_assert_cmpint(pgraph_vk_hybrid_prewarm_service(
                        &state, false, attempt_one, &fixture),
                    ==, PGRAPH_VK_HYBRID_PREWARM_SUBMITTED);
    g_assert_cmpuint(fixture.calls, ==, 1);
    g_assert_cmpuint(state.attempted, ==, 1);
    g_assert_cmpuint(state.scheduled, ==, 1);
}

static void test_launch_bound(void)
{
    PGRAPHVkHybridPrewarmState state = {
        .enabled = true,
        .attempted = PGRAPH_VK_HYBRID_PREWARM_MAX_CANDIDATES,
    };
    AttemptFixture fixture = {
        .result = PGRAPH_VK_HYBRID_PREWARM_SUBMITTED,
    };

    g_assert_cmpint(pgraph_vk_hybrid_prewarm_service(
                        &state, false, attempt_one, &fixture),
                    ==, PGRAPH_VK_HYBRID_PREWARM_IDLE);
    g_assert_cmpuint(fixture.calls, ==, 0);
}

static void test_outcome_accounting(void)
{
    PGRAPHVkHybridPrewarmState state = { .enabled = true };
    AttemptFixture fixture = { 0 };
    const struct {
        PGRAPHVkHybridPrewarmAttemptResult result;
        uint32_t *counter;
    } cases[] = {
        { PGRAPH_VK_HYBRID_PREWARM_READY, &state.ready },
        { PGRAPH_VK_HYBRID_PREWARM_MISSING_ARTIFACT, &state.missing },
        { PGRAPH_VK_HYBRID_PREWARM_REJECTED, &state.rejected },
        { PGRAPH_VK_HYBRID_PREWARM_DEFERRED, &state.deferred },
    };

    for (size_t i = 0; i < G_N_ELEMENTS(cases); i++) {
        fixture.result = cases[i].result;
        g_assert_cmpint(pgraph_vk_hybrid_prewarm_service(
                            &state, false, attempt_one, &fixture),
                        ==, cases[i].result);
        g_assert_cmpuint(*cases[i].counter, ==, 1);
    }

    unsigned int attempted = state.attempted;
    g_assert_cmpint(pgraph_vk_hybrid_prewarm_service(
                        &state, false, attempt_one, &fixture),
                    ==, PGRAPH_VK_HYBRID_PREWARM_IDLE);
    g_assert_cmpuint(fixture.calls, ==, G_N_ELEMENTS(cases));
    g_assert_cmpuint(state.attempted, ==, attempted);
    state.defer_services = 0;
    fixture.result = PGRAPH_VK_HYBRID_PREWARM_NO_CANDIDATE;
    g_assert_cmpint(pgraph_vk_hybrid_prewarm_service(
                        &state, false, attempt_one, &fixture),
                    ==, PGRAPH_VK_HYBRID_PREWARM_NO_CANDIDATE);
    g_assert_cmpuint(state.attempted, ==, attempted);
}

typedef struct StageFixture {
    PGRAPHVkHybridPrewarmStage stages[3];
    unsigned int count;
    PGRAPHVkHybridPrewarmStage fail_stage;
    PGRAPHVkCachedFamilyModulesResult failure;
} StageFixture;

static PGRAPHVkCachedFamilyModulesResult materialize_stage(
    void *opaque, PGRAPHVkHybridPrewarmStage stage)
{
    StageFixture *fixture = opaque;
    fixture->stages[fixture->count++] = stage;
    return stage == fixture->fail_stage ? fixture->failure :
           PGRAPH_VK_CACHED_FAMILY_MODULES_READY;
}

static void test_cached_stage_plan(void)
{
    StageFixture fixture = {
        .fail_stage = (PGRAPHVkHybridPrewarmStage)-1,
    };
    g_assert_cmpint(pgraph_vk_hybrid_prewarm_modules(
                        false, materialize_stage, &fixture),
                    ==, PGRAPH_VK_CACHED_FAMILY_MODULES_READY);
    g_assert_cmpuint(fixture.count, ==, 2);
    g_assert_cmpint(fixture.stages[0], ==,
                    PGRAPH_VK_HYBRID_PREWARM_VERTEX);
    g_assert_cmpint(fixture.stages[1], ==,
                    PGRAPH_VK_HYBRID_PREWARM_FRAGMENT);

    fixture = (StageFixture) {
        .fail_stage = (PGRAPHVkHybridPrewarmStage)-1,
    };
    g_assert_cmpint(pgraph_vk_hybrid_prewarm_modules(
                        true, materialize_stage, &fixture),
                    ==, PGRAPH_VK_CACHED_FAMILY_MODULES_READY);
    g_assert_cmpuint(fixture.count, ==, 3);
    g_assert_cmpint(fixture.stages[1], ==,
                    PGRAPH_VK_HYBRID_PREWARM_GEOMETRY);

    fixture = (StageFixture) {
        .fail_stage = PGRAPH_VK_HYBRID_PREWARM_GEOMETRY,
        .failure = PGRAPH_VK_CACHED_FAMILY_MODULES_MISSING,
    };
    g_assert_cmpint(pgraph_vk_hybrid_prewarm_modules(
                        true, materialize_stage, &fixture),
                    ==, PGRAPH_VK_CACHED_FAMILY_MODULES_MISSING);
    g_assert_cmpuint(fixture.count, ==, 2);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/nv2a/vk/hybrid-prewarm/demand-priority",
                    test_demand_has_priority);
    g_test_add_func("/nv2a/vk/hybrid-prewarm/one-per-service",
                    test_one_candidate_per_service);
    g_test_add_func("/nv2a/vk/hybrid-prewarm/launch-bound",
                    test_launch_bound);
    g_test_add_func("/nv2a/vk/hybrid-prewarm/accounting",
                    test_outcome_accounting);
    g_test_add_func("/nv2a/vk/hybrid-prewarm/cached-stage-plan",
                    test_cached_stage_plan);
    return g_test_run();
}

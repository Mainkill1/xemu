/*
 * NV2A Vulkan hybrid compiler queue tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/thread.h"

#include "hw/xbox/nv2a/pgraph/vk/hybrid-compiler.h"

typedef struct TestCompiler {
    QemuMutex lock;
    QemuCond started;
    QemuCond release;
    bool block;
    bool released;
    bool fail;
    unsigned int calls;
    unsigned int generate_calls;
    size_t generated_size;
    char source[32];
    char recipe[32];
    char config[32];
} TestCompiler;

typedef struct BlockingSubmit {
    PGRAPHVkHybridCompiler *compiler;
    PGRAPHVkHybridCompileRequest request;
    bool submitted;
    PGRAPHVkHybridCompileResult result;
} BlockingSubmit;

static void test_compiler_init(TestCompiler *compiler, bool block)
{
    *compiler = (TestCompiler) { .block = block };
    qemu_mutex_init(&compiler->lock);
    qemu_cond_init(&compiler->started);
    qemu_cond_init(&compiler->release);
}

static void test_compiler_destroy(TestCompiler *compiler)
{
    qemu_cond_destroy(&compiler->release);
    qemu_cond_destroy(&compiler->started);
    qemu_mutex_destroy(&compiler->lock);
}

static bool test_compile(void *opaque,
                         const PGRAPHVkHybridCompileRequest *request,
                         uint8_t **spirv, size_t *spirv_size)
{
    TestCompiler *compiler = opaque;

    qemu_mutex_lock(&compiler->lock);
    compiler->calls++;
    g_assert_cmpuint(request->glsl_size, <, sizeof(compiler->source));
    g_assert_cmpuint(request->config_size, <, sizeof(compiler->config));
    memcpy(compiler->source, request->glsl, request->glsl_size);
    compiler->source[request->glsl_size] = '\0';
    memcpy(compiler->config, request->config, request->config_size);
    compiler->config[request->config_size] = '\0';
    qemu_cond_broadcast(&compiler->started);
    while (compiler->block && !compiler->released) {
        qemu_cond_wait(&compiler->release, &compiler->lock);
    }
    bool fail = compiler->fail;
    qemu_mutex_unlock(&compiler->lock);

    if (fail) {
        return false;
    }
    *spirv_size = request->glsl_size;
    *spirv = g_memdup2(request->glsl, request->glsl_size);
    return *spirv != NULL;
}

static bool test_generate(void *opaque,
                          const PGRAPHVkHybridCompileRequest *request,
                          uint8_t **glsl, size_t *glsl_size)
{
    TestCompiler *compiler = opaque;

    qemu_mutex_lock(&compiler->lock);
    compiler->generate_calls++;
    g_assert_cmpuint(request->recipe_size, <, sizeof(compiler->recipe));
    g_assert_cmpuint(request->config_size, <, sizeof(compiler->config));
    memcpy(compiler->recipe, request->recipe, request->recipe_size);
    compiler->recipe[request->recipe_size] = '\0';
    memcpy(compiler->config, request->config, request->config_size);
    compiler->config[request->config_size] = '\0';
    size_t generated_size = compiler->generated_size;
    qemu_cond_broadcast(&compiler->started);
    bool fail = compiler->fail;
    qemu_mutex_unlock(&compiler->lock);

    if (fail) {
        return false;
    }
    if (generated_size) {
        *glsl = g_malloc0(generated_size);
        *glsl_size = generated_size;
        return *glsl != NULL;
    }
    *glsl = (uint8_t *)g_strdup_printf("generated-%s", compiler->recipe);
    *glsl_size = strlen((char *)*glsl) + 1;
    return *glsl != NULL;
}

static void test_wait_for_calls(TestCompiler *compiler, unsigned int calls)
{
    qemu_mutex_lock(&compiler->lock);
    while (compiler->calls < calls) {
        qemu_cond_wait(&compiler->started, &compiler->lock);
    }
    qemu_mutex_unlock(&compiler->lock);
}

static void test_release(TestCompiler *compiler)
{
    qemu_mutex_lock(&compiler->lock);
    compiler->released = true;
    qemu_cond_broadcast(&compiler->release);
    qemu_mutex_unlock(&compiler->lock);
}

static PGRAPHVkHybridCompilerConfig test_config(TestCompiler *test,
                                                size_t jobs, size_t bytes)
{
    return (PGRAPHVkHybridCompilerConfig) {
        .max_async_jobs = jobs,
        .max_async_bytes = bytes,
        .generate = test_generate,
        .compile = test_compile,
        .opaque = test,
    };
}

static PGRAPHVkHybridCompileRequest test_request_stage(
    uint64_t generation, uint64_t ticket, uint32_t stage,
    const char *glsl, const char *config, PGRAPHVkCompileUrgency urgency)
{
    return (PGRAPHVkHybridCompileRequest) {
        .generation = generation,
        .ticket = ticket,
        .kind = PGRAPH_VK_HYBRID_JOB_COMPILE_SOURCE,
        .stage = stage,
        .urgency = urgency,
        .glsl = glsl,
        .glsl_size = strlen(glsl),
        .config = config,
        .config_size = strlen(config),
    };
}

static PGRAPHVkHybridCompileRequest test_recipe_request(
    uint64_t generation, uint64_t ticket, const char *recipe,
    const char *config)
{
    return (PGRAPHVkHybridCompileRequest) {
        .generation = generation,
        .ticket = ticket,
        .kind = PGRAPH_VK_HYBRID_JOB_GENERATE_SOURCE,
        .stage = 16,
        .urgency = PGRAPH_VK_COMPILE_DEMAND,
        .recipe = recipe,
        .recipe_size = strlen(recipe),
        .config = config,
        .config_size = strlen(config),
    };
}

static PGRAPHVkHybridCompileRequest test_request(uint64_t generation,
                                                  uint64_t ticket,
                                                  const char *glsl,
                                                  const char *config)
{
    return test_request_stage(generation, ticket, 16, glsl, config,
                              PGRAPH_VK_COMPILE_DEMAND);
}

static bool init_compiler(PGRAPHVkHybridCompiler *compiler,
                          TestCompiler *test, size_t jobs, size_t bytes)
{
    PGRAPHVkHybridCompilerConfig config = test_config(test, jobs, bytes);

    return pgraph_vk_hybrid_compiler_init(compiler, &config);
}

static PGRAPHVkHybridCompilerSubmitResult submit_async(
    PGRAPHVkHybridCompiler *compiler, uint64_t generation, uint64_t ticket,
    const char *glsl, const char *config, PGRAPHVkHybridCompileIdentity *owner)
{
    PGRAPHVkHybridCompileRequest request =
        test_request(generation, ticket, glsl, config);

    return pgraph_vk_hybrid_compiler_submit_async(compiler, &request, owner);
}

static PGRAPHVkHybridCompilerSubmitResult submit_async_urgency(
    PGRAPHVkHybridCompiler *compiler, uint64_t generation, uint64_t ticket,
    const char *glsl, const char *config, PGRAPHVkCompileUrgency urgency,
    PGRAPHVkHybridCompileIdentity *owner)
{
    PGRAPHVkHybridCompileRequest request =
        test_request_stage(generation, ticket, 16, glsl, config, urgency);

    return pgraph_vk_hybrid_compiler_submit_async(compiler, &request, owner);
}

static bool take_result(PGRAPHVkHybridCompiler *compiler,
                        PGRAPHVkHybridCompileResult *result)
{
    for (unsigned int i = 0; i < 1000; i++) {
        if (pgraph_vk_hybrid_compiler_take_result(compiler, result)) {
            return true;
        }
        g_usleep(1000);
    }
    return false;
}

static void test_async_deduplicates_and_deep_owns_input(void)
{
    TestCompiler test;
    PGRAPHVkHybridCompiler compiler = { 0 };
    char glsl[] = "shader-a";
    char config[] = "config-a";
    PGRAPHVkHybridCompileRequest request =
        test_request(7, 19, glsl, config);
    PGRAPHVkHybridCompileResult result;
    PGRAPHVkHybridCompileIdentity owner;

    test_compiler_init(&test, true);
    g_assert_true(init_compiler(&compiler, &test, 2, 64));
    g_assert_cmpint(pgraph_vk_hybrid_compiler_submit_async(&compiler, &request,
                                                            &owner),
                    ==, PGRAPH_VK_HYBRID_COMPILER_ACCEPTED);
    g_assert_cmpuint(owner.ticket, ==, 19);
    g_assert_cmpint(submit_async(&compiler, 8, 20, "shader-a", "config-a",
                                 &owner),
                    ==, PGRAPH_VK_HYBRID_COMPILER_DUPLICATE);
    g_assert_cmpuint(owner.generation, ==, 7);
    g_assert_cmpuint(owner.ticket, ==, 19);

    memcpy(glsl, "mutated!", sizeof(glsl));
    memcpy(config, "mutated!", sizeof(config));
    test_wait_for_calls(&test, 1);
    test_release(&test);

    g_assert_true(take_result(&compiler, &result));
    g_assert_true(result.success);
    g_assert_cmpuint(result.generation, ==, 7);
    g_assert_cmpuint(result.ticket, ==, 19);
    g_assert_cmpstr(test.source, ==, "shader-a");
    g_assert_cmpstr(test.config, ==, "config-a");
    g_assert_cmpmem(result.spirv, result.spirv_size, "shader-a", 8);
    pgraph_vk_hybrid_compile_result_destroy(&result);
    pgraph_vk_hybrid_compiler_destroy(&compiler);
    test_compiler_destroy(&test);
}

static void test_async_generates_source_from_deep_owned_recipe(void)
{
    TestCompiler test;
    PGRAPHVkHybridCompiler compiler = { 0 };
    char recipe[] = "recipe-a";
    char config[] = "config-a";
    PGRAPHVkHybridCompileRequest request =
        test_recipe_request(7, 19, recipe, config);
    PGRAPHVkHybridCompileResult result;

    test_compiler_init(&test, false);
    g_assert_true(init_compiler(&compiler, &test, 2, 64));
    g_assert_cmpint(pgraph_vk_hybrid_compiler_submit_async(
                        &compiler, &request, NULL),
                    ==, PGRAPH_VK_HYBRID_COMPILER_ACCEPTED);
    memcpy(recipe, "mutated!", sizeof(recipe));
    memcpy(config, "mutated!", sizeof(config));

    g_assert_true(take_result(&compiler, &result));
    g_assert_true(result.success);
    g_assert_cmpint(result.kind, ==,
                    PGRAPH_VK_HYBRID_JOB_GENERATE_SOURCE);
    g_assert_cmpstr((char *)result.glsl, ==, "generated-recipe-a");
    g_assert_cmpuint(result.glsl_size, ==,
                     strlen("generated-recipe-a") + 1);
    g_assert_null(result.spirv);
    g_assert_cmpuint(result.spirv_size, ==, 0);
    g_assert_cmpuint(result.submitted_us, >, 0);
    g_assert_cmpuint(result.started_us, >=, result.submitted_us);
    g_assert_cmpuint(result.finished_us, >=, result.started_us);
    g_assert_cmpuint(test.generate_calls, ==, 1);
    g_assert_cmpuint(test.calls, ==, 0);
    g_assert_cmpstr(test.recipe, ==, "recipe-a");
    g_assert_cmpstr(test.config, ==, "config-a");
    pgraph_vk_hybrid_compile_result_destroy(&result);
    pgraph_vk_hybrid_compiler_destroy(&compiler);
    test_compiler_destroy(&test);
}

static void test_async_keeps_recipe_and_source_jobs_distinct(void)
{
    TestCompiler test;
    PGRAPHVkHybridCompiler compiler = { 0 };
    PGRAPHVkHybridCompileRequest generate =
        test_recipe_request(1, 1, "same-bytes", "config");
    PGRAPHVkHybridCompileRequest compile =
        test_request(1, 2, "same-bytes", "config");
    PGRAPHVkHybridCompileResult result;

    test_compiler_init(&test, false);
    g_assert_true(init_compiler(&compiler, &test, 2, 64));
    g_assert_cmpint(pgraph_vk_hybrid_compiler_submit_async(
                        &compiler, &generate, NULL),
                    ==, PGRAPH_VK_HYBRID_COMPILER_ACCEPTED);
    g_assert_cmpint(pgraph_vk_hybrid_compiler_submit_async(
                        &compiler, &compile, NULL),
                    ==, PGRAPH_VK_HYBRID_COMPILER_ACCEPTED);

    g_assert_true(take_result(&compiler, &result));
    g_assert_cmpint(result.kind, ==,
                    PGRAPH_VK_HYBRID_JOB_GENERATE_SOURCE);
    pgraph_vk_hybrid_compile_result_destroy(&result);
    g_assert_true(take_result(&compiler, &result));
    g_assert_cmpint(result.kind, ==,
                    PGRAPH_VK_HYBRID_JOB_COMPILE_SOURCE);
    pgraph_vk_hybrid_compile_result_destroy(&result);
    g_assert_cmpuint(test.generate_calls, ==, 1);
    g_assert_cmpuint(test.calls, ==, 1);

    pgraph_vk_hybrid_compiler_destroy(&compiler);
    test_compiler_destroy(&test);
}

static void test_generated_source_is_bounded(void)
{
    TestCompiler test;
    PGRAPHVkHybridCompiler compiler = { 0 };
    PGRAPHVkHybridCompileRequest request =
        test_recipe_request(1, 1, "recipe", "config");
    PGRAPHVkHybridCompileResult result;

    test_compiler_init(&test, false);
    /* 6 recipe + 6 config + 53 output exceeds the retained-byte cap. */
    test.generated_size = 53;
    g_assert_true(init_compiler(&compiler, &test, 1, 64));
    g_assert_cmpint(pgraph_vk_hybrid_compiler_submit_async(
                        &compiler, &request, NULL),
                    ==, PGRAPH_VK_HYBRID_COMPILER_ACCEPTED);
    g_assert_true(take_result(&compiler, &result));
    g_assert_false(result.success);
    g_assert_null(result.glsl);
    g_assert_cmpuint(result.glsl_size, ==, 0);
    g_assert_cmpuint(test.generate_calls, ==, 1);
    g_assert_cmpuint(test.calls, ==, 0);
    pgraph_vk_hybrid_compile_result_destroy(&result);
    pgraph_vk_hybrid_compiler_destroy(&compiler);
    test_compiler_destroy(&test);
}

static void test_recipe_requires_generator_and_async_lane(void)
{
    TestCompiler test;
    PGRAPHVkHybridCompiler compiler = { 0 };
    PGRAPHVkHybridCompileRequest request =
        test_recipe_request(1, 1, "recipe", "config");
    PGRAPHVkHybridCompileResult result;
    PGRAPHVkHybridCompilerConfig config;

    test_compiler_init(&test, false);
    config = test_config(&test, 1, 64);
    config.generate = NULL;
    g_assert_true(pgraph_vk_hybrid_compiler_init(&compiler, &config));
    g_assert_cmpint(pgraph_vk_hybrid_compiler_submit_async(
                        &compiler, &request, NULL),
                    ==, PGRAPH_VK_HYBRID_COMPILER_INVALID);
    g_assert_false(pgraph_vk_hybrid_compiler_submit_blocking(
        &compiler, &request, &result));
    g_assert_cmpuint(test.generate_calls, ==, 0);
    g_assert_cmpuint(test.calls, ==, 0);
    pgraph_vk_hybrid_compiler_destroy(&compiler);
    test_compiler_destroy(&test);
}

static void test_async_limits_release_when_result_is_taken(void)
{
    TestCompiler test;
    PGRAPHVkHybridCompiler compiler = { 0 };
    PGRAPHVkHybridCompileResult result;

    test_compiler_init(&test, false);
    g_assert_true(init_compiler(&compiler, &test, 1, 16));
    g_assert_true(pgraph_vk_hybrid_compiler_can_submit_async(&compiler, 8, 8));
    g_assert_cmpint(submit_async(&compiler, 1, 1, "abcdefgh", "12345678",
                                 NULL),
                    ==, PGRAPH_VK_HYBRID_COMPILER_ACCEPTED);
    g_assert_false(pgraph_vk_hybrid_compiler_can_submit_async(&compiler, 1, 1));
    g_assert_cmpint(submit_async(&compiler, 1, 2, "x", "y", NULL),
                    ==, PGRAPH_VK_HYBRID_COMPILER_QUEUE_FULL);
    g_assert_true(take_result(&compiler, &result));
    pgraph_vk_hybrid_compile_result_destroy(&result);
    g_assert_true(pgraph_vk_hybrid_compiler_can_submit_async(&compiler, 8, 8));
    g_assert_cmpint(submit_async(&compiler, 1, 3, "123456789", "12345678",
                                 NULL),
                    ==, PGRAPH_VK_HYBRID_COMPILER_BYTE_LIMIT);
    pgraph_vk_hybrid_compiler_destroy(&compiler);
    test_compiler_destroy(&test);
}

static void test_dedup_keeps_different_immutable_configs_distinct(void)
{
    TestCompiler test;
    PGRAPHVkHybridCompiler compiler = { 0 };
    PGRAPHVkHybridCompileResult result;

    test_compiler_init(&test, false);
    g_assert_true(init_compiler(&compiler, &test, 2, 64));
    g_assert_cmpint(submit_async(&compiler, 1, 1, "same-source", "config-a",
                                 NULL),
                    ==, PGRAPH_VK_HYBRID_COMPILER_ACCEPTED);
    g_assert_cmpint(submit_async(&compiler, 2, 2, "same-source", "config-b",
                                 NULL),
                    ==, PGRAPH_VK_HYBRID_COMPILER_ACCEPTED);
    g_assert_true(take_result(&compiler, &result));
    g_assert_cmpuint(result.ticket, ==, 1);
    g_assert_cmpuint(result.submitted_us, >, 0);
    g_assert_cmpuint(result.started_us, >=, result.submitted_us);
    g_assert_cmpuint(result.finished_us, >=, result.started_us);
    pgraph_vk_hybrid_compile_result_destroy(&result);
    g_assert_true(take_result(&compiler, &result));
    g_assert_cmpuint(result.ticket, ==, 2);
    pgraph_vk_hybrid_compile_result_destroy(&result);
    pgraph_vk_hybrid_compiler_destroy(&compiler);
    test_compiler_destroy(&test);
}

static void test_async_preserves_stage_identity(void)
{
    static const struct {
        uint64_t generation;
        uint64_t ticket;
        uint32_t stage;
        const char *source;
    } requests[] = {
        { 11, 21, 1, "same-source" },
        { 12, 22, 2, "same-source" },
        { 13, 23, 3, "fragment-source" },
    };
    TestCompiler test;
    PGRAPHVkHybridCompiler compiler = { 0 };
    PGRAPHVkHybridCompileResult result;

    test_compiler_init(&test, false);
    g_assert_true(init_compiler(&compiler, &test, ARRAY_SIZE(requests), 128));
    for (size_t i = 0; i < ARRAY_SIZE(requests); i++) {
        PGRAPHVkHybridCompileRequest request = test_request_stage(
            requests[i].generation, requests[i].ticket, requests[i].stage,
            requests[i].source, "config", PGRAPH_VK_COMPILE_DEMAND);
        g_assert_cmpint(pgraph_vk_hybrid_compiler_submit_async(
                            &compiler, &request, NULL),
                        ==, PGRAPH_VK_HYBRID_COMPILER_ACCEPTED);
    }

    for (size_t i = 0; i < ARRAY_SIZE(requests); i++) {
        g_assert_true(take_result(&compiler, &result));
        g_assert_cmpuint(result.generation, ==, requests[i].generation);
        g_assert_cmpuint(result.ticket, ==, requests[i].ticket);
        g_assert_cmpuint(result.stage, ==, requests[i].stage);
        pgraph_vk_hybrid_compile_result_destroy(&result);
    }

    pgraph_vk_hybrid_compiler_destroy(&compiler);
    test_compiler_destroy(&test);
}

static void test_async_urgency_orders_queued_work(void)
{
    TestCompiler test;
    PGRAPHVkHybridCompiler compiler = { 0 };
    PGRAPHVkHybridCompileResult result;
    static const struct {
        uint64_t ticket;
        PGRAPHVkCompileUrgency urgency;
    } expected[] = {
        { 1, PGRAPH_VK_COMPILE_SPECULATIVE },
        { 4, PGRAPH_VK_COMPILE_DEMAND },
        { 5, PGRAPH_VK_COMPILE_DEMAND },
        { 3, PGRAPH_VK_COMPILE_PREWARM },
        { 2, PGRAPH_VK_COMPILE_SPECULATIVE },
    };

    test_compiler_init(&test, true);
    g_assert_true(init_compiler(&compiler, &test, 5, 160));
    g_assert_cmpint(submit_async_urgency(
                        &compiler, 1, 1, "active", "config",
                        PGRAPH_VK_COMPILE_SPECULATIVE, NULL),
                    ==, PGRAPH_VK_HYBRID_COMPILER_ACCEPTED);
    test_wait_for_calls(&test, 1);
    g_assert_cmpint(submit_async_urgency(
                        &compiler, 1, 2, "queued-spec", "config",
                        PGRAPH_VK_COMPILE_SPECULATIVE, NULL),
                    ==, PGRAPH_VK_HYBRID_COMPILER_ACCEPTED);
    g_assert_cmpint(submit_async_urgency(
                        &compiler, 1, 3, "queued-prewarm", "config",
                        PGRAPH_VK_COMPILE_PREWARM, NULL),
                    ==, PGRAPH_VK_HYBRID_COMPILER_ACCEPTED);
    g_assert_cmpint(submit_async_urgency(
                        &compiler, 1, 4, "queued-demand", "config",
                        PGRAPH_VK_COMPILE_DEMAND, NULL),
                    ==, PGRAPH_VK_HYBRID_COMPILER_ACCEPTED);
    g_assert_cmpint(submit_async_urgency(
                        &compiler, 1, 5, "demand-second", "config",
                        PGRAPH_VK_COMPILE_DEMAND, NULL),
                    ==, PGRAPH_VK_HYBRID_COMPILER_ACCEPTED);
    test_release(&test);

    for (size_t i = 0; i < ARRAY_SIZE(expected); i++) {
        g_assert_true(take_result(&compiler, &result));
        g_assert_cmpuint(result.ticket, ==, expected[i].ticket);
        g_assert_cmpint(result.urgency, ==, expected[i].urgency);
        pgraph_vk_hybrid_compile_result_destroy(&result);
    }
    g_assert_cmpuint(test.calls, ==, ARRAY_SIZE(expected));

    pgraph_vk_hybrid_compiler_destroy(&compiler);
    test_compiler_destroy(&test);
}

static void test_async_active_duplicate_is_not_reordered(void)
{
    TestCompiler test;
    PGRAPHVkHybridCompiler compiler = { 0 };
    PGRAPHVkHybridCompileIdentity owner;
    PGRAPHVkHybridCompileResult result;

    test_compiler_init(&test, true);
    g_assert_true(init_compiler(&compiler, &test, 1, 64));
    g_assert_cmpint(submit_async_urgency(
                        &compiler, 2, 2, "active", "config",
                        PGRAPH_VK_COMPILE_SPECULATIVE, NULL),
                    ==, PGRAPH_VK_HYBRID_COMPILER_ACCEPTED);
    test_wait_for_calls(&test, 1);
    g_assert_cmpint(submit_async_urgency(
                        &compiler, 9, 99, "active", "config",
                        PGRAPH_VK_COMPILE_DEMAND, &owner),
                    ==, PGRAPH_VK_HYBRID_COMPILER_DUPLICATE);
    g_assert_cmpuint(owner.generation, ==, 2);
    g_assert_cmpuint(owner.ticket, ==, 2);

    test_release(&test);
    g_assert_true(take_result(&compiler, &result));
    g_assert_cmpuint(result.ticket, ==, 2);
    g_assert_cmpint(result.urgency, ==, PGRAPH_VK_COMPILE_SPECULATIVE);
    pgraph_vk_hybrid_compile_result_destroy(&result);
    g_assert_cmpuint(test.calls, ==, 1);

    pgraph_vk_hybrid_compiler_destroy(&compiler);
    test_compiler_destroy(&test);
}

static void test_async_exact_duplicate_is_promoted_in_place(void)
{
    TestCompiler test;
    PGRAPHVkHybridCompiler compiler = { 0 };
    PGRAPHVkHybridCompileIdentity owner;
    PGRAPHVkHybridCompileResult result;

    test_compiler_init(&test, true);
    g_assert_true(init_compiler(&compiler, &test, 3, 128));
    g_assert_cmpint(submit_async_urgency(
                        &compiler, 1, 1, "active", "config",
                        PGRAPH_VK_COMPILE_SPECULATIVE, NULL),
                    ==, PGRAPH_VK_HYBRID_COMPILER_ACCEPTED);
    test_wait_for_calls(&test, 1);
    g_assert_cmpint(submit_async_urgency(
                        &compiler, 2, 2, "promoted", "config",
                        PGRAPH_VK_COMPILE_SPECULATIVE, NULL),
                    ==, PGRAPH_VK_HYBRID_COMPILER_ACCEPTED);
    g_assert_cmpint(submit_async_urgency(
                        &compiler, 9, 99, "promoted", "config",
                        PGRAPH_VK_COMPILE_DEMAND, &owner),
                    ==, PGRAPH_VK_HYBRID_COMPILER_DUPLICATE_PROMOTED);
    g_assert_cmpuint(owner.generation, ==, 2);
    g_assert_cmpuint(owner.ticket, ==, 2);
    g_assert_cmpint(submit_async_urgency(
                        &compiler, 10, 100, "promoted", "config",
                        PGRAPH_VK_COMPILE_PREWARM, &owner),
                    ==, PGRAPH_VK_HYBRID_COMPILER_DUPLICATE);
    g_assert_cmpuint(owner.generation, ==, 2);
    g_assert_cmpuint(owner.ticket, ==, 2);

    test_release(&test);
    g_assert_true(take_result(&compiler, &result));
    g_assert_cmpuint(result.ticket, ==, 1);
    pgraph_vk_hybrid_compile_result_destroy(&result);
    g_assert_true(take_result(&compiler, &result));
    g_assert_cmpuint(result.ticket, ==, 2);
    g_assert_cmpint(result.urgency, ==, PGRAPH_VK_COMPILE_DEMAND);
    pgraph_vk_hybrid_compile_result_destroy(&result);
    g_assert_cmpuint(test.calls, ==, 2);

    pgraph_vk_hybrid_compiler_destroy(&compiler);
    test_compiler_destroy(&test);
}

static void *test_submit_blocking(void *opaque)
{
    BlockingSubmit *submit = opaque;

    submit->submitted = pgraph_vk_hybrid_compiler_submit_blocking(
        submit->compiler, &submit->request, &submit->result);
    return NULL;
}

static void test_async_matching_blocking_work_keeps_its_own_result(void)
{
    TestCompiler test;
    PGRAPHVkHybridCompiler compiler = { 0 };
    BlockingSubmit blocking = { 0 };
    QemuThread thread;
    PGRAPHVkHybridCompileResult result;

    test_compiler_init(&test, true);
    g_assert_true(init_compiler(&compiler, &test, 1, 64));
    blocking.compiler = &compiler;
    blocking.request = test_request(3, 4, "same-source", "config");
    qemu_thread_create(&thread, "hybrid-compiler-test", test_submit_blocking,
                       &blocking, QEMU_THREAD_JOINABLE);
    test_wait_for_calls(&test, 1);

    g_assert_cmpint(submit_async(&compiler, 5, 6, "same-source", "config",
                                 NULL),
                    ==, PGRAPH_VK_HYBRID_COMPILER_ACCEPTED);
    test_release(&test);
    g_assert_null(qemu_thread_join(&thread));
    g_assert_true(blocking.submitted);
    g_assert_true(blocking.result.success);
    pgraph_vk_hybrid_compile_result_destroy(&blocking.result);
    test_wait_for_calls(&test, 2);

    g_assert_true(take_result(&compiler, &result));
    g_assert_cmpuint(result.generation, ==, 5);
    g_assert_cmpuint(result.ticket, ==, 6);
    pgraph_vk_hybrid_compile_result_destroy(&result);
    pgraph_vk_hybrid_compiler_destroy(&compiler);
    test_compiler_destroy(&test);
}

static void test_blocking_slot_is_reserved_from_async_limits(void)
{
    TestCompiler test;
    PGRAPHVkHybridCompiler compiler = { 0 };
    BlockingSubmit blocking = { 0 };
    QemuThread thread;
    PGRAPHVkHybridCompileResult result;

    test_compiler_init(&test, true);
    g_assert_true(init_compiler(&compiler, &test, 1, 16));
    g_assert_cmpint(submit_async(&compiler, 3, 4, "abcdefgh", "12345678",
                                 NULL),
                    ==, PGRAPH_VK_HYBRID_COMPILER_ACCEPTED);
    test_wait_for_calls(&test, 1);

    blocking.compiler = &compiler;
    blocking.request = test_request(5, 6, "abcdefgh", "12345678");
    qemu_thread_create(&thread, "hybrid-compiler-test", test_submit_blocking,
                       &blocking, QEMU_THREAD_JOINABLE);
    test_release(&test);
    g_assert_null(qemu_thread_join(&thread));
    g_assert_true(blocking.submitted);
    g_assert_true(blocking.result.success);
    test_wait_for_calls(&test, 2);
    g_assert_cmpuint(blocking.result.generation, ==, 5);
    g_assert_cmpuint(blocking.result.ticket, ==, 6);
    pgraph_vk_hybrid_compile_result_destroy(&blocking.result);

    g_assert_true(pgraph_vk_hybrid_compiler_has_result(&compiler));
    g_assert_true(take_result(&compiler, &result));
    g_assert_cmpuint(result.ticket, ==, 4);
    pgraph_vk_hybrid_compile_result_destroy(&result);
    g_assert_false(pgraph_vk_hybrid_compiler_has_result(&compiler));
    pgraph_vk_hybrid_compiler_destroy(&compiler);
    test_compiler_destroy(&test);
}

static void test_required_compile_starts_while_async_is_active(void)
{
    TestCompiler test;
    PGRAPHVkHybridCompiler compiler = { 0 };
    BlockingSubmit blocking = { 0 };
    QemuThread thread;
    PGRAPHVkHybridCompileResult result;
    bool started;

    test_compiler_init(&test, true);
    g_assert_true(init_compiler(&compiler, &test, 1, 64));
    g_assert_cmpint(submit_async(&compiler, 1, 1, "background", "config",
                                 NULL),
                    ==, PGRAPH_VK_HYBRID_COMPILER_ACCEPTED);
    test_wait_for_calls(&test, 1);

    blocking.compiler = &compiler;
    blocking.request = test_request(1, 2, "required", "config");
    qemu_thread_create(&thread, "hybrid-compiler-required-test",
                       test_submit_blocking, &blocking, QEMU_THREAD_JOINABLE);

    qemu_mutex_lock(&test.lock);
    while (test.calls < 2 &&
           qemu_cond_timedwait(&test.started, &test.lock, 1000)) {
    }
    started = test.calls >= 2;
    qemu_mutex_unlock(&test.lock);

    test_release(&test);
    g_assert_null(qemu_thread_join(&thread));
    g_assert_true(blocking.submitted);
    g_assert_true(blocking.result.success);
    pgraph_vk_hybrid_compile_result_destroy(&blocking.result);
    g_assert_true(take_result(&compiler, &result));
    pgraph_vk_hybrid_compile_result_destroy(&result);
    pgraph_vk_hybrid_compiler_destroy(&compiler);
    test_compiler_destroy(&test);
    g_assert_true(started);
}

static void test_compile_failure_is_returned_with_owned_empty_artifact(void)
{
    TestCompiler test;
    PGRAPHVkHybridCompiler compiler = { 0 };
    PGRAPHVkHybridCompileResult result;

    test_compiler_init(&test, false);
    test.fail = true;
    g_assert_true(init_compiler(&compiler, &test, 1, 64));
    g_assert_cmpint(submit_async(&compiler, 9, 11, "bad", "config", NULL),
                    ==, PGRAPH_VK_HYBRID_COMPILER_ACCEPTED);
    g_assert_true(take_result(&compiler, &result));
    g_assert_false(result.success);
    g_assert_null(result.spirv);
    g_assert_cmpuint(result.spirv_size, ==, 0);
    pgraph_vk_hybrid_compile_result_destroy(&result);
    pgraph_vk_hybrid_compiler_destroy(&compiler);
    test_compiler_destroy(&test);
}

static void test_invalid_urgency_is_rejected(void)
{
    TestCompiler test;
    PGRAPHVkHybridCompiler compiler = { 0 };
    PGRAPHVkHybridCompileRequest request =
        test_request(1, 1, "shader", "config");

    test_compiler_init(&test, false);
    g_assert_true(init_compiler(&compiler, &test, 1, 64));
    request.urgency = PGRAPH_VK_COMPILE_DEMAND + 1;
    g_assert_cmpint(pgraph_vk_hybrid_compiler_submit_async(
                        &compiler, &request, NULL),
                    ==, PGRAPH_VK_HYBRID_COMPILER_INVALID);
    g_assert_cmpuint(test.calls, ==, 0);
    pgraph_vk_hybrid_compiler_destroy(&compiler);
    test_compiler_destroy(&test);
}

static void test_stop_join_releases_idle_busy_and_full_queues(void)
{
    TestCompiler test;
    PGRAPHVkHybridCompiler idle = { 0 };
    PGRAPHVkHybridCompiler compiler = { 0 };

    test_compiler_init(&test, true);
    g_assert_true(init_compiler(&idle, &test, 1, 64));
    pgraph_vk_hybrid_compiler_stop(&idle);
    pgraph_vk_hybrid_compiler_join(&idle);
    pgraph_vk_hybrid_compiler_destroy(&idle);

    g_assert_true(init_compiler(&compiler, &test, 1, 64));
    g_assert_cmpint(submit_async(&compiler, 1, 1, "busy", "config", NULL),
                    ==, PGRAPH_VK_HYBRID_COMPILER_ACCEPTED);
    test_wait_for_calls(&test, 1);
    g_assert_cmpint(submit_async(&compiler, 1, 2, "full", "config", NULL),
                    ==, PGRAPH_VK_HYBRID_COMPILER_QUEUE_FULL);
    pgraph_vk_hybrid_compiler_stop(&compiler);
    g_assert_cmpint(submit_async(&compiler, 1, 3, "stopped", "config", NULL),
                    ==, PGRAPH_VK_HYBRID_COMPILER_STOPPED);
    test_release(&test);
    pgraph_vk_hybrid_compiler_join(&compiler);
    pgraph_vk_hybrid_compiler_destroy(&compiler);
    test_compiler_destroy(&test);
}

static void test_stop_waits_for_active_required_compile(void)
{
    TestCompiler test;
    PGRAPHVkHybridCompiler compiler = { 0 };
    BlockingSubmit blocking = { 0 };
    QemuThread thread;

    test_compiler_init(&test, true);
    g_assert_true(init_compiler(&compiler, &test, 1, 64));
    blocking.compiler = &compiler;
    blocking.request = test_request(1, 2, "required", "config");
    qemu_thread_create(&thread, "hybrid-compiler-stop-test",
                       test_submit_blocking, &blocking, QEMU_THREAD_JOINABLE);
    test_wait_for_calls(&test, 1);

    pgraph_vk_hybrid_compiler_stop(&compiler);
    test_release(&test);
    g_assert_null(qemu_thread_join(&thread));
    g_assert_false(blocking.submitted);
    g_assert_null(blocking.result.spirv);
    pgraph_vk_hybrid_compiler_destroy(&compiler);
    test_compiler_destroy(&test);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/vk/hybrid-compiler/dedup-deep-copy",
                    test_async_deduplicates_and_deep_owns_input);
    g_test_add_func("/xbox/vk/hybrid-compiler/recipe-deep-copy",
                    test_async_generates_source_from_deep_owned_recipe);
    g_test_add_func("/xbox/vk/hybrid-compiler/job-kind-identity",
                    test_async_keeps_recipe_and_source_jobs_distinct);
    g_test_add_func("/xbox/vk/hybrid-compiler/generated-source-limit",
                    test_generated_source_is_bounded);
    g_test_add_func("/xbox/vk/hybrid-compiler/recipe-lane-contract",
                    test_recipe_requires_generator_and_async_lane);
    g_test_add_func("/xbox/vk/hybrid-compiler/async-limits",
                    test_async_limits_release_when_result_is_taken);
    g_test_add_func("/xbox/vk/hybrid-compiler/exact-config",
                    test_dedup_keeps_different_immutable_configs_distinct);
    g_test_add_func("/xbox/vk/hybrid-compiler/stage-identity",
                    test_async_preserves_stage_identity);
    g_test_add_func("/xbox/vk/hybrid-compiler/urgency-order",
                    test_async_urgency_orders_queued_work);
    g_test_add_func("/xbox/vk/hybrid-compiler/duplicate-promotion",
                    test_async_exact_duplicate_is_promoted_in_place);
    g_test_add_func("/xbox/vk/hybrid-compiler/active-not-promoted",
                    test_async_active_duplicate_is_not_reordered);
    g_test_add_func("/xbox/vk/hybrid-compiler/async-after-blocking",
                    test_async_matching_blocking_work_keeps_its_own_result);
    g_test_add_func("/xbox/vk/hybrid-compiler/reserved-blocking",
                    test_blocking_slot_is_reserved_from_async_limits);
    g_test_add_func("/xbox/vk/hybrid-compiler/required-starts-before-async-finishes",
                    test_required_compile_starts_while_async_is_active);
    g_test_add_func("/xbox/vk/hybrid-compiler/failure-result",
                    test_compile_failure_is_returned_with_owned_empty_artifact);
    g_test_add_func("/xbox/vk/hybrid-compiler/invalid-urgency",
                    test_invalid_urgency_is_rejected);
    g_test_add_func("/xbox/vk/hybrid-compiler/stop-join",
                    test_stop_join_releases_idle_busy_and_full_queues);
    g_test_add_func("/xbox/vk/hybrid-compiler/stop-active-required",
                    test_stop_waits_for_active_required_compile);
    return g_test_run();
}

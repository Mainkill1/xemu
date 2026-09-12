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
    char source[32];
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
        .compile = test_compile,
        .opaque = test,
    };
}

static PGRAPHVkHybridCompileRequest test_request(uint64_t generation,
                                                  uint64_t ticket,
                                                  const char *glsl,
                                                  const char *config)
{
    return (PGRAPHVkHybridCompileRequest) {
        .generation = generation,
        .ticket = ticket,
        .stage = 16,
        .glsl = glsl,
        .glsl_size = strlen(glsl),
        .config = config,
        .config_size = strlen(config),
    };
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
    pgraph_vk_hybrid_compile_result_destroy(&result);
    g_assert_true(take_result(&compiler, &result));
    g_assert_cmpuint(result.ticket, ==, 2);
    pgraph_vk_hybrid_compile_result_destroy(&result);
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

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/vk/hybrid-compiler/dedup-deep-copy",
                    test_async_deduplicates_and_deep_owns_input);
    g_test_add_func("/xbox/vk/hybrid-compiler/async-limits",
                    test_async_limits_release_when_result_is_taken);
    g_test_add_func("/xbox/vk/hybrid-compiler/exact-config",
                    test_dedup_keeps_different_immutable_configs_distinct);
    g_test_add_func("/xbox/vk/hybrid-compiler/async-after-blocking",
                    test_async_matching_blocking_work_keeps_its_own_result);
    g_test_add_func("/xbox/vk/hybrid-compiler/reserved-blocking",
                    test_blocking_slot_is_reserved_from_async_limits);
    g_test_add_func("/xbox/vk/hybrid-compiler/failure-result",
                    test_compile_failure_is_returned_with_owned_empty_artifact);
    g_test_add_func("/xbox/vk/hybrid-compiler/stop-join",
                    test_stop_join_releases_idle_busy_and_full_queues);
    return g_test_run();
}

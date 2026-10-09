/*
 * NV2A explicit idle-read owner/waiter integration tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/atomic.h"
#include "hw/xbox/nv2a/pgraph/idle-completion.h"

typedef struct TestIdle {
    QemuMutex pfifo;
    QemuMutex pgraph;
    QemuMutex bql;
    QemuEvent kicked;
    QemuEvent copying;
    QemuEvent allow_copy;
    PGRAPHIdleCompletion completion;
    PGRAPHIdleCompletionCoordinator coordinator;
    bool eligible;
    bool serviceable;
    bool needed;
    bool copy_succeeds;
    bool block_copy;
    bool returned;
    uint64_t frontier;
    unsigned int copies;
    unsigned int kicks;
    uint8_t ram;
    PGRAPHIdleCompletionResult result;
} TestIdle;

static bool eligible(void *opaque)
{
    return ((TestIdle *)opaque)->eligible;
}

static bool serviceable(void *opaque)
{
    return ((TestIdle *)opaque)->serviceable;
}

static bool needed(void *opaque)
{
    return ((TestIdle *)opaque)->needed;
}

static uint64_t frontier(void *opaque)
{
    return ((TestIdle *)opaque)->frontier;
}

static void kick(void *opaque)
{
    TestIdle *test = opaque;

    test->kicks++;
    qemu_event_set(&test->kicked);
}

static void release_bql(void *opaque)
{
    qemu_mutex_unlock(&((TestIdle *)opaque)->bql);
}

static void acquire_bql(void *opaque)
{
    qemu_mutex_lock(&((TestIdle *)opaque)->bql);
}

static bool materialize(void *opaque)
{
    TestIdle *test = opaque;

    g_assert_cmpint(qemu_mutex_trylock(&test->pfifo), ==, 0);
    qemu_mutex_unlock(&test->pfifo);
    g_assert_cmpint(qemu_mutex_trylock(&test->bql), ==, 0);
    qemu_mutex_unlock(&test->bql);
    g_assert_cmpint(qemu_mutex_trylock(&test->pgraph), !=, 0);
    test->copies++;
    qemu_event_set(&test->copying);
    if (test->block_copy) {
        qemu_event_wait(&test->allow_copy);
    }
    if (test->copy_succeeds) {
        test->ram = 0xa7;
    }
    return test->copy_succeeds;
}

static const PGRAPHIdleCompletionOps ops = {
    .eligible = eligible,
    .serviceable = serviceable,
    .needed = needed,
    .frontier = frontier,
    .kick = kick,
    .release_bql = release_bql,
    .acquire_bql = acquire_bql,
    .materialize = materialize,
};

static void init(TestIdle *test)
{
    memset(test, 0, sizeof(*test));
    qemu_mutex_init(&test->pfifo);
    qemu_mutex_init(&test->pgraph);
    qemu_mutex_init(&test->bql);
    qemu_event_init(&test->kicked, false);
    qemu_event_init(&test->copying, false);
    qemu_event_init(&test->allow_copy, false);
    pgraph_idle_completion_init(&test->completion);
    test->coordinator = (PGRAPHIdleCompletionCoordinator){
        .pfifo_lock = &test->pfifo,
        .pgraph_lock = &test->pgraph,
        .completion = &test->completion,
    };
    test->eligible = true;
    test->serviceable = true;
    test->needed = true;
    test->copy_succeeds = true;
    test->frontier = 17;
}

static void cleanup(TestIdle *test)
{
    pgraph_idle_completion_destroy(&test->completion);
    qemu_event_destroy(&test->kicked);
    qemu_event_destroy(&test->copying);
    qemu_event_destroy(&test->allow_copy);
    qemu_mutex_destroy(&test->pfifo);
    qemu_mutex_destroy(&test->pgraph);
    qemu_mutex_destroy(&test->bql);
}

static void *waiter(void *opaque)
{
    TestIdle *test = opaque;

    qemu_mutex_lock(&test->bql);
    test->result = pgraph_idle_completion_wait(&test->coordinator, &ops, test);
    qatomic_set(&test->returned, true);
    qemu_mutex_unlock(&test->bql);
    return NULL;
}

static void *owner(void *opaque)
{
    TestIdle *test = opaque;

    qemu_mutex_lock(&test->pfifo);
    pgraph_idle_completion_process(&test->coordinator, &ops, test);
    qemu_mutex_unlock(&test->pfifo);
    return NULL;
}

static void start_waiter(TestIdle *test, QemuThread *thread)
{
    qatomic_set(&test->returned, false);
    qemu_event_reset(&test->kicked);
    qemu_thread_create(thread, "idle-waiter", waiter, test,
                       QEMU_THREAD_JOINABLE);
    qemu_event_wait(&test->kicked);
}

static void test_publication_and_locks(void)
{
    TestIdle test;
    QemuThread reader, renderer;

    init(&test);
    test.block_copy = true;
    start_waiter(&test, &reader);
    qemu_thread_create(&renderer, "idle-owner", owner, &test,
                       QEMU_THREAD_JOINABLE);
    qemu_event_wait(&test.copying);
    g_assert_false(qatomic_read(&test.returned));
    g_assert_true(qatomic_read(&test.completion.pending));
    g_assert_cmpuint(test.ram, ==, 0);
    qemu_event_set(&test.allow_copy);
    qemu_thread_join(&renderer);
    qemu_thread_join(&reader);
    g_assert_cmpint(test.result, ==, PGRAPH_IDLE_COMPLETE);
    g_assert_cmpuint(test.ram, ==, 0xa7);
    g_assert_cmpuint(test.completion.acknowledged, ==, 1);
    g_assert_cmpuint(test.copies, ==, 1);
    cleanup(&test);
}

static void test_skip(void)
{
    TestIdle test;

    init(&test);
    test.eligible = false;
    waiter(&test);
    g_assert_cmpint(test.result, ==, PGRAPH_IDLE_SKIPPED);
    test.eligible = true;
    test.needed = false;
    waiter(&test);
    g_assert_cmpint(test.result, ==, PGRAPH_IDLE_SKIPPED);
    g_assert_cmpuint(test.kicks, ==, 0);
    g_assert_cmpuint(test.copies, ==, 0);
    g_assert_cmpuint(test.completion.requested, ==, 0);
    cleanup(&test);
}

static void test_failure(void)
{
    TestIdle test;
    QemuThread reader;

    init(&test);
    test.copy_succeeds = false;
    start_waiter(&test, &reader);
    owner(&test);
    qemu_thread_join(&reader);
    g_assert_cmpint(test.result, ==, PGRAPH_IDLE_FAILED);
    g_assert_cmpuint(test.ram, ==, 0);
    g_assert_false(qatomic_read(&test.completion.pending));
    cleanup(&test);
}

static void test_cancel_and_next_request(void)
{
    TestIdle test;
    QemuThread reader;

    init(&test);
    start_waiter(&test, &reader);
    qemu_mutex_lock(&test.pgraph);
    pgraph_idle_completion_cancel(&test.completion);
    qemu_mutex_unlock(&test.pgraph);
    qemu_thread_join(&reader);
    g_assert_cmpint(test.result, ==, PGRAPH_IDLE_CANCELLED);
    g_assert_cmpuint(test.copies, ==, 0);

    /* The old signalled event must not acknowledge a fresh request. */
    start_waiter(&test, &reader);
    g_assert_false(qatomic_read(&test.returned));
    g_assert_cmpuint(test.completion.requested, ==, 2);
    owner(&test);
    qemu_thread_join(&reader);
    g_assert_cmpint(test.result, ==, PGRAPH_IDLE_COMPLETE);
    g_assert_cmpuint(test.completion.acknowledged, ==, 2);
    g_assert_cmpuint(test.ram, ==, 0xa7);
    cleanup(&test);
}

static void test_changed_frontier(void)
{
    TestIdle test;
    QemuThread reader;

    init(&test);
    start_waiter(&test, &reader);
    qemu_mutex_lock(&test.pfifo);
    test.frontier++;
    qemu_mutex_unlock(&test.pfifo);
    owner(&test);
    qemu_thread_join(&reader);
    g_assert_cmpint(test.result, ==, PGRAPH_IDLE_CANCELLED);
    g_assert_cmpuint(test.copies, ==, 0);
    cleanup(&test);
}

static void test_renderer_started_after_admission(void)
{
    TestIdle test;
    QemuThread reader;

    init(&test);
    start_waiter(&test, &reader);
    /* STARTED closes admission while retaining the old backend. */
    qemu_mutex_lock(&test.pfifo);
    qemu_mutex_lock(&test.pgraph);
    test.eligible = false;
    qemu_mutex_unlock(&test.pgraph);
    qemu_mutex_unlock(&test.pfifo);
    owner(&test);
    qemu_thread_join(&reader);
    g_assert_cmpint(test.result, ==, PGRAPH_IDLE_COMPLETE);
    g_assert_cmpuint(test.ram, ==, 0xa7);
    g_assert_cmpuint(test.copies, ==, 1);
    cleanup(&test);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/nv2a/idle/publication-and-locks",
                    test_publication_and_locks);
    g_test_add_func("/nv2a/idle/skip", test_skip);
    g_test_add_func("/nv2a/idle/renderer-started-after-admission",
                    test_renderer_started_after_admission);
    g_test_add_func("/nv2a/idle/failure", test_failure);
    g_test_add_func("/nv2a/idle/cancel-and-next", test_cancel_and_next_request);
    g_test_add_func("/nv2a/idle/changed-frontier", test_changed_frontier);
    return g_test_run();
}

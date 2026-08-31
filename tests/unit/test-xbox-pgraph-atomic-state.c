/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/thread.h"

#include "hw/xbox/nv2a/pgraph/atomic-state.h"

#define STRESS_ITERATIONS 200000

typedef struct AtomicStateFixture {
    uint32_t regs[8];
    DECLARE_BITMAP(regs_dirty, 8);
    bool waiting_for_nop;
    bool waiting_for_flip;
    bool waiting_for_context_switch;
    bool flush_pending;
    bool done;
} AtomicStateFixture;

static void *atomic_state_reader(void *opaque)
{
    AtomicStateFixture *fixture = opaque;
    while (!qatomic_read(&fixture->done)) {
        (void)pgraph_atomic_reg_read(fixture->regs, 0);
        (void)pgraph_atomic_reg_read(fixture->regs, 4);
        (void)qatomic_read(&fixture->waiting_for_nop);
        (void)qatomic_read(&fixture->waiting_for_flip);
        (void)qatomic_read(&fixture->waiting_for_context_switch);
        (void)qatomic_read(&fixture->flush_pending);
    }

    return NULL;
}

static void test_atomic_polled_state_stress(void)
{
    AtomicStateFixture fixture = { 0 };
    QemuThread reader;

    qemu_thread_create(&reader, "pgraph-atomic-reader", atomic_state_reader,
                       &fixture, QEMU_THREAD_JOINABLE);

    for (uint32_t i = 0; i < STRESS_ITERATIONS; i++) {
        bool state = i & 1;

        pgraph_atomic_reg_write(fixture.regs, fixture.regs_dirty, 0, i);
        pgraph_atomic_reg_write(fixture.regs, fixture.regs_dirty, 4, i);
        qatomic_set(&fixture.waiting_for_nop, state);
        qatomic_set(&fixture.waiting_for_flip, !state);
        qatomic_set(&fixture.waiting_for_context_switch, state);
        qatomic_set(&fixture.flush_pending, !state);
    }

    qatomic_set(&fixture.done, true);
    qemu_thread_join(&reader);

    g_assert_cmpuint(pgraph_atomic_reg_read(fixture.regs, 0), ==,
                     STRESS_ITERATIONS - 1);
    g_assert_cmpuint(pgraph_atomic_reg_read(fixture.regs, 4), ==,
                     STRESS_ITERATIONS - 1);
    g_assert_true(qatomic_read(&fixture.waiting_for_nop));
    g_assert_false(qatomic_read(&fixture.waiting_for_flip));
    g_assert_true(qatomic_read(&fixture.waiting_for_context_switch));
    g_assert_false(qatomic_read(&fixture.flush_pending));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/pgraph/atomic-state/tsan-stress",
                    test_atomic_polled_state_stress);
    return g_test_run();
}

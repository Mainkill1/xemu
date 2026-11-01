/*
 * NV2A BQL/PFIFO/surface-download progress regression
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/core/cpu.h"
#include "hw/xbox/nv2a/pgraph/pgraph.c"

/*
 * Keep the real puller lock transitions and private NOP handler without
 * retaining the complete Kelvin dispatch table in this focused fixture.
 */
static int test_nop_method(NV2AState *d, unsigned int subchannel,
                           unsigned int method, uint32_t parameter,
                           uint32_t *parameters, size_t available,
                           size_t lookahead, bool inc)
{
    size_t consumed = 1;

    g_assert_cmphex(method, ==, NV097_NO_OPERATION);
    pgraph_NV097_NO_OPERATION_handler(d, &d->pgraph, subchannel, method,
                                      parameter, parameters, available,
                                      &consumed, inc);
    return consumed;
}

#define pgraph_method test_nop_method
#include "hw/xbox/nv2a/pfifo.c"
#undef pgraph_method

#include "hw/xbox/nv2a/pgraph/vk/surface.c"

typedef enum TestActor {
    TEST_ACTOR_CONTROL,
    TEST_ACTOR_PFIFO,
} TestActor;

typedef struct TestFixture {
    NV2AState d;
    PGRAPHVkState renderer;
    SurfaceBinding surface;
    QemuThread pfifo_thread;
    uint32_t parameter;
} TestFixture;

static _Thread_local bool test_bql_held;
static _Thread_local TestActor test_actor;
static QemuMutex test_bql;
static bool surface_scenario_active;
static bool irq_levels[4];
static uint32_t irq_pending[4];
static unsigned int irq_update_count;

XemuTweakBits xemu_tweaks_active;
const NV2ABlockInfo blocktable[NV_NUM_BLOCKS] = { 0 };

bool bql_locked(void)
{
    return test_bql_held;
}

void bql_lock_impl(const char *file, int line)
{
    g_assert_false(test_bql_held);
    if (surface_scenario_active && test_actor == TEST_ACTOR_PFIFO) {
        g_test_message("cycle armed: PFIFO waits for BQL while DMA waits for "
                       "the surface download");
    }
    qemu_mutex_lock_impl(&test_bql, file, line);
    test_bql_held = true;
}

void bql_unlock(void)
{
    g_assert_true(test_bql_held);
    test_bql_held = false;
    qemu_mutex_unlock_impl(&test_bql, __FILE__, __LINE__);
}

bool mutex_is_bql(QemuMutex *mutex)
{
    return mutex == &test_bql;
}

void bql_update_status(bool locked)
{
    test_bql_held = locked;
}

void bql_block_unlock(bool increase)
{
    g_assert_true(bql_locked());
}

void nv2a_update_irq(NV2AState *d)
{
    g_assert_true(bql_locked());
    g_assert_cmpuint(irq_update_count, <, G_N_ELEMENTS(irq_levels));
    irq_pending[irq_update_count] = d->pgraph.pending_interrupts;
    irq_levels[irq_update_count] =
        (d->pgraph.pending_interrupts & d->pgraph.enabled_interrupts) != 0;
    irq_update_count++;
}

int64_t nv2a_profile_increment(void)
{
    return 0;
}

void nv2a_profile_log_increment(int64_t now)
{
}

uint64_t memory_region_size(MemoryRegion *mr)
{
    g_assert_not_reached();
}

uint32_t pgraph_rdi_read(PGRAPHState *pg, unsigned int select,
                         unsigned int address)
{
    g_assert_not_reached();
}

void pgraph_rdi_write(PGRAPHState *pg, unsigned int select,
                      unsigned int address, uint32_t value)
{
    g_assert_not_reached();
}

static void fixture_init(TestFixture *fixture)
{
    memset(fixture, 0, sizeof(*fixture));

    qemu_mutex_init(&fixture->d.pfifo.lock);
    qemu_cond_init(&fixture->d.pfifo.fifo_cond);
    qemu_cond_init(&fixture->d.pfifo.fifo_idle_cond);
    qemu_mutex_init(&fixture->d.pgraph.lock);
    qemu_event_init(&fixture->renderer.downloads_complete, false);

    fixture->d.pgraph.vk_renderer_state = &fixture->renderer;
    fixture->surface.d = &fixture->d;
    fixture->surface.vram_addr = 0x1000;
    fixture->surface.size = 0x1000;
    fixture->surface.draw_dirty = true;
    fixture->renderer.downloads_succeeded = true;
    fixture->parameter = 0xd0a00001;
    g_nv2a = &fixture->d;

    pgraph_reg_w(&fixture->d.pgraph, NV_PGRAPH_FIFO,
                 NV_PGRAPH_FIFO_ACCESS);
    pgraph_reg_w(&fixture->d.pgraph, NV_PGRAPH_CTX_CONTROL,
                 NV_PGRAPH_CTX_CONTROL_CHID);
    pgraph_reg_w(&fixture->d.pgraph, NV_PGRAPH_CTX_CACHE1,
                 NV_KELVIN_PRIMITIVE);
    pgraph_reg_w(&fixture->d.pgraph, NV_PGRAPH_CTX_USER, 3U << 24);
    fixture->d.pgraph.enabled_interrupts = NV_PGRAPH_INTR_ERROR;
    fixture->d.pfifo.regs[NV_PFIFO_CACHE1_PULL0] =
        NV_PFIFO_CACHE1_PULL0_ACCESS;
    SET_MASK(fixture->d.pfifo.regs[NV_PFIFO_CACHE1_ENGINE], 3,
             ENGINE_GRAPHICS);

    irq_update_count = 0;
    memset(irq_levels, 0, sizeof(irq_levels));
    memset(irq_pending, 0, sizeof(irq_pending));
}

static void fixture_destroy(TestFixture *fixture)
{
    g_assert_false(test_bql_held);
    qemu_event_destroy(&fixture->renderer.downloads_complete);
    qemu_mutex_destroy(&fixture->d.pgraph.lock);
    qemu_cond_destroy(&fixture->d.pfifo.fifo_idle_cond);
    qemu_cond_destroy(&fixture->d.pfifo.fifo_cond);
    qemu_mutex_destroy(&fixture->d.pfifo.lock);
    g_nv2a = NULL;
}

static void assert_nop_published(TestFixture *fixture)
{
    PGRAPHState *pg = &fixture->d.pgraph;
    uint32_t trapped = pgraph_reg_r(pg, NV_PGRAPH_TRAPPED_ADDR);

    g_assert_cmpuint(GET_MASK(trapped, NV_PGRAPH_TRAPPED_ADDR_CHID), ==, 3);
    g_assert_cmpuint(GET_MASK(trapped, NV_PGRAPH_TRAPPED_ADDR_SUBCH), ==, 0);
    g_assert_cmphex(GET_MASK(trapped, NV_PGRAPH_TRAPPED_ADDR_MTHD), ==,
                    NV097_NO_OPERATION);
    g_assert_cmphex(pgraph_reg_r(pg, NV_PGRAPH_TRAPPED_DATA_LOW), ==,
                    fixture->parameter);
    g_assert_cmphex(pgraph_reg_r(pg, NV_PGRAPH_NSOURCE), ==,
                    NV_PGRAPH_NSOURCE_NOTIFICATION);
    g_assert_true(qatomic_read(&pg->waiting_for_nop));
    g_assert_cmphex(qatomic_read(&pg->pending_interrupts) &
                        NV_PGRAPH_INTR_ERROR,
                    ==, NV_PGRAPH_INTR_ERROR);
    g_assert_true(pfifo_puller_should_stall(&fixture->d));
    g_assert_cmpuint(irq_update_count, ==, 1);
    g_assert_cmphex(irq_pending[0] & NV_PGRAPH_INTR_ERROR, ==,
                    NV_PGRAPH_INTR_ERROR);
    g_assert_true(irq_levels[0]);
}

static void acknowledge_nop(TestFixture *fixture)
{
    fixture->d.pfifo.fifo_kick = false;
    pgraph_write(&fixture->d, NV_PGRAPH_INTR, NV_PGRAPH_INTR_ERROR, 4);

    g_assert_cmphex(qatomic_read(&fixture->d.pgraph.pending_interrupts) &
                        NV_PGRAPH_INTR_ERROR,
                    ==, 0);
    g_assert_false(qatomic_read(&fixture->d.pgraph.waiting_for_nop));
    g_assert_false(pfifo_puller_should_stall(&fixture->d));
    g_assert_true(fixture->d.pfifo.fifo_kick);
    g_assert_cmpuint(irq_update_count, ==, 2);
    g_assert_false(irq_levels[1]);
}

static ssize_t run_nop_puller(TestFixture *fixture)
{
    return pfifo_run_puller(&fixture->d, NV097_NO_OPERATION,
                            fixture->parameter, &fixture->parameter, 1, 1);
}

static void test_irq_ack_contract(void)
{
    TestFixture *fixture = g_new0(TestFixture, 1);

    fixture_init(fixture);
    test_actor = TEST_ACTOR_PFIFO;
    qemu_mutex_lock(&fixture->d.pfifo.lock);
    g_assert_cmpint(run_nop_puller(fixture), ==, 1);
    qemu_mutex_unlock(&fixture->d.pfifo.lock);

    assert_nop_published(fixture);
    test_actor = TEST_ACTOR_CONTROL;
    bql_lock();
    acknowledge_nop(fixture);
    bql_unlock();
    fixture_destroy(fixture);
    g_free(fixture);
}

static void *surface_pfifo_worker(void *opaque)
{
    TestFixture *fixture = opaque;
    PGRAPHVkState *r = &fixture->renderer;

    test_actor = TEST_ACTOR_PFIFO;
    qemu_mutex_lock(&fixture->d.pfifo.lock);
    while (!qatomic_read(&r->downloads_pending)) {
        qemu_cond_wait(&fixture->d.pfifo.fifo_cond,
                       &fixture->d.pfifo.lock);
    }
    fixture->d.pfifo.fifo_kick = false;

    g_assert_cmpint(run_nop_puller(fixture), ==, 1);
    g_assert_true(pfifo_puller_should_stall(&fixture->d));

    /*
     * Model only the successful Vulkan copy boundary. The queueing, locks,
     * wait event, PFIFO puller, NOP publication, and ACK remain production.
     */
    qemu_mutex_unlock(&fixture->d.pfifo.lock);
    qemu_mutex_lock(&fixture->d.pgraph.lock);
    g_assert_true(pgraph_vk_surface_download_complete(
        true, &fixture->surface.download_pending,
        &fixture->surface.draw_dirty));
    qatomic_set(&r->downloads_succeeded, true);
    qatomic_set(&r->downloads_pending, false);
    qemu_event_set(&r->downloads_complete);
    qemu_mutex_unlock(&fixture->d.pgraph.lock);
    return NULL;
}

static void run_surface_progress_child(bool hold_bql)
{
    TestFixture *fixture = g_new0(TestFixture, 1);

    fixture_init(fixture);
    surface_scenario_active = true;
    qemu_thread_create(&fixture->pfifo_thread, "surface-pfifo",
                       surface_pfifo_worker, fixture, QEMU_THREAD_JOINABLE);

    test_actor = TEST_ACTOR_CONTROL;
    if (hold_bql) {
        bql_lock();
    }
    surface_access_callback(&fixture->surface, NULL,
                            fixture->surface.vram_addr, 4, true);
    qemu_thread_join(&fixture->pfifo_thread);
    g_assert_cmpint(bql_locked(), ==, hold_bql);

    g_assert_true(fixture->surface.upload_pending);
    g_assert_false(fixture->surface.download_pending);
    g_assert_false(fixture->surface.draw_dirty);
    g_assert_false(qatomic_read(&fixture->renderer.downloads_pending));
    g_assert_true(qatomic_read(&fixture->renderer.downloads_succeeded));
    assert_nop_published(fixture);
    if (!hold_bql) {
        bql_lock();
    }
    acknowledge_nop(fixture);
    bql_unlock();

    surface_scenario_active = false;
    fixture_destroy(fixture);
    g_free(fixture);
}

static void test_surface_progress_with_bql(void)
{
    if (g_test_subprocess()) {
        run_surface_progress_child(true);
        return;
    }

    g_test_trap_subprocess(NULL, 5 * G_USEC_PER_SEC, 0);
    g_test_trap_assert_passed();
}

static void test_surface_progress_without_bql(void)
{
    if (g_test_subprocess()) {
        run_surface_progress_child(false);
        return;
    }

    g_test_trap_subprocess(NULL, 5 * G_USEC_PER_SEC, 0);
    g_test_trap_assert_passed();
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qemu_mutex_init(&test_bql);
    g_test_add_func("/nv2a/pgraph/nonzero-nop/irq-ack-contract",
                    test_irq_ack_contract);
    g_test_add_func("/nv2a/pgraph/nonzero-nop/surface-download-bql-progress",
                    test_surface_progress_with_bql);
    g_test_add_func("/nv2a/pgraph/nonzero-nop/surface-download-no-bql",
                    test_surface_progress_without_bql);
    int result = g_test_run();
    qemu_mutex_destroy(&test_bql);
    return result;
}

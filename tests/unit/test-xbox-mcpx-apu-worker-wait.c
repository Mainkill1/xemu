/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/main-loop.h"
#include "qemu/bitmap.h"
#include "qom/object.h"
#include "exec/cpu-common.h"
#include "exec/page-vary.h"
#include "exec/ramlist.h"
#include "exec/target_page.h"
#include "exec/translation-block.h"
#include "hw/core/cpu.h"
#include "hw/qdev-core.h"
#include "system/memory.h"
#include "system/ioport.h"
#include "system/ramblock.h"
#include "system/tcg.h"
static QemuCond *spurious_condition;
static bool inject_spurious;
static unsigned injected;

static void wait_with_spurious_wake(QemuCond *condition, QemuMutex *mutex);
#undef qemu_cond_wait
#define qemu_cond_wait wait_with_spurious_wake
#include "hw/xbox/mcpx/apu/vp/vp.c"
#undef qemu_cond_wait

static void wait_with_spurious_wake(QemuCond *condition, QemuMutex *mutex)
{
    if (condition == spurious_condition && inject_spurious) {
        inject_spurious = false;
        injected++;
        return;
    }
    qemu_cond_wait_impl(condition, mutex, __FILE__, __LINE__);
}

struct config g_config;
int g_dbg_voice_monitor = -1;

bool mcpx_apu_debug_is_muted(uint16_t voice)
{
    return false;
}

struct McpxApuDebug g_dbg, g_dbg_cache;

/* Evidence collection is outside this worker-completion fixture. */
bool xemu_shortcut_evidence_enabled(void)
{
    return false;
}

bool xemu_shortcut_evidence_publish_execution(const char *component,
                                            const QDict *fields, Error **errp)
{
    g_assert_not_reached();
}

/* The memory engine is real; these target/CPU hooks are outside this test. */
__thread CPUState *current_cpu;
bool tcg_allowed;

Object *machine_get_container(const char *name)
{
    static Object *container;

    if (!container) {
        container = object_new(TYPE_CONTAINER);
    }
    return container;
}

bool target_big_endian(void)
{
    return false;
}

CPUState *qemu_get_cpu(int index)
{
    return NULL;
}

void tb_invalidate_phys_range(CPUState *cpu, tb_page_addr_t start,
                              tb_page_addr_t end)
{
}

void finalize_target_page_bits(void)
{
}

static uint64_t unassigned_read(void *opaque, hwaddr addr, unsigned size)
{
    return UINT64_MAX;
}

static void unassigned_write(void *opaque, hwaddr addr, uint64_t value,
                             unsigned size)
{
}

const MemoryRegionOps unassigned_io_ops = {
    .read = unassigned_read,
    .write = unassigned_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};


static MemoryRegion ram;
static RAMBlock block;
static MCPXAPUState d;
static uint8_t *bytes;

static void init_memory(void)
{
    const size_t ram_size = 0x10000;
    Object *owner = object_new(TYPE_CONTAINER);

    bytes = g_malloc0(ram_size);
    memory_region_init(&ram, owner, "voice-samples-ram", ram_size);
    ram.ram = true;
    ram.terminates = true;
    block.host = bytes;
    block.used_length = block.max_length = ram_size;
    block.mr = &ram;
    ram.ram_block = &block;
    memory_region_add_subregion(get_system_memory(), 0, &ram);
}

static void test_spurious_finished_wake(gconstpointer opaque)
{
    bool dispatch = GPOINTER_TO_INT(opaque);
    const int pools[] = { 1, 4, 16 };

    for (int p = 0; p < ARRAY_SIZE(pools); p++) {
        memset(&d, 0, sizeof(d));
        d.ram = &ram;
        d.ram_ptr = bytes;
        d.regs[NV_PAPU_VPVADDR] = 0x1000;
        g_config.audio.vp.num_workers = pools[p];
        spurious_condition = &d.vp.voice_work_dispatch.work_finished;
        inject_spurious = !dispatch;
        injected = 0;
        mcpx_apu_vp_init(&d);
        if (dispatch) {
            /* Paused voice still needs a real worker completion. */
            stl_le_phys(&address_space_memory, 0x1000 + NV_PAVS_VOICE_PAR_STATE,
                        NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE |
                            NV_PAVS_VOICE_PAR_STATE_PAUSED);
            voice_work_enqueue(&d, 0, 0);
            inject_spurious = true;
            float bins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME] = { 0 };
            voice_work_dispatch(&d, bins);
            g_assert_cmpuint(d.vp.voice_work_dispatch.workers_pending, ==, 0);
            g_assert_cmpint(d.vp.voice_work_dispatch.queue_len, ==, 0);
        }
        g_assert_cmpuint(injected, ==, 1);
        mcpx_apu_vp_finalize(&d);
        /* No helper or worker may survive this owned pool. */
        g_assert_null(d.vp.voice_work_dispatch.workers);
        qemu_cond_destroy(&d.vp.voice_work_dispatch.work_pending);
        qemu_cond_destroy(&d.vp.voice_work_dispatch.work_finished);
        qemu_mutex_destroy(&d.vp.voice_work_dispatch.lock);
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    module_call_init(MODULE_INIT_QOM);
    cpu_exec_init_all();
    rust_bql_mock_lock();
    init_memory();
    g_test_add_data_func("/xbox/apu/worker-wait/spurious-startup",
                         GINT_TO_POINTER(false), test_spurious_finished_wake);
    g_test_add_data_func("/xbox/apu/worker-wait/spurious-dispatch",
                         GINT_TO_POINTER(true), test_spurious_finished_wake);
    return g_test_run();
}

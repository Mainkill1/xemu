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
#include "hw/xbox/mcpx/apu/vp/vp.c"

struct McpxApuDebug g_dbg, g_dbg_cache;

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

static void setup_voice(int frames, bool stereo)
{
    voice_destroy_resampler(&d.vp.filters[0]);
    memset(&d, 0, sizeof(d));
    memset(bytes, 0, block.used_length);
    d.ram = &ram;
    d.ram_ptr = bytes;
    d.regs[NV_PAPU_VPVADDR] = 0x1000;
    d.regs[NV_PAPU_VPSGEADDR] = 0x2000;
    d.regs[NV_PAPU_FENADDR] = 0x3000;
    d.regs[NV_PAPU_VPSSLADDR] = 0x2800;
    stl_le_phys(&address_space_memory, 0x2000, 0x4000);
    for (int i = 0; i < frames * (stereo ? 2 : 1); i++) {
        stw_le_phys(&address_space_memory, 0x4000 + i * 2, 100 + i);
    }
    voice_set_mask(&d, 0, NV_PAVS_VOICE_CFG_FMT,
                   NV_PAVS_VOICE_CFG_FMT_SAMPLE_SIZE,
                   NV_PAVS_VOICE_CFG_FMT_SAMPLE_SIZE_S16);
    voice_set_mask(&d, 0, NV_PAVS_VOICE_CFG_FMT,
                   NV_PAVS_VOICE_CFG_FMT_CONTAINER_SIZE,
                   NV_PAVS_VOICE_CFG_FMT_CONTAINER_SIZE_B16);
    voice_set_mask(&d, 0, NV_PAVS_VOICE_CFG_FMT,
                   NV_PAVS_VOICE_CFG_FMT_STEREO, stereo);
    voice_set_mask(&d, 0, NV_PAVS_VOICE_CFG_FMT,
                   NV_PAVS_VOICE_CFG_FMT_SAMPLES_PER_BLOCK, stereo);
    voice_set_mask(&d, 0, NV_PAVS_VOICE_PAR_STATE,
                   NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE, 1);
    voice_set_mask(&d, 0, NV_PAVS_VOICE_PAR_NEXT,
                   NV_PAVS_VOICE_PAR_NEXT_EBO, frames - 1);
}

static uint32_t cursor(void)
{
    return voice_get_mask(&d, 0, NV_PAVS_VOICE_PAR_OFFSET,
                          NV_PAVS_VOICE_PAR_OFFSET_CBO);
}

static uint8_t notification(void)
{
    return ldub_phys(&address_space_memory,
                    0x3000 + 16 * MCPX_HW_NOTIFIER_BASE_OFFSET + 15);
}

static void test_inclusive_end(gconstpointer opaque)
{
    bool stereo = GPOINTER_TO_INT(opaque);
    const int frames[] = { 33, 65, 97 };
    float samples[32][2];

    for (int n = 0; n < ARRAY_SIZE(frames); n++) {
        setup_voice(frames[n], stereo);
        for (int offset = 0; offset < frames[n] - 1; offset += 32) {
            bool end;
            g_assert_cmpint(voice_get_samples(&d, 0, samples, 32, &end),
                            ==, 32);
            g_assert_false(end);
            g_assert_cmpuint(cursor(), ==, offset + 32);
        }
        bool end;
        g_assert_cmpint(voice_get_samples(&d, 0, samples, 32, &end), ==, 1);
        g_assert_true(end);
        int last = 100 + (frames[n] - 1) * (stereo ? 2 : 1);
        g_assert_cmpfloat(samples[0][0], ==, int16_to_float(last));
        g_assert_cmpfloat(samples[0][1], ==,
                          int16_to_float(last + (stereo ? 1 : 0)));
    }
}

static void test_loop_boundary(void)
{
    float samples[32][2];
    bool end;

    setup_voice(33, false);
    voice_set_mask(&d, 0, NV_PAVS_VOICE_CFG_FMT,
                   NV_PAVS_VOICE_CFG_FMT_LOOP, 1);
    voice_set_mask(&d, 0, NV_PAVS_VOICE_CUR_PSH_SAMPLE,
                   NV_PAVS_VOICE_CUR_PSH_SAMPLE_LBO, 4);
    g_assert_cmpint(voice_get_samples(&d, 0, samples, 32, &end), ==, 32);
    g_assert_false(end);
    g_assert_cmpuint(cursor(), ==, 32);
    g_assert_cmpint(voice_get_samples(&d, 0, samples, 32, &end), ==, 1);
    g_assert_cmpfloat(samples[0][0], ==, int16_to_float(132));
    g_assert_false(end);
    g_assert_cmpuint(cursor(), ==, 4);
    g_assert_cmpuint(notification(), ==, 0);
}

static void test_stream_notification_boundary(void)
{
    float samples[32][2];
    bool end;

    setup_voice(33, false);
    voice_set_mask(&d, 0, NV_PAVS_VOICE_CFG_FMT,
                   NV_PAVS_VOICE_CFG_FMT_DATA_TYPE, 1);
    voice_set_mask(&d, 0, NV_PAVS_VOICE_CFG_FMT,
                   NV_PAVS_VOICE_CFG_FMT_PERSIST, 1);
    d.vp.ssl[0].count[0] = 1;
    stl_le_phys(&address_space_memory, 0x2800, 0x4000);
    stl_le_phys(&address_space_memory, 0x2804, 33 | (1 << 16));
    g_assert_cmpint(voice_get_samples(&d, 0, samples, 32, &end), ==, 32);
    g_assert_cmpuint(cursor(), ==, 32);
    g_assert_cmpint(d.vp.ssl[0].ssl_index, ==, 0);
    g_assert_cmpuint(notification(), ==, 0);
    g_assert_cmpint(voice_get_samples(&d, 0, samples, 32, &end), ==, 1);
    g_assert_cmpfloat(samples[0][0], ==, int16_to_float(132));
    g_assert_cmpuint(cursor(), ==, 0);
    g_assert_cmpint(d.vp.ssl[0].ssl_index, ==, 1);
    g_assert_cmpuint(notification(), ==,
                     NV1BA0_NOTIFICATION_STATUS_DONE_SUCCESS);
}

static void test_converter_drain(gconstpointer opaque)
{
    int type = GPOINTER_TO_INT(opaque);
    float samples[32][2];
    bool heard_last = false;

    setup_voice(33, false);
    d.vp.resampler_type = type;
    for (int i = 0; i < 33; i++) {
        stw_le_phys(&address_space_memory, 0x4000 + i * 2,
                    i == 32 ? 16384 : 0);
    }
    int blocks;
    for (blocks = 0; blocks < 32; blocks++) {
        int count = voice_resample(&d, 0, samples, 32, 1.0f, false);
        g_assert_cmpint(count, >=, 0);
        for (int i = 0; i < count; i++) {
            heard_last |= fabsf(samples[i][0]) > 0.01f;
        }
        if (count == 0) {
            break;
        }
    }
    g_assert_cmpint(blocks, <, 32);
    g_assert_true(heard_last);
    g_assert_cmpuint(cursor(), ==, 32);
    g_assert_true(d.vp.filters[0].resampler_source_finished);
    g_assert_true(d.vp.filters[0].resampler_deactivate_after_mix);
    g_assert_cmpuint(notification(), ==, 0);
    voice_off(&d, 0);
    g_assert_cmpuint(notification(), ==,
                     NV1BA0_NOTIFICATION_STATUS_DONE_SUCCESS);
    g_assert_cmpuint(voice_get_mask(&d, 0, NV_PAVS_VOICE_PAR_STATE,
                                   NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE), ==, 0);
    mcpx_apu_vp_reset(&d);
    g_assert_null(d.vp.filters[0].resampler);
    g_assert_cmpint(d.vp.filters[0].resampler_channels, ==, 0);
    g_assert_false(d.vp.filters[0].resampler_source_finished);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    module_call_init(MODULE_INIT_QOM);
    cpu_exec_init_all();
    rust_bql_mock_lock();
    init_memory();
    g_test_add_data_func("/xbox/apu/voice-samples/inclusive-end-mono",
                         GINT_TO_POINTER(false), test_inclusive_end);
    g_test_add_data_func("/xbox/apu/voice-samples/inclusive-end-stereo",
                         GINT_TO_POINTER(true), test_inclusive_end);
    g_test_add_func("/xbox/apu/voice-samples/loop-boundary", test_loop_boundary);
    g_test_add_func("/xbox/apu/voice-samples/stream-notification-boundary",
                    test_stream_notification_boundary);
    g_test_add_data_func("/xbox/apu/voice-samples/sinc-finite-drain-reset",
                         GINT_TO_POINTER(SRC_SINC_FASTEST), test_converter_drain);
    g_test_add_data_func("/xbox/apu/voice-samples/linear-finite-drain-reset",
                         GINT_TO_POINTER(SRC_LINEAR), test_converter_drain);
    return g_test_run();
}

/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "migration/qemu-file-types.h"
#include "migration/qemu-file.h"
#include "io/channel-buffer.h"
#include "qapi/error.h"

/* Reuse the real AddressSpace setup and production reader, not sample doubles.
 */
int voice_samples_main(int argc, char **argv);
#define main voice_samples_main
#include "test-xbox-mcpx-apu-voice-samples.c"
#undef main

/*
 * Serialize the production APU fields/subsections and execute its pre_load.
 * PCI fields, device registration, DSP execution and BQL ownership are test
 * doubles. This is a device serialization test, not a complete VM snapshot.
 */
#undef type_init
#define type_init(function) \
    static void (*const unused_register)(void) G_GNUC_UNUSED = function
#include "hw/xbox/mcpx/apu/apu.c"

const VMStateDescription vmstate_pci_device = {
    .name = "pci-device",
    .version_id = 2,
    .minimum_version_id = 1,
    .fields = (VMStateField[]){ VMSTATE_END_OF_LIST() },
};

void dsp_invalidate_opcache(DSPState *dsp)
{
}

struct config g_config;
int g_dbg_voice_monitor = -1;
static bool test_muted;

bool mcpx_apu_debug_is_muted(uint16_t voice)
{
    return test_muted;
}

static MemoryRegion notification_region;
static unsigned notification_writes;

static uint64_t notification_read(void *opaque, hwaddr address, unsigned size)
{
    return bytes[0x3000 + 16 * MCPX_HW_NOTIFIER_BASE_OFFSET + 15];
}

static void notification_write(void *opaque, hwaddr address, uint64_t value,
                               unsigned size)
{
    notification_writes++;
    bytes[0x3000 + 16 * MCPX_HW_NOTIFIER_BASE_OFFSET + 15] = value;
}

static const MemoryRegionOps notification_ops = {
    .read = notification_read,
    .write = notification_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 1 },
};

static void prepare_mix_state(void);

static int round_trip_descriptions(const VMStateDescription *save,
                                   const VMStateDescription *load)
{
    Error *err = NULL;
    QIOChannelBuffer *buffer = qio_channel_buffer_new(0);
    QEMUFile *file = qemu_file_new_output(QIO_CHANNEL(buffer));

    g_assert_cmpint(vmstate_save_state(file, save, &d, NULL, &err), ==, 0);
    g_assert_null(err);
    qemu_put_byte(file, 0); /* Delimit the device section. */
    qemu_fflush(file);
    QIOChannelBuffer *input = qio_channel_buffer_new(buffer->usage);
    memcpy(input->data, buffer->data, buffer->usage);
    input->usage = buffer->usage;
    g_assert_cmpint(qemu_fclose(file), ==, 0);
    file = qemu_file_new_input(QIO_CHANNEL(input));
    int result = vmstate_load_state(file, load, &d, 1, &err);
    if (result == 0) {
        g_assert_null(err);
    } else {
        g_assert_nonnull(err);
        error_free(err);
    }
    qemu_fclose(file);
    object_unref(OBJECT(input));
    object_unref(OBJECT(buffer));
    return result;
}

static void test_terminal_round_trip(gconstpointer opaque)
{
    int type = GPOINTER_TO_INT(opaque);
    float samples[32][2];

    for (int channels = 1; channels <= 2; channels++) {
        setup_voice(64, channels == 2);
        d.vp.resampler_type = type;
        prepare_mix_state();
        d.gp.dsp = g_new0(DSPState, 1);
        d.ep.dsp = g_new0(DSPState, 1);
        d.is_idle = true;
        qemu_mutex_init(&d.lock);
        qemu_cond_init(&d.cond);
        g_assert_cmpint(voice_resample(&d, 0, samples, 32, 1, channels == 2),
                        ==, 32);
        g_assert_cmpint(voice_resample(&d, 0, samples, 32, 1, channels == 2),
                        ==, 32);
        g_assert_true(d.vp.filters[0].resampler_source_finished);
        g_assert_cmpuint(cursor(), ==, 63);
        g_assert_cmpuint(notification(), ==, 0);

        /* Actual device pre_load + real VMState serializer. */
        g_assert_cmpint(
            round_trip_descriptions(&vmstate_mcpx_apu, &vmstate_mcpx_apu), ==,
            0);
        g_assert_null(d.vp.filters[0].resampler);
        g_assert_true(d.vp.filters[0].resampler_source_finished);
        /* Poison the already-read terminal source to expose a refetch. */
        stw_le_phys(&address_space_memory, 0x4000 + 63 * channels * 2, 30000);
        g_assert_cmpint(voice_resample(&d, 0, samples, 32, 1, channels == 2),
                        ==, 0);
        g_assert_true(d.vp.filters[0].resampler_deactivate_after_mix);
        g_assert_cmpuint(notification(), ==, 0);
        float bins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME] = { 0 };
        voice_process(&d, bins, samples, 0, 0);
        g_assert_cmpuint(notification(), ==,
                         NV1BA0_NOTIFICATION_STATUS_DONE_SUCCESS);
        for (int i = 0; i < NUM_SAMPLES_PER_FRAME; i++) {
            g_assert_cmpfloat(bins[0][i], ==, 0);
        }
        voice_destroy_resampler(&d.vp.filters[0]);
        qemu_cond_destroy(&d.cond);
        qemu_mutex_destroy(&d.lock);
        g_free(d.gp.dsp);
        g_free(d.ep.dsp);
    }
}

static void setup_snapshot_voice(void)
{
    setup_voice(64, false);
    d.gp.dsp = g_new0(DSPState, 1);
    d.ep.dsp = g_new0(DSPState, 1);
    d.is_idle = true;
    qemu_mutex_init(&d.lock);
    qemu_cond_init(&d.cond);
}

static void cleanup_snapshot_voice(void)
{
    voice_destroy_resampler(&d.vp.filters[0]);
    qemu_cond_destroy(&d.cond);
    qemu_mutex_destroy(&d.lock);
    g_free(d.gp.dsp);
    g_free(d.ep.dsp);
}

static void prepare_mix_state(void)
{
    d.monitor.point = MCPX_APU_DEBUG_MON_GP_OR_EP;
    stl_le_phys(&address_space_memory, 0x1000 + NV_PAVS_VOICE_CFG_ENVA,
                0xff000000);
    stl_le_phys(&address_space_memory, 0x1000 + NV_PAVS_VOICE_CFG_ENVF,
                0xff000000);
    voice_set_mask(&d, 0, NV_PAVS_VOICE_PAR_STATE,
                   NV_PAVS_VOICE_PAR_STATE_EACUR,
                   NV_PAVS_VOICE_PAR_STATE_EFCUR_SUSTAIN);
    voice_set_mask(&d, 0, NV_PAVS_VOICE_PAR_STATE,
                   NV_PAVS_VOICE_PAR_STATE_EFCUR,
                   NV_PAVS_VOICE_PAR_STATE_EFCUR_SUSTAIN);
}

static void test_automatic_completion(gconstpointer opaque)
{
    int type = GPOINTER_TO_INT(opaque);
    const int lengths[] = { 33, 64 };

    for (int channels = 1; channels <= 2; channels++) {
        for (int muted = 0; muted < 2; muted++) {
            for (int n = 0; n < ARRAY_SIZE(lengths); n++) {
                setup_snapshot_voice();
                voice_set_mask(&d, 0, NV_PAVS_VOICE_PAR_NEXT,
                               NV_PAVS_VOICE_PAR_NEXT_EBO, lengths[n] - 1);
                voice_set_mask(&d, 0, NV_PAVS_VOICE_CFG_FMT,
                               NV_PAVS_VOICE_CFG_FMT_STEREO, channels == 2);
                voice_set_mask(&d, 0, NV_PAVS_VOICE_CFG_FMT,
                               NV_PAVS_VOICE_CFG_FMT_SAMPLES_PER_BLOCK,
                               channels == 2);
                for (int i = 0; i < lengths[n] * channels; i++) {
                    stw_le_phys(&address_space_memory, 0x4000 + i * 2,
                                i / channels >= 32 ? 16384 : 0);
                }
                prepare_mix_state();
                d.vp.resampler_type = type;
                test_muted = muted;
                notification_writes = 0;
                bool heard_tail = false;
                int notifications = 0;
                for (int block = 0; block < 32; block++) {
                    float bins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME] = { 0 };
                    float output[NUM_SAMPLES_PER_FRAME][2] = { 0 };
                    uint8_t before = notification();
                    voice_process(&d, bins, output, 0, 0);
                    notifications += before != notification();
                    for (int i = 0; i < NUM_SAMPLES_PER_FRAME; i++) {
                        heard_tail |= fabsf(bins[0][i]) > 0.01f;
                    }
                    if (notification()) {
                        g_assert_cmpuint(
                            notification(), ==,
                            NV1BA0_NOTIFICATION_STATUS_DONE_SUCCESS);
                        break;
                    }
                }
                g_test_message("type=%d channels=%d muted=%d length=%d tail=%d",
                               type, channels, muted, lengths[n], heard_tail);
                g_assert_cmpint(notifications, ==, 1);
                g_assert_cmpuint(notification_writes, ==, 1);
                float bins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME] = { 0 };
                float output[NUM_SAMPLES_PER_FRAME][2] = { 0 };
                voice_process(&d, bins, output, 0, 0);
                g_assert_cmpuint(notification_writes, ==, 1);
                g_assert_cmpint(heard_tail, ==, !muted);
                g_assert_cmpuint(cursor(), ==, lengths[n] - 1);
                g_assert_cmpuint(
                    voice_get_mask(&d, 0, NV_PAVS_VOICE_PAR_STATE,
                                   NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE),
                    ==, 0);
                cleanup_snapshot_voice();
            }
        }
    }
    test_muted = false;
}

static void test_before_eof_and_pending_output(gconstpointer opaque)
{
    int type = GPOINTER_TO_INT(opaque);
    float samples[32][2];

    for (int before = 0; before <= 1; before++) {
        setup_snapshot_voice();
        d.vp.resampler_type = type;
        prepare_mix_state();
        if (!before) {
            for (int block = 0;
                 !d.vp.filters[0].resampler_source_finished && block < 16;
                 block++) {
                g_assert_cmpint(voice_resample(&d, 0, samples, 32, 4, false),
                                ==, 32);
            }
            /* EOF reached during a full output block, before completion. */
            g_assert_true(d.vp.filters[0].resampler_source_finished);
        }
        g_assert_cmpint(
            round_trip_descriptions(&vmstate_mcpx_apu, &vmstate_mcpx_apu), ==,
            0);
        g_assert_cmpint(d.vp.filters[0].resampler_source_finished, ==, !before);
        int generated = voice_resample(&d, 0, samples, 32, 4, false);
        /* Lost host history is intentional; consumed source must not replay. */
        g_assert_cmpint(generated, ==, before ? 32 : 0);
        cleanup_snapshot_voice();
    }
}

static void test_old_snapshot_unread_terminal(void)
{
    VMStateDescription old = vmstate_mcpx_apu;
    float samples[32][2];
    old.subsections = NULL;

    setup_snapshot_voice();
    d.vp.resampler_type = SRC_SINC_FASTEST;
    voice_set_mask(&d, 0, NV_PAVS_VOICE_PAR_OFFSET,
                   NV_PAVS_VOICE_PAR_OFFSET_CBO, 63);
    g_assert_cmpint(round_trip_descriptions(&old, &vmstate_mcpx_apu), ==, 0);
    g_assert_false(d.vp.filters[0].resampler_source_finished);
    /* CBO == inclusive EBO is still readable in an old snapshot. */
    g_assert_cmpint(voice_resample(&d, 0, samples, 32, 1, false), ==, 1);
    g_assert_cmpfloat(samples[0][0], >, 0);
    cleanup_snapshot_voice();
}

static void test_old_snapshot_clears_stale_eof(void)
{
    VMStateDescription old = vmstate_mcpx_apu;
    old.subsections = NULL;

    setup_snapshot_voice();
    d.vp.filters[0].resampler_source_finished = true;
    d.vp.filters[17].resampler_source_finished = true;
    g_assert_cmpint(round_trip_descriptions(&old, &vmstate_mcpx_apu), ==, 0);
    for (int v = 0; v < MCPX_HW_MAX_VOICES; v++) {
        g_assert_false(d.vp.filters[v].resampler_source_finished);
    }
    cleanup_snapshot_voice();
}

static void test_old_reader_rejects_terminal_subsection(void)
{
    VMStateDescription old = vmstate_mcpx_apu;
    old.subsections = NULL;

    setup_snapshot_voice();
    d.vp.filters[0].resampler_source_finished = true;
    g_assert_cmpint(round_trip_descriptions(&vmstate_mcpx_apu, &old), ==,
                    -ENOENT);
    cleanup_snapshot_voice();
}

static void test_sparse_terminal_flags(void)
{
    setup_snapshot_voice();
    d.vp.filters[17].resampler_source_finished = true;
    d.vp.filters[MCPX_HW_MAX_VOICES - 1].resampler_source_finished = true;
    g_assert_cmpint(
        round_trip_descriptions(&vmstate_mcpx_apu, &vmstate_mcpx_apu), ==, 0);
    for (int v = 0; v < MCPX_HW_MAX_VOICES; v++) {
        g_assert_cmpint(d.vp.filters[v].resampler_source_finished, ==,
                        v == 17 || v == MCPX_HW_MAX_VOICES - 1);
    }
    cleanup_snapshot_voice();
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    module_call_init(MODULE_INIT_QOM);
    cpu_exec_init_all();
    rust_bql_mock_lock();
    init_memory();
    memory_region_init_io(&notification_region, object_new(TYPE_CONTAINER),
                          &notification_ops, NULL, "completion-status", 1);
    memory_region_add_subregion_overlap(
        get_system_memory(), 0x3000 + 16 * MCPX_HW_NOTIFIER_BASE_OFFSET + 15,
        &notification_region, 1);
    g_test_add_data_func("/xbox/apu/vmstate/terminal-sinc",
                         GINT_TO_POINTER(SRC_SINC_FASTEST),
                         test_terminal_round_trip);
    g_test_add_data_func("/xbox/apu/vmstate/terminal-linear",
                         GINT_TO_POINTER(SRC_LINEAR), test_terminal_round_trip);
    g_test_add_data_func("/xbox/apu/vmstate/automatic-completion-sinc",
                         GINT_TO_POINTER(SRC_SINC_FASTEST),
                         test_automatic_completion);
    g_test_add_data_func("/xbox/apu/vmstate/automatic-completion-linear",
                         GINT_TO_POINTER(SRC_LINEAR),
                         test_automatic_completion);
    g_test_add_data_func("/xbox/apu/vmstate/before-eof-pending-sinc",
                         GINT_TO_POINTER(SRC_SINC_FASTEST),
                         test_before_eof_and_pending_output);
    g_test_add_data_func("/xbox/apu/vmstate/before-eof-pending-linear",
                         GINT_TO_POINTER(SRC_LINEAR),
                         test_before_eof_and_pending_output);
    g_test_add_func("/xbox/apu/vmstate/old-snapshot-unread-terminal",
                    test_old_snapshot_unread_terminal);
    g_test_add_func("/xbox/apu/vmstate/old-snapshot-clears-stale-eof",
                    test_old_snapshot_clears_stale_eof);
    g_test_add_func("/xbox/apu/vmstate/old-reader-rejects-new-subsection",
                    test_old_reader_rejects_terminal_subsection);
    g_test_add_func("/xbox/apu/vmstate/sparse-terminal-flags",
                    test_sparse_terminal_flags);
    return g_test_run();
}

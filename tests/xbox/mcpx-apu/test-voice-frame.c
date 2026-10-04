/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "system/cpus.h"
#include "hw/xbox/mcpx/apu/apu_int.h"

/* This fixture drives the production VP API directly. It has no timer,
 * device frame thread, GP/EP DSP program, sound stream, ROM or renderer. */
static MemoryRegion ram;
static const hwaddr ram_base = 0x100000;
static const unsigned voice_count = 45;
static const unsigned first_voice = 64;
static const unsigned warmup_frames = 256;
static bool negative_silence;

enum Payload { MONO_ADPCM, STEREO_ADPCM, MONO_PCM };

static void set_voice_field(uint8_t *voice, unsigned offset, uint32_t mask,
                            uint32_t value)
{
    uint32_t word = ldl_le_p(voice + offset);
    SET_MASK(word, mask, value);
    stl_le_p(voice + offset, word);
}

static MCPXAPUState *prepare_voice_frame(enum Payload payload, unsigned workers)
{
    MCPXAPUState *d = g_new0(MCPXAPUState, 1);
    uint8_t *data = memory_region_get_ram_ptr(&ram);
    memset(data, 0, 65536);
    bool stereo = payload == STEREO_ADPCM;
    bool pcm = payload == MONO_PCM;
    unsigned block_bytes = stereo ? 72 : 36;
    /* Three logical pages map to physical pages 0/2/4; holes are poison. */
    memset(data + 0x9000, 0xa5, 5 * 4096);
    for (unsigned page = 0; page < 3; page++) {
        stl_le_p(data + 0x8000 + page * 8, ram_base + 0x9000 + page * 8192);
    }
    unsigned payload_bytes = pcm ? 8192 : 64 * block_bytes;
    for (unsigned i = 0; i < payload_bytes; i++) {
        uint8_t byte = 0;
        if (pcm) {
            byte = i % 2 ? 0x10 : 0;
        } else if (i % block_bytes == 1) {
            byte = 0x10;
        } else if (stereo && i % block_bytes == 5) {
            byte = 0xf0;
        }
        data[0x9000 + (i / 4096) * 8192 + i % 4096] = byte;
    }
    for (unsigned i = 0; i < voice_count; i++) {
        uint8_t *voice = data + (first_voice + i) * NV_PAVS_SIZE;
        /* Bin 0 takes left, bin 1 takes right; all other sends are muted. */
        stl_le_p(voice + NV_PAVS_VOICE_CFG_VBIN, (1U << 5) | (31U << 10) |
                                                     (31U << 16) | (31U << 21) |
                                                     (31U << 26));
        stl_le_p(voice + NV_PAVS_VOICE_CFG_FMT,
                 31U | (31U << 5) | NV_PAVS_VOICE_CFG_FMT_LOOP);
        set_voice_field(voice, NV_PAVS_VOICE_CFG_FMT,
                        NV_PAVS_VOICE_CFG_FMT_STEREO, stereo);
        set_voice_field(voice, NV_PAVS_VOICE_CFG_FMT,
                        NV_PAVS_VOICE_CFG_FMT_SAMPLES_PER_BLOCK, stereo);
        set_voice_field(voice, NV_PAVS_VOICE_CFG_FMT,
                        NV_PAVS_VOICE_CFG_FMT_SAMPLE_SIZE, 1);
        set_voice_field(voice, NV_PAVS_VOICE_CFG_FMT,
                        NV_PAVS_VOICE_CFG_FMT_CONTAINER_SIZE, pcm ? 1 : 2);
        stl_le_p(voice + NV_PAVS_VOICE_CFG_ENVA, 0xff000000);
        stl_le_p(voice + NV_PAVS_VOICE_CFG_ENVF, 0xff000000);
        stl_le_p(voice + NV_PAVS_VOICE_PAR_STATE,
                 NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE | (5U << 24) |
                     (5U << 28));
        stl_le_p(voice + NV_PAVS_VOICE_PAR_OFFSET, 0xff000000);
        stl_le_p(voice + NV_PAVS_VOICE_PAR_NEXT, 0xff000fff);
        stl_le_p(voice + NV_PAVS_VOICE_TAR_VOLA, 0x000f000f);
        stl_le_p(voice + NV_PAVS_VOICE_TAR_VOLB, 0xffffffff);
        stl_le_p(voice + NV_PAVS_VOICE_TAR_VOLC, 0xffffffff);
        stl_le_p(voice + NV_PAVS_VOICE_TAR_PITCH_LINK,
                 i + 1 == voice_count ? 0xffff : first_voice + i + 1);
    }
    d->regs[NV_PAPU_VPVADDR] = ram_base;
    d->regs[NV_PAPU_VPSGEADDR] = ram_base + 0x8000;
    d->regs[NV_PAPU_TVL2D] = first_voice;
    d->regs[NV_PAPU_TVL3D] = 0xffff;
    d->regs[NV_PAPU_TVLMP] = 0xffff;
    d->vp.submix_headroom[0] = d->vp.submix_headroom[1] = 6;
    d->monitor.point =
        negative_silence ? MCPX_APU_DEBUG_MON_VP : MCPX_APU_DEBUG_MON_GP_OR_EP;
    g_config.audio.vp.num_workers = workers;
    qemu_mutex_init(&d->lock);
    qemu_cond_init(&d->cond);
    mcpx_apu_vp_init(d);
    return d;
}

static void destroy_voice_frame(MCPXAPUState *d)
{
    mcpx_apu_vp_finalize(d);
    qemu_cond_destroy(&d->cond);
    qemu_mutex_destroy(&d->lock);
    g_free(d);
}

static uint64_t run_voice_frame(MCPXAPUState *d, enum Payload payload,
                                double *maximum_error)
{
    float bins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME] = { 0 };
    mcpx_apu_vp_frame(d, bins);
    uint64_t checksum = 0;
    for (unsigned b = 0; b < NUM_MIXBINS; b++) {
        /* 45 voices * (4096 / 32768) / 64 = 45/512. The constant zero-
         * nibble IMA payload and PCM control independently give this value. */
        double expected = b < 2 ? 45.0 / 512.0 : 0;
        if (b == 1 && payload == STEREO_ADPCM) {
            expected = -expected;
        }
        for (unsigned s = 0; s < NUM_SAMPLES_PER_FRAME; s++) {
            double error = fabs(bins[b][s] - expected);
            if (!isfinite(error)) {
                *maximum_error = INFINITY;
            } else {
                *maximum_error = MAX(*maximum_error, error);
            }
            if (b < 2) {
                /* Quantized at 16-bit sample resolution; per-frame maximum
                 * error separately enforces a tighter 32-unit 24-bit bound. */
                checksum += llround(fabs(bins[b][s]) * 32768.0);
            }
        }
    }
    return checksum;
}

static void run_workload(enum Payload payload, unsigned workers,
                         uint64_t frames, bool report)
{
    MCPXAPUState *d = prepare_voice_frame(payload, workers);
    double max_error = 0;
    for (unsigned i = 0; i < warmup_frames; i++) {
        run_voice_frame(d, payload, &max_error);
        /* Sinc startup is outside measurement and output qualification. */
        max_error = 0;
    }
    uint64_t checksum = run_voice_frame(d, payload, &max_error);
    g_assert_cmpuint(checksum, ==, 184320);
    g_assert_cmpfloat(max_error * 8388608.0, <=, 32);
    max_error = 0;
    checksum = 0;
    int64_t started = g_get_monotonic_time();
    for (uint64_t i = 0; i < frames; i++) {
        checksum += run_voice_frame(d, payload, &max_error);
    }
    int64_t elapsed = g_get_monotonic_time() - started;
    g_assert_cmpuint(checksum, ==, frames * 184320);
    g_assert_cmpfloat(max_error * 8388608.0, <=, 32);
    unsigned processed = 0;
    for (unsigned i = 0; i < workers; i++) {
        processed += g_dbg.vp.workers[i].num_voices;
    }
    g_assert_cmpuint(processed, ==, voice_count);
    mcpx_apu_vp_finalize(d);
    uint8_t *data = memory_region_get_ram_ptr(&ram);
    uint32_t final_cbo =
        ldl_le_p(data + first_voice * NV_PAVS_SIZE + NV_PAVS_VOICE_PAR_OFFSET) &
        0xffffff;
    for (unsigned i = 0; i < voice_count; i++) {
        uint8_t *voice = data + (first_voice + i) * NV_PAVS_SIZE;
        g_assert_true(ldl_le_p(voice + NV_PAVS_VOICE_PAR_STATE) &
                      NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE);
        g_assert_cmpuint(ldl_le_p(voice + NV_PAVS_VOICE_PAR_OFFSET) & 0xffffff,
                         ==, final_cbo);
    }
    for (unsigned page = 1; page <= 3; page += 2) {
        for (unsigned i = 0; i < 4096; i++) {
            g_assert_cmpuint(data[0x9000 + page * 4096 + i], ==, 0xa5);
        }
    }
    qemu_cond_destroy(&d->cond);
    qemu_mutex_destroy(&d->lock);
    g_free(d);
    if (report) {
        printf("{\"profile\":\"%s\",\"voices\":45,\"workers\":%u,"
               "\"warmup_frames\":256,\"frames\":%" PRIu64 ","
               "\"samples_per_voice_frame\":32,\"elapsed_us\":%" PRId64 ","
               "\"checksum\":%" PRIu64 ",\"expected_checksum\":%" PRIu64 ","
               "\"final_cbo\":%u,"
               "\"maximum_error_24bit_units\":%.9g,\"correctness\":\"PASS\"}\n",
               payload == MONO_PCM     ? "mono-pcm" :
               payload == STEREO_ADPCM ? "stereo-adpcm" :
                                         "mono-adpcm",
               workers, frames, elapsed, checksum, frames * 184320, final_cbo,
               max_error * 8388608.0);
    }
}

typedef struct FrameObservation {
    float bins[2][NUM_SAMPLES_PER_FRAME];
    uint32_t cbo;
    bool active;
    uint8_t headroom[2];
} FrameObservation;

typedef struct BoundaryFrameThread {
    MCPXAPUState *d;
    QemuEvent locked;
    FrameObservation observation;
} BoundaryFrameThread;

typedef struct BoundaryWriterThread {
    MCPXAPUState *d;
    uint32_t format;
    QemuEvent requested;
} BoundaryWriterThread;

static uint8_t *voice_record(unsigned base, unsigned voice)
{
    uint8_t *data = memory_region_get_ram_ptr(&ram);
    return data + base - ram_base + voice * NV_PAVS_SIZE;
}

static void write_vp_method(MCPXAPUState *d, hwaddr method, uint32_t argument)
{
    vp_ops.write(d, method, argument, sizeof(argument));
}

static FrameObservation observe_one_voice_frame(MCPXAPUState *d)
{
    float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME] = { 0 };
    mcpx_apu_vp_frame(d, mixbins);
    uint8_t *voice = voice_record(d->regs[NV_PAPU_VPVADDR], first_voice);
    FrameObservation observation = {
        .cbo = ldl_le_p(voice + NV_PAVS_VOICE_PAR_OFFSET) & 0xffffff,
        .active = ldl_le_p(voice + NV_PAVS_VOICE_PAR_STATE) &
                  NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE,
        .headroom = {
            d->vp.submix_headroom[0],
            d->vp.submix_headroom[1],
        },
    };
    memcpy(observation.bins, mixbins, sizeof(observation.bins));
    return observation;
}

static void assert_observation_equal(const FrameObservation *actual,
                                     const FrameObservation *expected)
{
    g_assert_cmpuint(actual->cbo, ==, expected->cbo);
    g_assert_cmpuint(actual->active, ==, expected->active);
    g_assert_cmpuint(actual->headroom[0], ==, expected->headroom[0]);
    g_assert_cmpuint(actual->headroom[1], ==, expected->headroom[1]);
    for (unsigned bin = 0; bin < ARRAY_SIZE(actual->bins); bin++) {
        for (unsigned sample = 0; sample < NUM_SAMPLES_PER_FRAME; sample++) {
            g_assert_cmpfloat(actual->bins[bin][sample], ==,
                              expected->bins[bin][sample]);
        }
    }
}

static uint32_t mono_pcm_format(void)
{
    return 31U | (31U << 5) | NV_PAVS_VOICE_CFG_FMT_LOOP |
           (NV_PAVS_VOICE_CFG_FMT_SAMPLE_SIZE_S16 << 28) |
           (NV_PAVS_VOICE_CFG_FMT_CONTAINER_SIZE_B16 << 30);
}

static uint32_t stereo_pcm_format(void)
{
    return mono_pcm_format() | NV_PAVS_VOICE_CFG_FMT_STEREO;
}

static MCPXAPUState *prepare_boundary_voice(uint32_t format)
{
    MCPXAPUState *d = prepare_voice_frame(MONO_PCM, 1);
    uint8_t *voice = voice_record(ram_base, first_voice);
    stl_le_p(voice + NV_PAVS_VOICE_CFG_FMT, format);
    stl_le_p(voice + NV_PAVS_VOICE_TAR_PITCH_LINK, 0xffff);
    return d;
}

static FrameObservation reference_observation(uint32_t format)
{
    MCPXAPUState *d = prepare_boundary_voice(format);
    FrameObservation observation = observe_one_voice_frame(d);
    destroy_voice_frame(d);
    return observation;
}

static void *boundary_frame_thread(void *opaque)
{
    BoundaryFrameThread *thread = opaque;
    qemu_mutex_lock(&thread->d->lock);
    qemu_event_set(&thread->locked);
    thread->observation = observe_one_voice_frame(thread->d);
    qemu_mutex_unlock(&thread->d->lock);
    return NULL;
}

static void *boundary_writer_thread(void *opaque)
{
    BoundaryWriterThread *thread = opaque;
    qemu_event_set(&thread->requested);
    write_vp_method(thread->d, NV1BA0_PIO_VOICE_LOCK, 1);
    write_vp_method(thread->d, NV1BA0_PIO_SET_VOICE_CFG_FMT,
                    thread->format);
    write_vp_method(thread->d, NV1BA0_PIO_VOICE_LOCK, 0);
    return NULL;
}

static void test_method_update_before_frame(void)
{
    FrameObservation expected = reference_observation(stereo_pcm_format());
    MCPXAPUState *d = prepare_boundary_voice(mono_pcm_format());

    write_vp_method(d, NV1BA0_PIO_SET_CURRENT_VOICE, first_voice);
    write_vp_method(d, NV1BA0_PIO_VOICE_LOCK, 1);
    write_vp_method(d, NV1BA0_PIO_SET_VOICE_CFG_FMT, stereo_pcm_format());
    write_vp_method(d, NV1BA0_PIO_VOICE_LOCK, 0);

    FrameObservation actual = observe_one_voice_frame(d);
    assert_observation_equal(&actual, &expected);
    destroy_voice_frame(d);
}

static void test_method_update_waits_for_active_frame(void)
{
    FrameObservation old_expected = reference_observation(mono_pcm_format());
    FrameObservation new_expected = reference_observation(stereo_pcm_format());
    MCPXAPUState *d = prepare_boundary_voice(mono_pcm_format());
    VoiceWorkDispatch *dispatch = &d->vp.voice_work_dispatch;
    BoundaryFrameThread *frame = g_new0(BoundaryFrameThread, 1);
    BoundaryWriterThread *writer = g_new0(BoundaryWriterThread, 1);
    QemuThread frame_thread;
    QemuThread writer_thread;

    frame->d = d;
    writer->d = d;
    writer->format = stereo_pcm_format();
    write_vp_method(d, NV1BA0_PIO_SET_CURRENT_VOICE, first_voice);
    qemu_event_init(&frame->locked, false);
    qemu_event_init(&writer->requested, false);
    qemu_mutex_lock(&dispatch->lock);
    qemu_thread_create(&frame_thread, "voice-boundary-frame",
                       boundary_frame_thread, frame, QEMU_THREAD_JOINABLE);
    qemu_event_wait(&frame->locked);
    qemu_thread_create(&writer_thread, "voice-boundary-writer",
                       boundary_writer_thread, writer, QEMU_THREAD_JOINABLE);
    qemu_event_wait(&writer->requested);
    qemu_mutex_unlock(&dispatch->lock);
    qemu_thread_join(&frame_thread);
    qemu_thread_join(&writer_thread);

    assert_observation_equal(&frame->observation, &old_expected);
    g_assert_cmphex(ldl_le_p(voice_record(ram_base, first_voice) +
                             NV_PAVS_VOICE_CFG_FMT),
                    ==, stereo_pcm_format());

    mcpx_apu_vp_reset(d);
    d->vp.submix_headroom[0] = d->vp.submix_headroom[1] = 6;
    stl_le_p(voice_record(ram_base, first_voice) + NV_PAVS_VOICE_PAR_OFFSET,
             0xff000000);
    FrameObservation next = observe_one_voice_frame(d);
    assert_observation_equal(&next, &new_expected);

    qemu_event_destroy(&writer->requested);
    qemu_event_destroy(&frame->locked);
    g_free(writer);
    g_free(frame);
    destroy_voice_frame(d);
}

static void test_direct_ram_and_table_base_boundaries(void)
{
    FrameObservation expected = reference_observation(stereo_pcm_format());
    MCPXAPUState *d = prepare_boundary_voice(mono_pcm_format());
    uint8_t *voice = voice_record(ram_base, first_voice);

    stl_le_p(voice + NV_PAVS_VOICE_CFG_FMT, stereo_pcm_format());
    FrameObservation direct = observe_one_voice_frame(d);
    assert_observation_equal(&direct, &expected);

    const unsigned alternate_base = ram_base + 0x4000;
    uint8_t *alternate = voice_record(alternate_base, first_voice);
    memcpy(alternate, voice, NV_PAVS_SIZE);
    stl_le_p(alternate + NV_PAVS_VOICE_PAR_OFFSET, 0xff000000);
    mcpx_apu_vp_reset(d);
    d->vp.submix_headroom[0] = d->vp.submix_headroom[1] = 6;
    qatomic_set(&d->regs[NV_PAPU_VPVADDR], alternate_base);
    FrameObservation rebased = observe_one_voice_frame(d);
    assert_observation_equal(&rebased, &expected);

    destroy_voice_frame(d);
}

static void test_voice_frame(gconstpointer opaque)
{
    unsigned variant = GPOINTER_TO_UINT(opaque);
    run_workload(variant % 3, variant < 3 ? 1 : 8, 4, false);
}

int __wrap_main(int argc, char **argv);
int __wrap_main(int argc, char **argv)
{
    bool benchmark = argc > 1 && !strcmp(argv[1], "--benchmark");
    enum Payload payload = MONO_ADPCM;
    uint64_t frames = 0, workers = 0;
    if (benchmark) {
        if (argc != 5 || qemu_strtou64(argv[3], NULL, 10, &frames) ||
            qemu_strtou64(argv[4], NULL, 10, &workers) || !frames ||
            frames > 1000000 || !workers || workers > MAX_VOICE_WORKERS) {
            fprintf(stderr,
                    "usage: --benchmark mono-adpcm|stereo-adpcm|mono-pcm "
                    "frames(1..1000000) workers(1..16)\n");
            return 2;
        }
        if (!strcmp(argv[2], "stereo-adpcm")) {
            payload = STEREO_ADPCM;
        } else if (!strcmp(argv[2], "mono-pcm")) {
            payload = MONO_PCM;
        } else if (strcmp(argv[2], "mono-adpcm")) {
            fprintf(stderr, "unknown voice payload profile\n");
            return 2;
        }
    } else {
        if (argc > 1 && !strcmp(argv[1], "--negative-silence")) {
            negative_silence = true;
            memmove(argv + 1, argv + 2, (argc - 1) * sizeof(*argv));
            argc--;
        }
        g_test_init(&argc, &argv, NULL);
    }
    qemu_init_cpu_loop();
    bql_lock();
    module_call_init(MODULE_INIT_QOM);
    current_machine = MACHINE(object_new("xbox-machine"));
    object_property_add_child(object_get_root(), "machine",
                              OBJECT(current_machine));
    object_property_add_new_container(OBJECT(current_machine), "unattached");
    cpu_exec_init_all();
    memory_region_init_ram(&ram, NULL, "test-voice-pipeline-ram", 65536,
                           &error_fatal);
    memory_region_add_subregion(get_system_memory(), ram_base, &ram);
    if (benchmark) {
        run_workload(payload, workers, frames, true);
        return 0;
    }
    const char *names[] = {
        "/mcpx-apu/voice-frame/mono-adpcm-worker1",
        "/mcpx-apu/voice-frame/stereo-adpcm-worker1",
        "/mcpx-apu/voice-frame/mono-pcm-worker1",
        "/mcpx-apu/voice-frame/mono-adpcm-worker8",
        "/mcpx-apu/voice-frame/stereo-adpcm-worker8",
        "/mcpx-apu/voice-frame/mono-pcm-worker8",
    };
    for (unsigned i = 0; i < ARRAY_SIZE(names); i++) {
        g_test_add_data_func(names[i], GUINT_TO_POINTER(i), test_voice_frame);
    }
    g_test_add_func("/mcpx-apu/voice-frame/method-before-frame",
                    test_method_update_before_frame);
    g_test_add_func("/mcpx-apu/voice-frame/method-waits-for-frame",
                    test_method_update_waits_for_active_frame);
    g_test_add_func("/mcpx-apu/voice-frame/direct-and-table-boundaries",
                    test_direct_ram_and_table_base_boundaries);
    return g_test_run();
}

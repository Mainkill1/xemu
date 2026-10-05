/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "system/cpus.h"
#include "hw/xbox/mcpx/apu/apu_int.h"

/*
 * Drive the production VP API without a timer, device frame thread, GP/EP DSP
 * program, sound stream, ROM or renderer.
 */
static MemoryRegion ram;
static const hwaddr ram_base = 0x100000;
static const unsigned voice_count = 45;
static const unsigned first_voice = 64;
static const unsigned warmup_frames = 256;
static bool negative_silence;
static bool negative_page_boundary;
static int converter = SRC_SINC_FASTEST;
static bool independent_profile;
static MCPXAPUState *assignment_probe;
static unsigned actual_pool, busy_workers;
static unsigned distribution[MAX_VOICE_WORKERS];

void __real_qemu_cond_broadcast(QemuCond *cond);
void __wrap_qemu_cond_broadcast(QemuCond *cond);
void __wrap_qemu_cond_broadcast(QemuCond *cond)
{
    if (assignment_probe &&
        cond == &assignment_probe->vp.voice_work_dispatch.work_pending) {
        /*
         * Production dispatch owns its mutex; inspect before waking workers.
         * Constant workloads probe once; signed checks probe every
         * untimed batch.
         */
        VoiceWorkDispatch *vwd = &assignment_probe->vp.voice_work_dispatch;
        bool seen[45] = { 0 };
        actual_pool = vwd->num_workers;
        busy_workers = 0;
        for (unsigned w = 0; w < actual_pool; w++) {
            VoiceWorker *worker = &vwd->workers[w];
            unsigned expected = independent_profile ?
                45 / actual_pool + (w < 45 % actual_pool) : (w == 0 ? 45 : 0);
            distribution[w] = worker->queue_len;
            g_assert_cmpuint(distribution[w], ==, expected);
            busy_workers += worker->queue_len != 0;
            for (int i = 0; i < worker->queue_len; i++) {
                int id = worker->queue[i].voice - (int)first_voice;
                g_assert_cmpint(id, >=, 0);
                g_assert_cmpint(id, <, 45);
                g_assert_false(seen[id]);
                seen[id] = true;
            }
        }
        for (unsigned i = 0; i < 45; i++) {
            g_assert_true(seen[i]);
        }
        g_assert_cmpuint(vwd->workers_pending, ==,
                         (UINT64_C(1) << busy_workers) - 1);
        assignment_probe = NULL;
    }
    __real_qemu_cond_broadcast(cond);
}

enum Payload { MONO_ADPCM, STEREO_ADPCM, MONO_PCM };

SRC_STATE *__real_src_callback_new(src_callback_t callback, int type,
                                   int channels, int *error, void *opaque);
SRC_STATE *__wrap_src_callback_new(src_callback_t callback, int type,
                                   int channels, int *error, void *opaque);
SRC_STATE *__wrap_src_callback_new(src_callback_t callback, int type,
                                   int channels, int *error, void *opaque)
{
    #ifdef MCPX_TEST_RESAMPLER_SELECTOR
    g_assert_cmpint(type, ==, converter);
#else
    g_assert_cmpint(type, ==, SRC_SINC_FASTEST);
    type = converter;
#endif
    return __real_src_callback_new(callback, type, channels, error, opaque);
}

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
    unsigned unused_bin = independent_profile ? 2 : 31;
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
        stl_le_p(voice + NV_PAVS_VOICE_CFG_VBIN,
                 (1U << 5) | (unused_bin << 10) | (unused_bin << 16) |
                 (unused_bin << 21) | (unused_bin << 26));
        stl_le_p(voice + NV_PAVS_VOICE_CFG_FMT,
                 unused_bin | (unused_bin << 5) | NV_PAVS_VOICE_CFG_FMT_LOOP);
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
#ifdef MCPX_TEST_RESAMPLER_SELECTOR
    g_config.audio.vp.resampler = converter == SRC_LINEAR ?
        CONFIG_AUDIO_VP_RESAMPLER_LINEAR : CONFIG_AUDIO_VP_RESAMPLER_SINC;
#endif
    mcpx_apu_vp_init(d);
    return d;
}

static uint64_t
validate_voice_frame(float bins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME],
                     enum Payload payload, double *maximum_error)
{
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

static bool vp_only_timing;
static bool changing_encoded_data;

static uint64_t run_voice_frame(MCPXAPUState *d, enum Payload payload,
                                double *maximum_error)
{
    float bins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME] = { 0 };
    mcpx_apu_vp_frame(d, bins);
    return validate_voice_frame(bins, payload, maximum_error);
}

/*
 * Toggle zero-delta nibble signs at index 0: fresh bytes, known constant PCM.
 */
static void change_encoded_payload(enum Payload payload, uint64_t frame)
{
    if (payload == MONO_PCM) {
        return;
    }
    uint8_t *data = memory_region_get_ram_ptr(&ram);
    unsigned block_size = payload == STEREO_ADPCM ? 72 : 36;
    unsigned header_size = payload == STEREO_ADPCM ? 8 : 4;
    for (unsigned block = 0; block < 64; block++) {
        for (unsigned byte = header_size; byte < block_size; byte++) {
            unsigned offset = block * block_size + byte;
            data[0x9000 + offset / 4096 * 8192 + offset % 4096] =
                frame % 2 ? 0 : 0x88;
        }
    }
}

typedef struct ProgressOracle {
    uint64_t supplied_frames;
    float input[NUM_SAMPLES_PER_FRAME * 2];
} ProgressOracle;

static long oracle_input(void *opaque, float **data)
{
    ProgressOracle *oracle = opaque;
    *data = oracle->input;
    oracle->supplied_frames += NUM_SAMPLES_PER_FRAME;
    return NUM_SAMPLES_PER_FRAME;
}

static uint32_t expected_buffer_offset(int channels, uint64_t vp_frames)
{
    /*
     * Independent SRC callback: no guest cursor or VP reader is consulted.
     * Count actual input requests, including converter-specific read-ahead.
     */
    ProgressOracle oracle = { 0 };
    for (unsigned i = 0; i < ARRAY_SIZE(oracle.input); i++) {
        oracle.input[i] = 0.125f;
    }
    int error;
    SRC_STATE *src = __real_src_callback_new(oracle_input, converter, channels,
                                            &error, &oracle);
    g_assert_nonnull(src);
    g_assert_cmpint(error, ==, 0);
    for (uint64_t i = 0; i < vp_frames; i++) {
        float output[NUM_SAMPLES_PER_FRAME * 2];
        g_assert_cmpint(src_callback_read(src, 1.0, NUM_SAMPLES_PER_FRAME,
                                         output), ==, NUM_SAMPLES_PER_FRAME);
    }
    src_delete(src);
    return oracle.supplied_frames % 4096;
}

static void run_workload(enum Payload payload, unsigned workers,
                         uint64_t frames, bool report)
{
    MCPXAPUState *d = prepare_voice_frame(payload, workers);
    assignment_probe = d;
    double max_error = 0;
    for (unsigned i = 0; i < warmup_frames; i++) {
        run_voice_frame(d, payload, &max_error);
        /* Sinc startup is outside measurement and output qualification. */
        max_error = 0;
    }
    uint64_t checksum = run_voice_frame(d, payload, &max_error);
    g_assert_cmpuint(checksum, ==, 184320);
    g_assert_cmpfloat(max_error * 8388608.0, <=, 32);
    if (negative_page_boundary) {
        /*
         * Corrupt only the next logical page after all warmup/probe work.
         * Four checked frames cannot reach it; a full checked loop must.
         */
        uint8_t *data = memory_region_get_ram_ptr(&ram);
        stl_le_p(data + 0x8000 + 8, ram_base + 0xa000);
    }
    max_error = 0;
    checksum = 0;
    int64_t elapsed = 0;
    int64_t started = g_get_monotonic_time();
    for (uint64_t i = 0; i < frames; i++) {
        if (vp_only_timing) {
            float bins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME] = { 0 };
            if (changing_encoded_data) {
                change_encoded_payload(payload, i);
            }
            int64_t frame_start = g_get_monotonic_time();
            mcpx_apu_vp_frame(d, bins);
            elapsed += g_get_monotonic_time() - frame_start;
            checksum += validate_voice_frame(bins, payload, &max_error);
        } else {
            checksum += run_voice_frame(d, payload, &max_error);
        }
    }
    if (!vp_only_timing) {
        elapsed = g_get_monotonic_time() - started;
    }
    g_assert_cmpuint(checksum, ==, frames * 184320);
    g_assert_cmpfloat(max_error * 8388608.0, <=, 32);
    mcpx_apu_vp_finalize(d);
    uint8_t *data = memory_region_get_ram_ptr(&ram);
    uint32_t expected_cbo = expected_buffer_offset(
        payload == STEREO_ADPCM ? 2 : 1, warmup_frames + 1 + frames);
    uint32_t final_cbo =
        ldl_le_p(data + first_voice * NV_PAVS_SIZE + NV_PAVS_VOICE_PAR_OFFSET) &
        0xffffff;
    for (unsigned i = 0; i < voice_count; i++) {
        uint8_t *voice = data + (first_voice + i) * NV_PAVS_SIZE;
        g_assert_true(ldl_le_p(voice + NV_PAVS_VOICE_PAR_STATE) &
                      NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE);
        g_assert_cmpuint(ldl_le_p(voice + NV_PAVS_VOICE_PAR_OFFSET) & 0xffffff,
                         ==, expected_cbo);
    }
    for (unsigned page = 1; page <= 3; page += 2) {
        for (unsigned i = 0; i < 4096; i++) {
            g_assert_cmpuint(data[0x9000 + page * 4096 + i], ==, 0xa5);
        }
    }
    g_free(d);
    if (report) {
        printf("{\"converter\":\"%s\",\"profile\":\"%s\","
               "\"voices\":45,\"workers\":%u,"
               "\"warmup_frames\":256,\"frames\":%" PRIu64 ","
               "\"samples_per_voice_frame\":32,\"elapsed_us\":%" PRId64 ","
               "\"checksum\":%" PRIu64 ",\"expected_checksum\":%" PRIu64 ","
               "\"final_cbo\":%u,\"expected_cbo\":%u,"
               "\"workload_version\":%u,\"routing\":\"%s\","
               "\"actual_workers\":%u,\"busy_workers\":%u,"
               "\"probe_frames\":1,\"pitch\":0,\"ratio\":1,"
               "\"adpcm_decode\":\"%s\",\"library\":\"%s\","
               "\"timing_scope\":\"%s\","
               "\"distribution\":[",
               converter == SRC_LINEAR ? "linear" : "sinc",
               payload == MONO_PCM     ? "mono-pcm" :
               payload == STEREO_ADPCM ? "stereo-adpcm" :
                                         "mono-adpcm",
               workers, frames, elapsed, checksum, frames * 184320, final_cbo,
               expected_cbo, vp_only_timing ? 3 : (independent_profile ? 2 : 1),
               independent_profile ? "independent" : "grouped-one-busy-worker",
               actual_pool, busy_workers,
               payload == MONO_PCM ?
                   "N/A" :
                   (changing_encoded_data ?
                        "encoded-byte-change; unchanged PCM oracle" :
                        "encoded-byte-cache-hit"),
               src_get_version(),
               vp_only_timing ?
                   "VP-only; per-frame clocks; full validation outside" :
                   "VP+clear+validation+checksum");
        for (unsigned w = 0; w < actual_pool; w++) {
            printf("%s%u", w ? "," : "", distribution[w]);
        }
        printf("],\"maximum_error_24bit_units\":%.9g,"
               "\"correctness\":\"PASS\"}\n", max_error * 8388608.0);
    }
}

static void test_voice_frame(gconstpointer opaque)
{
    unsigned variant = GPOINTER_TO_UINT(opaque);
    converter = variant < 6 ? SRC_SINC_FASTEST : SRC_LINEAR;
    variant %= 6;
    run_workload(variant % 3, variant < 3 ? 1 : 8, 132, false);
}

static void test_independent_workers(gconstpointer opaque)
{
    independent_profile = true;
    converter = GPOINTER_TO_UINT(opaque) ? SRC_LINEAR : SRC_SINC_FASTEST;
    run_workload(MONO_PCM, 8, 132, false);
    independent_profile = false;
}

static void test_vp_only_workload(gconstpointer opaque)
{
    unsigned variant = GPOINTER_TO_UINT(opaque);
    converter = variant < 4 ? SRC_SINC_FASTEST : SRC_LINEAR;
    variant %= 4;
    independent_profile = true;
    vp_only_timing = true;
    changing_encoded_data = variant >= 2;
    run_workload(variant % 2 ? STEREO_ADPCM : MONO_ADPCM, 8, 132, false);
    changing_encoded_data = false;
    vp_only_timing = false;
    independent_profile = false;
}

/* Independent queues also need a reduction check with nonidentical voices. */
static void test_signed_reduction(gconstpointer opaque)
{
    float reference[16][2][NUM_SAMPLES_PER_FRAME];
    const unsigned pools[] = { 1, 8 };
    converter = GPOINTER_TO_UINT(opaque) ? SRC_LINEAR : SRC_SINC_FASTEST;
    independent_profile = true;
    for (unsigned r = 0; r < ARRAY_SIZE(pools); r++) {
        MCPXAPUState *d = prepare_voice_frame(MONO_PCM, pools[r]);
        uint8_t *data = memory_region_get_ram_ptr(&ram);
        d->vp.submix_headroom[0] = d->vp.submix_headroom[1] = 0;
        for (unsigned v = 0; v < voice_count; v++) {
            uint8_t *voice = data + (first_voice + v) * NV_PAVS_SIZE;
            set_voice_field(voice, NV_PAVS_VOICE_CUR_PSL_START,
                            NV_PAVS_VOICE_CUR_PSL_START_BA, v * 256);
            set_voice_field(voice, NV_PAVS_VOICE_PAR_NEXT,
                            NV_PAVS_VOICE_PAR_NEXT_EBO, 127);
            for (unsigned sample = 0; sample < 128; sample++) {
                int sign = (sample + v * 3) % 128 < 64 ? 1 : -1;
                unsigned offset = v * 256 + sample * 2;
                stw_le_p(data + 0x9000 + offset / 4096 * 8192 + offset % 4096,
                         (uint16_t)(sign * (32000 - v * 71)));
            }
        }
        float peak = 0;
        for (unsigned frame = 0; frame < ARRAY_SIZE(reference); frame++) {
            float bins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME] = { 0 };
            /* Untimed, exactly-once membership per batch. */
            assignment_probe = d;
            mcpx_apu_vp_frame(d, bins);
            for (unsigned b = 0; b < NUM_MIXBINS; b++) {
                for (unsigned i = 0; i < NUM_SAMPLES_PER_FRAME; i++) {
                    g_assert_true(isfinite(bins[b][i]));
                    if (b < 2) {
                        peak = fmaxf(peak, fabsf(bins[b][i]));
                        if (!r) {
                            reference[frame][b][i] = bins[b][i];
                        } else {
                            /* Forty-five FP additions in different orders. */
                            g_assert_cmpfloat_with_epsilon(
                                bins[b][i], reference[frame][b][i], 0.00002f);
                        }
                    } else {
                        g_assert_cmpfloat(bins[b][i], ==, 0);
                    }
                }
            }
        }
        g_assert_cmpfloat(peak, >, 0.9f);
        mcpx_apu_vp_finalize(d);
        uint32_t expected_cbo = expected_buffer_offset(1, 16) % 128;
        for (unsigned v = 0; v < voice_count; v++) {
            uint8_t *voice = data + (first_voice + v) * NV_PAVS_SIZE;
            g_assert_cmpuint(ldl_le_p(voice + NV_PAVS_VOICE_PAR_OFFSET) &
                             0xffffff, ==, expected_cbo);
        }
        g_free(d);
    }
    independent_profile = false;
}

int __wrap_main(int argc, char **argv);
int __wrap_main(int argc, char **argv)
{
    bool benchmark = argc > 1 && (!strcmp(argv[1], "--benchmark") ||
                                  !strcmp(argv[1], "--benchmark-v2") ||
                                  !strcmp(argv[1], "--benchmark-v3") ||
                                  !strcmp(argv[1], "--benchmark-v3-changing"));
    vp_only_timing = benchmark && (!strcmp(argv[1], "--benchmark-v3") ||
                                   !strcmp(argv[1], "--benchmark-v3-changing"));
    changing_encoded_data =
        benchmark && !strcmp(argv[1], "--benchmark-v3-changing");
    independent_profile =
        benchmark && (vp_only_timing || !strcmp(argv[1], "--benchmark-v2"));
    enum Payload payload = MONO_ADPCM;
    uint64_t frames = 0, workers = 0;
    if (benchmark) {
        if (argc != 6 || qemu_strtou64(argv[4], NULL, 10, &frames) ||
            qemu_strtou64(argv[5], NULL, 10, &workers) || !frames ||
            frames > 1000000 || !workers || workers > MAX_VOICE_WORKERS) {
            fprintf(stderr,
                    "usage: --benchmark[-v2|-v3|-v3-changing] sinc|linear "
                    "mono-adpcm|stereo-adpcm|mono-pcm "
                    "frames(1..1000000) workers(1..16)\n");
            return 2;
        }
        if (!strcmp(argv[2], "linear")) {
            converter = SRC_LINEAR;
        } else if (strcmp(argv[2], "sinc")) {
            fprintf(stderr, "unknown converter\n");
            return 2;
        }
        if (!strcmp(argv[3], "stereo-adpcm")) {
            payload = STEREO_ADPCM;
        } else if (!strcmp(argv[3], "mono-pcm")) {
            payload = MONO_PCM;
        } else if (strcmp(argv[3], "mono-adpcm")) {
            fprintf(stderr, "unknown voice payload profile\n");
            return 2;
        }
    } else {
        if (argc > 1 && (!strcmp(argv[1], "--negative-silence") ||
                         !strcmp(argv[1], "--negative-page-boundary"))) {
            negative_silence = !strcmp(argv[1], "--negative-silence");
            negative_page_boundary =
                !strcmp(argv[1], "--negative-page-boundary");
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
        "/mcpx-apu/voice-frame/linear/mono-adpcm-worker1",
        "/mcpx-apu/voice-frame/linear/stereo-adpcm-worker1",
        "/mcpx-apu/voice-frame/linear/mono-pcm-worker1",
        "/mcpx-apu/voice-frame/linear/mono-adpcm-worker8",
        "/mcpx-apu/voice-frame/linear/stereo-adpcm-worker8",
        "/mcpx-apu/voice-frame/linear/mono-pcm-worker8",
    };
    for (unsigned i = 0; i < ARRAY_SIZE(names); i++) {
        g_test_add_data_func(names[i], GUINT_TO_POINTER(i), test_voice_frame);
    }
    g_test_add_data_func("/mcpx-apu/voice-frame/independent-v2/sinc",
                         GUINT_TO_POINTER(0), test_independent_workers);
    g_test_add_data_func("/mcpx-apu/voice-frame/independent-v2/linear",
                         GUINT_TO_POINTER(1), test_independent_workers);
    g_test_add_data_func("/mcpx-apu/voice-frame/signed-reduction/sinc",
                         GUINT_TO_POINTER(0), test_signed_reduction);
    g_test_add_data_func("/mcpx-apu/voice-frame/signed-reduction/linear",
                         GUINT_TO_POINTER(1), test_signed_reduction);
    const char *v3_names[] = {
        "/mcpx-apu/voice-frame/v3/sinc/mono-constant",
        "/mcpx-apu/voice-frame/v3/sinc/stereo-constant",
        "/mcpx-apu/voice-frame/v3/sinc/mono-changing",
        "/mcpx-apu/voice-frame/v3/sinc/stereo-changing",
        "/mcpx-apu/voice-frame/v3/linear/mono-constant",
        "/mcpx-apu/voice-frame/v3/linear/stereo-constant",
        "/mcpx-apu/voice-frame/v3/linear/mono-changing",
        "/mcpx-apu/voice-frame/v3/linear/stereo-changing",
    };
    for (unsigned i = 0; i < ARRAY_SIZE(v3_names); i++) {
        g_test_add_data_func(v3_names[i], GUINT_TO_POINTER(i),
                             test_vp_only_workload);
    }
    return g_test_run();
}

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "system/cpus.h"
#include "hw/xbox/mcpx/apu/apu_int.h"

#include <math.h>
#include <samplerate.h>

/* GNU ld wraps only this test executable. The emulator still constructs
 * SRC_SINC_FASTEST. No VP, fetch, notification or mixing implementation is
 * substituted. One voice on one worker makes each trace ordered. */
static MemoryRegion ram;
static const hwaddr ram_base = 0x100000;
static const unsigned voice_handle = 64;
static int converter = SRC_SINC_FASTEST;
static bool report_matrix;
static bool report_trace;
static bool require_finite_tail;
static bool negative_silence;

typedef struct Trace {
    src_callback_t callback;
    void *opaque;
    int channels;
    unsigned callbacks;
    unsigned payload_frames;
    unsigned generated_frames;
    unsigned active_transitions;
    unsigned notify_transitions;
    unsigned callback_at_off;
    unsigned frame_at_off;
    unsigned vp_frame;
    unsigned last_cbo;
} Trace;

static Trace trace;

enum Payload { MONO_PCM, STEREO_PCM, MONO_ADPCM, STEREO_ADPCM };
static const char *const payload_names[] = {
    "mono-pcm",
    "stereo-pcm",
    "mono-adpcm",
    "stereo-adpcm",
};

static uint8_t *voice_ptr(void)
{
    return memory_region_get_ram_ptr(&ram) + voice_handle * NV_PAVS_SIZE;
}

static bool voice_active(void)
{
    return ldl_le_p(voice_ptr() + NV_PAVS_VOICE_PAR_STATE) &
           NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE;
}

static uint8_t *notifier_ptr(void)
{
    return memory_region_get_ram_ptr(&ram) + 0xc000 +
           16 * (MCPX_HW_NOTIFIER_BASE_OFFSET +
                 voice_handle * MCPX_HW_NOTIFIER_COUNT +
                 MCPX_HW_NOTIFIER_SSLA_DONE) +
           15;
}

static long traced_callback(void *opaque, float **data)
{
    Trace *t = opaque;
    bool active = voice_active();
    unsigned notified = *notifier_ptr();
    long count = t->callback(t->opaque, data);
    g_assert_cmpint(count, ==, NUM_SAMPLES_PER_FRAME);
    t->callbacks++;
    for (long i = 0; i < count; i++) {
        /* Constant nonzero PCM / zero-nibble IMA payload identifies actual
         * payload versus the real callback's silence padding. This is not a
         * general-purpose fetched-frame counter for arbitrary audio. */
        if ((*data)[i * t->channels] != 0) {
            g_assert_cmpfloat((*data)[i * t->channels], ==, 0.125f);
            t->payload_frames++;
        }
    }
    if (active && !voice_active()) {
        t->active_transitions++;
        t->callback_at_off = t->callbacks;
        t->frame_at_off = t->vp_frame;
    }
    if (notified != *notifier_ptr()) {
        t->notify_transitions++;
    }
    t->last_cbo = ldl_le_p(voice_ptr() + NV_PAVS_VOICE_PAR_OFFSET) & 0xffffff;
    if (report_trace) {
        printf("{\"kind\":\"callback\",\"vp_frame\":%u,"
               "\"callback\":%u,\"returned_frames\":%ld,"
               "\"payload_frames_total\":%u,\"cbo\":%u,"
               "\"active\":%s,\"notifier\":%u}\n",
               t->vp_frame, t->callbacks, count, t->payload_frames, t->last_cbo,
               voice_active() ? "true" : "false", *notifier_ptr());
    }
    return count;
}

SRC_STATE *__real_src_callback_new(src_callback_t callback, int type,
                                   int channels, int *error, void *opaque);
SRC_STATE *__wrap_src_callback_new(src_callback_t callback, int type,
                                   int channels, int *error, void *opaque);
SRC_STATE *__wrap_src_callback_new(src_callback_t callback, int type,
                                   int channels, int *error, void *opaque)
{
    g_assert_cmpint(type, ==, SRC_SINC_FASTEST);
    trace.callback = callback;
    trace.opaque = opaque;
    trace.channels = channels;
    return __real_src_callback_new(traced_callback, converter, channels, error,
                                   &trace);
}

long __real_src_callback_read(SRC_STATE *state, double ratio, long frames,
                              float *output);
long __wrap_src_callback_read(SRC_STATE *state, double ratio, long frames,
                              float *output);
long __wrap_src_callback_read(SRC_STATE *state, double ratio, long frames,
                              float *output)
{
    long count = __real_src_callback_read(state, ratio, frames, output);
    g_assert_cmpint(count, ==, frames);
    trace.generated_frames += count;
    return count;
}

static void set_voice_field(unsigned offset, uint32_t mask, uint32_t value)
{
    uint32_t word = ldl_le_p(voice_ptr() + offset);
    SET_MASK(word, mask, value);
    stl_le_p(voice_ptr() + offset, word);
}

static MCPXAPUState *prepare(enum Payload payload, unsigned frames, int pitch)
{
    MCPXAPUState *d = g_new0(MCPXAPUState, 1);
    uint8_t *data = memory_region_get_ram_ptr(&ram);
    bool stereo = payload == STEREO_PCM || payload == STEREO_ADPCM;
    bool pcm = payload == MONO_PCM || payload == STEREO_PCM;
    unsigned channels = stereo ? 2 : 1;
    memset(data, 0, 65536);
    memset(&trace, 0, sizeof(trace));
    stl_le_p(data + 0x8000, ram_base + 0x9000);
    if (pcm) {
        for (unsigned i = 0; i < frames; i++) {
            stw_le_p(data + 0x9000 + i * channels * 2, 4096);
            if (stereo) {
                stw_le_p(data + 0x9000 + i * channels * 2 + 2, -4096);
            }
        }
    } else {
        for (unsigned i = 0; i < DIV_ROUND_UP(frames, 64); i++) {
            stw_le_p(data + 0x9000 + i * 36 * channels, 4096);
            if (stereo) {
                stw_le_p(data + 0x9000 + i * 36 * channels + 4, -4096);
            }
        }
    }
    uint8_t *v = voice_ptr();
    stl_le_p(v + NV_PAVS_VOICE_CFG_VBIN,
             (1U << 5) | (31U << 10) | (31U << 16) | (31U << 21) | (31U << 26));
    stl_le_p(v + NV_PAVS_VOICE_CFG_FMT, 31U | (31U << 5));
    set_voice_field(NV_PAVS_VOICE_CFG_FMT, NV_PAVS_VOICE_CFG_FMT_STEREO,
                    stereo);
    set_voice_field(NV_PAVS_VOICE_CFG_FMT,
                    NV_PAVS_VOICE_CFG_FMT_SAMPLES_PER_BLOCK, stereo);
    set_voice_field(NV_PAVS_VOICE_CFG_FMT, NV_PAVS_VOICE_CFG_FMT_SAMPLE_SIZE,
                    1);
    set_voice_field(NV_PAVS_VOICE_CFG_FMT, NV_PAVS_VOICE_CFG_FMT_CONTAINER_SIZE,
                    pcm ? 1 : 2);
    stl_le_p(v + NV_PAVS_VOICE_CFG_ENVA, 0xff000000);
    stl_le_p(v + NV_PAVS_VOICE_CFG_ENVF, 0xff000000);
    stl_le_p(v + NV_PAVS_VOICE_PAR_STATE,
             NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE | (5U << 24) | (5U << 28));
    stl_le_p(v + NV_PAVS_VOICE_PAR_OFFSET, 0xff000000);
    stl_le_p(v + NV_PAVS_VOICE_PAR_NEXT, 0xff000000 | (frames - 1));
    stl_le_p(v + NV_PAVS_VOICE_TAR_VOLA, 0x000f000f);
    stl_le_p(v + NV_PAVS_VOICE_TAR_VOLB, 0xffffffff);
    stl_le_p(v + NV_PAVS_VOICE_TAR_VOLC, 0xffffffff);
    stl_le_p(v + NV_PAVS_VOICE_TAR_PITCH_LINK,
             ((uint32_t)(uint16_t)pitch << 16) | 0xffff);
    *notifier_ptr() = 0xa5;
    d->regs[NV_PAPU_VPVADDR] = ram_base;
    d->regs[NV_PAPU_VPSGEADDR] = ram_base + 0x8000;
    d->regs[NV_PAPU_FENADDR] = ram_base + 0xc000;
    d->regs[NV_PAPU_FETFORCE1] = NV_PAPU_FETFORCE1_SE2FE_IDLE_VOICE;
    d->regs[NV_PAPU_TVL2D] = voice_handle;
    d->regs[NV_PAPU_TVL3D] = d->regs[NV_PAPU_TVLMP] = 0xffff;
    d->vp.submix_headroom[0] = d->vp.submix_headroom[1] = 6;
    d->monitor.point =
        negative_silence ? MCPX_APU_DEBUG_MON_VP : MCPX_APU_DEBUG_MON_GP_OR_EP;
    g_config.audio.vp.num_workers = 1;
    mcpx_apu_vp_init(d);
    return d;
}

static void run_case(enum Payload payload, unsigned frames, int pitch)
{
    MCPXAPUState *d = prepare(payload, frames, pitch);
    if (report_trace) {
        printf("{\"kind\":\"begin\",\"converter\":\"%s\","
               "\"profile\":\"%s\",\"input_frames\":%u,\"pitch\":%d}\n",
               converter == SRC_LINEAR ? "linear" : "sinc",
               payload_names[payload], frames, pitch);
    }
    unsigned mixed_frames = 0;
    double mixed_energy = 0;
    bool stereo = payload == STEREO_PCM || payload == STEREO_ADPCM;
    /* Slowest source rate is 4x upsampling. Include ample tail and two
     * post-completion frames; no host clock determines correctness. */
    unsigned limit = DIV_ROUND_UP(frames * 4, NUM_SAMPLES_PER_FRAME) + 32;
    unsigned post_completion = 0;
    for (trace.vp_frame = 1; trace.vp_frame <= limit; trace.vp_frame++) {
        float bins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME] = { 0 };
        mcpx_apu_vp_frame(d, bins);
        unsigned mixed_before = mixed_frames;
        for (unsigned i = 0; i < NUM_SAMPLES_PER_FRAME; i++) {
            g_assert_true(isfinite(bins[0][i]));
            g_assert_cmpfloat_with_epsilon(
                bins[0][i], stereo ? -bins[1][i] : bins[1][i], 1e-7f);
            if (bins[0][i] != 0) {
                mixed_frames++;
            }
            mixed_energy += (double)bins[0][i] * bins[0][i];
            if (!voice_active()) {
                /* Existing voice_process discards generated PCM after its
                 * callback clears ACTIVE. Record that boundary explicitly. */
                g_assert_cmpfloat(bins[0][i], ==, 0);
            }
            for (unsigned b = 2; b < NUM_MIXBINS; b++) {
                g_assert_cmpfloat(bins[b][i], ==, 0);
            }
        }
        if (report_trace) {
            printf("{\"kind\":\"block\",\"vp_frame\":%u,"
                   "\"generated_frames_total\":%u,\"mixed_frames\":%u,"
                   "\"cbo\":%u,\"active\":%s,\"notifier\":%u}\n",
                   trace.vp_frame, trace.generated_frames,
                   mixed_frames - mixed_before,
                   ldl_le_p(voice_ptr() + NV_PAVS_VOICE_PAR_OFFSET) & 0xffffff,
                   voice_active() ? "true" : "false", *notifier_ptr());
        }
        if (!voice_active() && ++post_completion == 3) {
            break;
        }
    }
    g_assert_false(voice_active());
    g_assert_cmpuint(trace.active_transitions, ==, 1);
    g_assert_cmpuint(trace.notify_transitions, ==, 1);
    g_assert_cmpuint(*notifier_ptr(), ==,
                     NV1BA0_NOTIFICATION_STATUS_DONE_SUCCESS);
    g_assert_cmpuint(*(notifier_ptr() - 1), ==, 1);
    g_assert_true(d->set_irq);
    g_assert_cmpuint(d->regs[NV_PAPU_ISTS] &
                         (NV_PAPU_ISTS_FEVINTSTS | NV_PAPU_ISTS_FENINTSTS),
                     ==, NV_PAPU_ISTS_FEVINTSTS | NV_PAPU_ISTS_FENINTSTS);
    g_assert_cmpuint(trace.last_cbo, ==, frames - 1);
    g_assert_cmpuint(trace.payload_frames, <=, frames);
    g_assert_cmpuint(trace.payload_frames, >=, frames - 1);
    g_assert_cmpuint(trace.callbacks, >, 0);
    g_assert_cmpuint(trace.generated_frames, ==,
                     trace.frame_at_off * NUM_SAMPLES_PER_FRAME);
    if (frames == 257 && pitch == 0) {
        /* Both converters must mix some payload for this long control. */
        g_assert_cmpuint(mixed_frames, >, 0);
    }
    if (require_finite_tail && pitch == 0) {
        /* Explicit negative control for the unchanged main path. A future
         * draining implementation needs to preserve finite source payload
         * and mix its full unity-rate duration before completing. */
        g_assert_cmpuint(trace.payload_frames, ==, frames);
        g_assert_cmpuint(mixed_frames, ==, frames);
    }
    mcpx_apu_vp_finalize(d);
    g_assert_null(d->vp.filters[voice_handle].resampler);
    g_assert_cmpint(d->vp.filters[voice_handle].resampler_channels, ==, 0);
    g_free(d);
    if (report_matrix) {
        printf("{\"kind\":\"case\",\"converter\":\"%s\",\"profile\":\"%s\","
               "\"input_frames\":%u,\"pitch\":%d,\"callbacks\":%u,"
               "\"payload_frames\":%u,\"generated_frames\":%u,"
               "\"frame_at_off\":%u,\"callback_at_off\":%u,"
               "\"active_transitions\":%u,\"notify_transitions\":%u,"
               "\"final_cbo\":%u,\"mixed_frames\":%u,"
               "\"mixed_energy\":%.17g,\"invariants\":\"PASS\"}\n",
               converter == SRC_LINEAR ? "linear" : "sinc",
               payload_names[payload], frames, pitch, trace.callbacks,
               trace.payload_frames, trace.generated_frames, trace.frame_at_off,
               trace.callback_at_off, trace.active_transitions,
               trace.notify_transitions, trace.last_cbo, mixed_frames,
               mixed_energy);
    }
}

static void test_boundaries(gconstpointer opaque)
{
    unsigned variant = GPOINTER_TO_UINT(opaque);
    converter = variant < 4 ? SRC_SINC_FASTEST : SRC_LINEAR;
    enum Payload payload = variant % 4;
    static const unsigned lengths[] = {
        1, 2, 7, 31, 32, 33, 63, 64, 65, 127, 128, 129, 255, 256, 257,
    };
    static const int pitches[] = { -8192, -4096, 0, 4096, 8192 };
    for (unsigned n = 0; n < ARRAY_SIZE(lengths); n++) {
        for (unsigned p = 0; p < ARRAY_SIZE(pitches); p++) {
            run_case(payload, lengths[n], pitches[p]);
        }
    }
}

int __wrap_main(int argc, char **argv);
int __wrap_main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--require-finite-tail")) {
        require_finite_tail = true;
        memmove(argv + 1, argv + 2, (argc - 1) * sizeof(*argv));
        argc--;
    }
    report_trace = argc == 2 && !strcmp(argv[1], "--trace");
    report_matrix = argc == 2 && !strcmp(argv[1], "--matrix");
    report_matrix |= report_trace;
    if (!report_matrix) {
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
    memory_region_init_ram(&ram, NULL, "test-resampler-state-ram", 65536,
                           &error_fatal);
    memory_region_add_subregion(get_system_memory(), ram_base, &ram);
    if (report_matrix) {
        for (unsigned v = 0; v < 8; v++) {
            test_boundaries(GUINT_TO_POINTER(v));
        }
        printf("{\"kind\":\"summary\",\"cases\":600,\"invariants\":\"PASS\","
               "\"library\":\"%s\"}\n",
               src_get_version());
        return 0;
    }
    for (unsigned v = 0; v < 8; v++) {
        char *path =
            g_strdup_printf("/mcpx-apu/resampler-state/%s/%s",
                            v < 4 ? "sinc" : "linear", payload_names[v % 4]);
        g_test_add_data_func(path, GUINT_TO_POINTER(v), test_boundaries);
        g_free(path);
    }
    return g_test_run();
}

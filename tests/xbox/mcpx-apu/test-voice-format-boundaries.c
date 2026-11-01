/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "monitor/monitor.h"
#include "hw/boards.h"
#include "system/cpus.h"
#include "hw/xbox/mcpx/apu/apu_int.h"
#include "test-voice-format-boundaries.h"

/*
 * Drive the production VP API without a timer, device frame thread, GP/EP
 * DSP program, sound stream, ROM or renderer. Throughput uses the existing
 * test-resampler-throughput fixture.
 */
static MemoryRegion ram;
static const hwaddr ram_base = 0x100000;
static const unsigned voice_count = 1;
static const unsigned first_voice = 64;

static void set_voice_field(uint8_t *voice, unsigned offset, uint32_t mask,
                            uint32_t value)
{
    uint32_t word = ldl_le_p(voice + offset);
    SET_MASK(word, mask, value);
    stl_le_p(voice + offset, word);
}

static MCPXAPUState *prepare_voice_frame(void)
{
    MCPXAPUState *d = g_new0(MCPXAPUState, 1);
    uint8_t *data = memory_region_get_ram_ptr(&ram);
    memset(data, 0, 65536);
    /* Three logical pages map to physical pages 0/2/4; holes are poison. */
    memset(data + 0x9000, 0xa5, 5 * 4096);
    for (unsigned page = 0; page < 3; page++) {
        stl_le_p(data + 0x8000 + page * 8, ram_base + 0x9000 + page * 8192);
    }
    for (unsigned i = 0; i < 8192; i++) {
        data[0x9000 + (i / 4096) * 8192 + i % 4096] = i % 2 ? 0x10 : 0;
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
                        NV_PAVS_VOICE_CFG_FMT_STEREO, 0);
        set_voice_field(voice, NV_PAVS_VOICE_CFG_FMT,
                        NV_PAVS_VOICE_CFG_FMT_SAMPLES_PER_BLOCK, 0);
        set_voice_field(voice, NV_PAVS_VOICE_CFG_FMT,
                        NV_PAVS_VOICE_CFG_FMT_SAMPLE_SIZE, 1);
        set_voice_field(voice, NV_PAVS_VOICE_CFG_FMT,
                        NV_PAVS_VOICE_CFG_FMT_CONTAINER_SIZE, 1);
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
    d->monitor.point = MCPX_APU_DEBUG_MON_GP_OR_EP;
    g_config.audio.vp.num_workers = 1;
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
    QemuSemaphore attempted;
    bool completed;
} BoundaryWriterThread;

static __thread BoundaryWriterThread *observed_writer;

int __wrap_pthread_mutex_lock(pthread_mutex_t *mutex)
{
    BoundaryWriterThread *writer = observed_writer;
    if (writer && mutex == &writer->d->lock.lock) {
        /* Observe the actual external lock call, including under LTO. */
        observed_writer = NULL;
        int result = pthread_mutex_trylock(mutex);
        g_assert_cmpint(result, ==, EBUSY);
        qemu_sem_post(&writer->attempted);
    }
    return __real_pthread_mutex_lock(mutex);
}

static uint8_t *voice_record(unsigned base, unsigned voice)
{
    uint8_t *data = memory_region_get_ram_ptr(&ram);
    return data + base - ram_base + voice * NV_PAVS_SIZE;
}

static void write_vp_method(MCPXAPUState *d, hwaddr method, uint32_t argument)
{
    vp_ops.write(d, method, argument, sizeof(argument));
}

typedef struct CallbackObservation {
    long frames;
    float pcm[NUM_SAMPLES_PER_FRAME];
    uint32_t cbo;
    unsigned ssl_index;
    unsigned ssl_seg;
    bool active;
    uint8_t notify[2];
    uint8_t notify_phase[2];
    uint32_t irq;
    bool set_irq;
} CallbackObservation;

typedef struct CallbackTrace {
    MCPXAPUState *d;
    src_callback_t callback;
    void *opaque;
    unsigned count;
    CallbackObservation observations[32];
} CallbackTrace;

static CallbackTrace *callback_trace;

static uint8_t *notifier_status(MCPXAPUState *d, unsigned ssl)
{
    uint8_t *data = memory_region_get_ram_ptr(&ram);
    return data + d->regs[NV_PAPU_FENADDR] - ram_base +
           16 * (MCPX_HW_NOTIFIER_BASE_OFFSET +
                 first_voice * MCPX_HW_NOTIFIER_COUNT +
                 MCPX_HW_NOTIFIER_SSLA_DONE + ssl) + 15;
}

static long observe_sample_callback(void *opaque, float **data)
{
    CallbackTrace *trace = opaque;
    long count = trace->callback(trace->opaque, data);
    g_assert_true(count == 0 || count == NUM_SAMPLES_PER_FRAME);
    g_assert_cmpuint(trace->count, <, ARRAY_SIZE(trace->observations));
    CallbackObservation *observation = &trace->observations[trace->count++];
    MCPXAPUState *d = trace->d;
    uint8_t *voice = voice_record(d->regs[NV_PAPU_VPVADDR], first_voice);
    observation->frames = count;
    memcpy(observation->pcm, *data, count * sizeof(float));
    observation->cbo = ldl_le_p(voice + NV_PAVS_VOICE_PAR_OFFSET) & 0xffffff;
    observation->active = ldl_le_p(voice + NV_PAVS_VOICE_PAR_STATE) &
                          NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE;
    observation->ssl_index = d->vp.ssl[first_voice].ssl_index;
    observation->ssl_seg = d->vp.ssl[first_voice].ssl_seg;
    for (unsigned ssl = 0; ssl < 2; ssl++) {
        observation->notify[ssl] = *notifier_status(d, ssl);
        observation->notify_phase[ssl] = *(notifier_status(d, ssl) - 1);
    }
    observation->irq = d->regs[NV_PAPU_ISTS];
    observation->set_irq = d->set_irq;
    return count;
}

SRC_STATE *__wrap_src_callback_new(src_callback_t callback, int type,
                                   int channels, int *error, void *opaque)
{
    /* Observe mono PCM before sinc buffering; retain the real converter. */
    if (callback_trace) {
        g_assert_cmpint(channels, ==, 1);
        callback_trace->callback = callback;
        callback_trace->opaque = opaque;
        return __real_src_callback_new(observe_sample_callback, type, channels,
                                       error, callback_trace);
    }
    return __real_src_callback_new(callback, type, channels, error, opaque);
}

static void prepare_notifications(MCPXAPUState *d)
{
    d->regs[NV_PAPU_FENADDR] = ram_base + 0x6000;
    d->regs[NV_PAPU_FETFORCE1] = NV_PAPU_FETFORCE1_SE2FE_IDLE_VOICE;
    for (unsigned ssl = 0; ssl < 2; ssl++) {
        memset(notifier_status(d, ssl) - 1, 0xa5, 2);
    }
}

static void assert_callback(const CallbackObservation *observation, float pcm,
                            unsigned cbo, unsigned ssl_index, unsigned ssl_seg,
                            bool active)
{
    g_assert_cmpint(observation->frames, ==, NUM_SAMPLES_PER_FRAME);
    for (unsigned i = 0; i < NUM_SAMPLES_PER_FRAME; i++) {
        g_assert_cmpfloat(observation->pcm[i], ==, pcm);
    }
    g_assert_cmpuint(observation->cbo, ==, cbo);
    g_assert_cmpuint(observation->ssl_index, ==, ssl_index);
    g_assert_cmpuint(observation->ssl_seg, ==, ssl_seg);
    g_assert_cmpuint(observation->active, ==, active);
}

static void assert_notified(const CallbackObservation *observation,
                            unsigned ssl)
{
    g_assert_cmpuint(observation->notify[ssl], ==,
                     NV1BA0_NOTIFICATION_STATUS_DONE_SUCCESS);
    g_assert_cmpuint(observation->notify_phase[ssl], ==, 1);
    g_assert_cmpuint(observation->irq &
                         (NV_PAPU_ISTS_FEVINTSTS | NV_PAPU_ISTS_FENINTSTS),
                     ==, NV_PAPU_ISTS_FEVINTSTS | NV_PAPU_ISTS_FENINTSTS);
    g_assert_true(observation->set_irq);
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
    uint32_t format = mono_pcm_format() | NV_PAVS_VOICE_CFG_FMT_STEREO;
    SET_MASK(format, NV_PAVS_VOICE_CFG_FMT_SAMPLES_PER_BLOCK, 1);
    return format;
}

static MCPXAPUState *prepare_boundary_voice(uint32_t format)
{
    MCPXAPUState *d = prepare_voice_frame();
    uint8_t *data = memory_region_get_ram_ptr(&ram);
    /* Mono alternates signs; stereo has positive left and negative right. */
    for (unsigned i = 0; i < 8192; i += 2) {
        stw_le_p(data + 0x9000 + (i / 4096) * 8192 + i % 4096,
                 i % 4 ? (uint16_t)-8192 : 4096);
    }
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

static void reference_continuation(FrameObservation observations[2])
{
    MCPXAPUState *d = prepare_boundary_voice(mono_pcm_format());
    observations[0] = observe_one_voice_frame(d);
    stl_le_p(voice_record(ram_base, first_voice) + NV_PAVS_VOICE_CFG_FMT,
             stereo_pcm_format());
    observations[1] = observe_one_voice_frame(d);
    g_assert_true(observations[0].active);
    g_assert_true(observations[1].active);
    g_assert_cmpuint(observations[0].cbo, >, 0);
    g_assert_cmpuint(observations[1].cbo, >, observations[0].cbo);
    /*
     * Independent oracle: retaining mono until reset must not pass by
     * producing the same incorrect result in both matched sequences.
     */
    g_assert_cmpfloat(observations[1].bins[0][NUM_SAMPLES_PER_FRAME - 1], >, 0);
    g_assert_cmpfloat(observations[1].bins[1][NUM_SAMPLES_PER_FRAME - 1], <, 0);
    destroy_voice_frame(d);
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
    observed_writer = thread;
    write_vp_method(thread->d, NV1BA0_PIO_VOICE_LOCK, 1);
    write_vp_method(thread->d, NV1BA0_PIO_SET_VOICE_CFG_FMT, thread->format);
    write_vp_method(thread->d, NV1BA0_PIO_VOICE_LOCK, 0);
    qatomic_set(&thread->completed, true);
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
    FrameObservation expected[2];
    reference_continuation(expected);
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
    qemu_sem_init(&writer->attempted, 0);
    qemu_mutex_lock(&dispatch->lock);
    qemu_thread_create(&frame_thread, "voice-boundary-frame",
                       boundary_frame_thread, frame, QEMU_THREAD_JOINABLE);
    qemu_event_wait(&frame->locked);
    qemu_thread_create(&writer_thread, "voice-boundary-writer",
                       boundary_writer_thread, writer, QEMU_THREAD_JOINABLE);
    g_assert_cmpint(qemu_sem_timedwait(&writer->attempted, 5000), ==, 0);
    g_assert_false(qatomic_read(&writer->completed));
    g_assert_cmphex(
        ldl_le_p(voice_record(ram_base, first_voice) + NV_PAVS_VOICE_CFG_FMT),
        ==, mono_pcm_format());
    qemu_mutex_unlock(&dispatch->lock);
    qemu_thread_join(&frame_thread);
    qemu_thread_join(&writer_thread);

    g_assert_true(qatomic_read(&writer->completed));
    assert_observation_equal(&frame->observation, &expected[0]);
    g_assert_cmphex(
        ldl_le_p(voice_record(ram_base, first_voice) + NV_PAVS_VOICE_CFG_FMT),
        ==, stereo_pcm_format());

    /* Continue the active voice at its existing cursor, without VP reset. */
    FrameObservation next = observe_one_voice_frame(d);
    assert_observation_equal(&next, &expected[1]);

    qemu_sem_destroy(&writer->attempted);
    qemu_event_destroy(&frame->locked);
    g_free(writer);
    g_free(frame);
    destroy_voice_frame(d);
}

static void test_direct_ram_and_table_base_boundaries(void)
{
    FrameObservation expected[2];
    reference_continuation(expected);
    MCPXAPUState *d = prepare_boundary_voice(mono_pcm_format());
    uint8_t *voice = voice_record(ram_base, first_voice);

    FrameObservation direct = observe_one_voice_frame(d);
    assert_observation_equal(&direct, &expected[0]);

    const unsigned alternate_base = ram_base + 0x4000;
    uint8_t *alternate = voice_record(alternate_base, first_voice);
    memcpy(alternate, voice, NV_PAVS_SIZE);
    stl_le_p(alternate + NV_PAVS_VOICE_CFG_FMT, stereo_pcm_format());
    qatomic_set(&d->regs[NV_PAPU_VPVADDR], alternate_base);
    FrameObservation rebased = observe_one_voice_frame(d);
    assert_observation_equal(&rebased, &expected[1]);

    destroy_voice_frame(d);
}

static void test_method_update_survives_reset(void)
{
    FrameObservation expected = reference_observation(stereo_pcm_format());
    MCPXAPUState *d = prepare_boundary_voice(mono_pcm_format());
    FrameObservation old = observe_one_voice_frame(d);
    g_assert_true(old.active);
    g_assert_cmpuint(old.cbo, >, 0);

    write_vp_method(d, NV1BA0_PIO_SET_CURRENT_VOICE, first_voice);
    write_vp_method(d, NV1BA0_PIO_VOICE_LOCK, 1);
    write_vp_method(d, NV1BA0_PIO_SET_VOICE_CFG_FMT, stereo_pcm_format());
    write_vp_method(d, NV1BA0_PIO_VOICE_LOCK, 0);
    mcpx_apu_vp_reset(d);
    d->vp.submix_headroom[0] = d->vp.submix_headroom[1] = 6;
    stl_le_p(voice_record(ram_base, first_voice) + NV_PAVS_VOICE_PAR_OFFSET,
             0xff000000);
    FrameObservation reset = observe_one_voice_frame(d);
    assert_observation_equal(&reset, &expected);

    destroy_voice_frame(d);
}

static void test_pcm_to_adpcm_continuation(void)
{
    MCPXAPUState *d = prepare_boundary_voice(mono_pcm_format());
    uint8_t *data = memory_region_get_ram_ptr(&ram);
    for (unsigned i = 0; i < 512; i++) {
        stw_le_p(data + 0x9000 + i * 2, 4096);
    }
    /* Zero index/nibbles leave every decoded sample at the -8192 predictor. */
    memset(data + 0x9400, 0, 64 * 36);
    for (unsigned block = 0; block < 64; block++) {
        stw_le_p(data + 0x9400 + block * 36, (uint16_t)-8192);
    }
    prepare_notifications(d);
    CallbackTrace trace = { .d = d };
    callback_trace = &trace;
    observe_one_voice_frame(d);
    g_assert_cmpuint(trace.count, >, 0);
    for (unsigned i = 0; i < trace.count; i++) {
        assert_callback(&trace.observations[i], 0.125f, (i + 1) * 32, 0, 0,
                         true);
    }
    unsigned before = trace.count;
    unsigned old_cbo = trace.observations[before - 1].cbo;
    uint32_t format = mono_pcm_format();
    SET_MASK(format, NV_PAVS_VOICE_CFG_FMT_CONTAINER_SIZE,
              NV_PAVS_VOICE_CFG_FMT_CONTAINER_SIZE_ADPCM);
    write_vp_method(d, NV1BA0_PIO_SET_CURRENT_VOICE, first_voice);
    write_vp_method(d, NV1BA0_PIO_VOICE_LOCK, 1);
    write_vp_method(d, NV1BA0_PIO_SET_VOICE_CFG_BUF_BASE, 0x400);
    write_vp_method(d, NV1BA0_PIO_SET_VOICE_CFG_FMT, format);
    write_vp_method(d, NV1BA0_PIO_VOICE_LOCK, 0);
    FrameObservation mixed = { 0 };
    for (unsigned i = 0; i < 4; i++) {
        mixed = observe_one_voice_frame(d);
    }
    g_assert_cmpuint(trace.count, >, before);
    for (unsigned i = before; i < trace.count; i++) {
        CallbackObservation *observation = &trace.observations[i];
        assert_callback(observation, -0.25f, old_cbo + (i - before + 1) * 32,
                         0, 0, true);
        g_assert_cmpuint(observation->notify[0], ==, 0xa5);
        g_assert_cmpuint(observation->notify[1], ==, 0xa5);
        g_assert_false(observation->set_irq);
    }
    g_assert_cmpfloat(mixed.bins[0][NUM_SAMPLES_PER_FRAME - 1], <, 0);
    g_assert_cmpfloat(mixed.bins[0][NUM_SAMPLES_PER_FRAME - 1], ==,
                      mixed.bins[1][NUM_SAMPLES_PER_FRAME - 1]);
    destroy_voice_frame(d);
    callback_trace = NULL;
}

static void advance_to_callback(CallbackTrace *trace, unsigned count)
{
    for (unsigned frame = 0; frame < 16 && trace->count < count; frame++) {
        observe_one_voice_frame(trace->d);
    }
    g_assert_cmpuint(trace->count, >=, count);
}

static void test_persistent_stream_release(void)
{
    uint32_t format = (mono_pcm_format() & ~NV_PAVS_VOICE_CFG_FMT_LOOP) |
                      NV_PAVS_VOICE_CFG_FMT_DATA_TYPE |
                      NV_PAVS_VOICE_CFG_FMT_PERSIST;
    MCPXAPUState *d = prepare_boundary_voice(format);
    prepare_notifications(d);
    uint8_t *data = memory_region_get_ram_ptr(&ram);
    for (unsigned i = 0; i < 128; i++) {
        stw_le_p(data + 0x9000 + i * 2, 4096);
        stw_le_p(data + 0x9400 + i * 2, (uint16_t)-8192);
    }
    d->regs[NV_PAPU_VPSSLADDR] = ram_base + 0x5000;
    write_vp_method(d, NV1BA0_PIO_SET_CURRENT_VOICE, first_voice);
    write_vp_method(d, NV1BA0_PIO_VOICE_LOCK, 1);
    write_vp_method(d, NV1BA0_PIO_SET_CURRENT_SSL, 0);
    for (unsigned segment = 0; segment < 3; segment++) {
        unsigned offset = segment == 2 ? 0x9400 : 0x9000 + segment * 128;
        unsigned samples = segment == 2 ? 128 : 64;
        write_vp_method(d, NV1BA0_PIO_SET_SSL_SEGMENT_OFFSET + segment * 8,
                        ram_base + offset);
        /* Mono PCM, B16 container, one sample per block. */
        write_vp_method(d, NV1BA0_PIO_SET_SSL_SEGMENT_LENGTH + segment * 8,
                        samples | (1U << 16));
    }
    write_vp_method(d, NV1BA0_PIO_SET_VOICE_SSL_A, 2);
    write_vp_method(d, NV1BA0_PIO_SET_VOICE_SSL_B, (2U << 8) | 1);
    write_vp_method(d, NV1BA0_PIO_VOICE_LOCK, 0);
    CallbackTrace trace = { .d = d };
    callback_trace = &trace;
    advance_to_callback(&trace, 4);
    g_assert_cmpuint(d->vp.ssl[first_voice].ssl_index, ==, 1);
    for (unsigned i = 0; i < 4; i++) {
        assert_callback(&trace.observations[i], 0.125f, (i + 1) * 32 % 64,
                         i == 3, i == 1 || i == 2, true);
        if (i < 3) {
            g_assert_cmpuint(trace.observations[i].notify[0], ==, 0xa5);
            g_assert_cmpuint(trace.observations[i].notify_phase[0], ==, 0xa5);
        }
        g_assert_cmpuint(trace.observations[i].notify[1], ==, 0xa5);
    }
    assert_notified(&trace.observations[3], 0);
    /* Retire consumed A between frames, so B exhausts into an empty A. */
    write_vp_method(d, NV1BA0_PIO_VOICE_LOCK, 1);
    write_vp_method(d, NV1BA0_PIO_SET_VOICE_SSL_A, 0);
    memset(notifier_status(d, 0) - 1, 0xa5, 2);
    write_vp_method(d, NV1BA0_PIO_VOICE_LOCK, 0);
    advance_to_callback(&trace, 9);
    for (unsigned i = 4; i < 8; i++) {
        assert_callback(&trace.observations[i], -0.25f, (i + 1) * 32 % 128,
                         i != 7, 0, true);
        if (i < 7) {
            g_assert_cmpuint(trace.observations[i].notify[1], ==, 0xa5);
            g_assert_cmpuint(trace.observations[i].notify_phase[1], ==, 0xa5);
        }
    }
    assert_notified(&trace.observations[7], 1);
    g_assert_cmpuint(trace.observations[7].notify[0], ==, 0xa5);
    for (unsigned i = 8; i < trace.count; i++) {
        assert_callback(&trace.observations[i], 0, 0, 0, 0, true);
        assert_notified(&trace.observations[i], 0);
    }
    unsigned before_release = trace.count;
    write_vp_method(d, NV1BA0_PIO_VOICE_LOCK, 1);
    memset(notifier_status(d, 0) - 1, 0xa5, 2);
    d->regs[NV_PAPU_ISTS] = 0;
    d->set_irq = false;
    /*
     * Keep the envelope in release long enough that only stream exhaustion
     * can turn the voice off in this bounded observation.
     */
    write_vp_method(d, NV1BA0_PIO_SET_VOICE_LFO_ENV, 0xfff);
    write_vp_method(d, NV1BA0_PIO_SET_VOICE_CFG_MISC, 0xfff);
    write_vp_method(d, NV1BA0_PIO_SET_VOICE_CFG_FMT,
                    format & ~NV_PAVS_VOICE_CFG_FMT_PERSIST);
    write_vp_method(d, NV1BA0_PIO_VOICE_RELEASE, first_voice);
    write_vp_method(d, NV1BA0_PIO_VOICE_LOCK, 0);
    uint8_t *voice = voice_record(ram_base, first_voice);
    g_assert_cmpuint(GET_MASK(ldl_le_p(voice + NV_PAVS_VOICE_PAR_STATE),
                              NV_PAVS_VOICE_PAR_STATE_EACUR),
                     ==, NV_PAVS_VOICE_PAR_STATE_EFCUR_RELEASE);
    advance_to_callback(&trace, before_release + 1);
    /* Finite EOF precedes converter drain and the final mixed frame. */
    const CallbackObservation *eof = &trace.observations[before_release];
    g_assert_cmpint(eof->frames, ==, 0);
    g_assert_true(eof->active);
    g_assert_cmpuint(eof->cbo, ==, 0);
    g_assert_cmpuint(eof->ssl_index, ==, 0);
    g_assert_cmpuint(eof->ssl_seg, ==, 0);
    g_assert_cmpuint(eof->notify[0], ==, 0xa5);
    g_assert_cmpuint(eof->notify_phase[0], ==, 0xa5);
    g_assert_cmpuint(eof->irq, ==, 0);
    g_assert_false(eof->set_irq);
    g_assert_true(d->vp.filters[first_voice].resampler_source_finished);
    for (unsigned frame = 0; frame < 16 &&
         (ldl_le_p(voice + NV_PAVS_VOICE_PAR_STATE) &
          NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE); frame++) {
        observe_one_voice_frame(d);
    }
    g_assert_cmpuint(ldl_le_p(voice + NV_PAVS_VOICE_PAR_STATE) &
                     NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE, ==, 0);
    g_assert_cmpuint(*notifier_status(d, 0), ==,
                     NV1BA0_NOTIFICATION_STATUS_DONE_SUCCESS);
    g_assert_cmpuint(*(notifier_status(d, 0) - 1), ==, 1);
    g_assert_cmpuint(d->regs[NV_PAPU_ISTS] &
                     (NV_PAPU_ISTS_FEVINTSTS | NV_PAPU_ISTS_FENINTSTS), ==,
                     NV_PAPU_ISTS_FEVINTSTS | NV_PAPU_ISTS_FENINTSTS);
    g_assert_true(d->set_irq);
    g_assert_cmpuint(GET_MASK(ldl_le_p(voice + NV_PAVS_VOICE_PAR_STATE),
                              NV_PAVS_VOICE_PAR_STATE_EACUR),
                     ==, NV_PAVS_VOICE_PAR_STATE_EFCUR_RELEASE);
    FrameObservation inactive = observe_one_voice_frame(d);
    g_assert_false(inactive.active);
    for (unsigned bin = 0; bin < 2; bin++) {
        for (unsigned i = 0; i < NUM_SAMPLES_PER_FRAME; i++) {
            g_assert_cmpfloat(inactive.bins[bin][i], ==, 0);
        }
    }
    destroy_voice_frame(d);
    callback_trace = NULL;
}

int __wrap_main(int argc, char **argv);
int __wrap_main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qemu_init_cpu_loop();
    monitor_init_globals();
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
    g_test_add_func("/mcpx-apu/voice-format/method-before-frame",
                    test_method_update_before_frame);
    g_test_add_func("/mcpx-apu/voice-format/method-waits-for-frame",
                    test_method_update_waits_for_active_frame);
    g_test_add_func("/mcpx-apu/voice-format/direct-and-table-boundaries",
                    test_direct_ram_and_table_base_boundaries);
    g_test_add_func("/mcpx-apu/voice-format/method-survives-reset",
                    test_method_update_survives_reset);
    g_test_add_func("/mcpx-apu/voice-format/pcm-to-adpcm-continuation",
                    test_pcm_to_adpcm_continuation);
    g_test_add_func("/mcpx-apu/voice-format/persistent-stream-release",
                    test_persistent_stream_release);
    return g_test_run();
}

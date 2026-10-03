/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "hw/xbox/mcpx/apu/vp/resample.h"
#include "xemu-config.h"

#include <math.h>
#include <samplerate.h>

typedef struct TestSamples {
    float *samples;
    long frames;
    long offset;
    long callback_frames;
    long callback_calls;
    int channels;
} TestSamples;

static long sample_callback(void *opaque, float **data)
{
    TestSamples *samples = opaque;

    samples->callback_calls++;
    long remaining = samples->frames - samples->offset;
    if (remaining == 0) {
        *data = NULL;
        return 0;
    }

    long frames = samples->callback_frames > 0 ?
                      MIN(remaining, samples->callback_frames) :
                      remaining;
    *data = &samples->samples[samples->offset * samples->channels];
    samples->offset += frames;
    return frames;
}

static void test_pack_mono(void)
{
    const float stereo[][2] = {
        { 0.25f, -0.5f },
        { 0.75f, -1.0f },
        { -0.125f, 1.0f },
    };
    float mono[ARRAY_SIZE(stereo)] = { 0 };

    mcpx_apu_pack_mono_samples(stereo, mono, ARRAY_SIZE(stereo));

    for (int i = 0; i < ARRAY_SIZE(stereo); i++) {
        g_assert_cmpfloat(mono[i], ==, stereo[i][0]);
    }
}

static void test_expand_mono(void)
{
    const float mono[] = { 0.25f, 0.75f, -0.125f };
    float stereo[ARRAY_SIZE(mono)][2] = { 0 };

    mcpx_apu_expand_mono_samples(mono, stereo, ARRAY_SIZE(mono));

    for (int i = 0; i < ARRAY_SIZE(mono); i++) {
        g_assert_cmpfloat(stereo[i][0], ==, mono[i]);
        g_assert_cmpfloat(stereo[i][1], ==, mono[i]);
    }
}

static void test_configured_converter_type(void)
{
    g_assert_cmpint(mcpx_apu_resampler_type(CONFIG_AUDIO_VP_RESAMPLER_SINC), ==,
                    SRC_SINC_FASTEST);
    g_assert_cmpint(mcpx_apu_resampler_type(CONFIG_AUDIO_VP_RESAMPLER_LINEAR),
                    ==, SRC_LINEAR);
    g_assert_cmpint(mcpx_apu_resampler_type(-1), ==, SRC_SINC_FASTEST);
}

static void run_mono_matches_duplicated_stereo(int converter_type)
{
    enum { INPUT_FRAMES = 256, OUTPUT_FRAMES = 96 };
    float mono_input[INPUT_FRAMES];
    float stereo_input[INPUT_FRAMES][2];
    float mono_output[OUTPUT_FRAMES];
    float stereo_output[OUTPUT_FRAMES][2];

    for (int i = 0; i < INPUT_FRAMES; i++) {
        mono_input[i] = sinf(i * 0.071f) * 0.75f + cosf(i * 0.019f) * 0.2f;
        stereo_input[i][0] = mono_input[i];
        stereo_input[i][1] = mono_input[i];
    }

    TestSamples mono_samples = {
        .samples = mono_input,
        .frames = INPUT_FRAMES,
        .channels = 1,
    };
    TestSamples stereo_samples = {
        .samples = (float *)stereo_input,
        .frames = INPUT_FRAMES,
        .channels = 2,
    };
    int mono_err;
    int stereo_err;
    SRC_STATE *mono = src_callback_new(sample_callback, converter_type, 1,
                                       &mono_err, &mono_samples);
    SRC_STATE *stereo = src_callback_new(sample_callback, converter_type, 2,
                                         &stereo_err, &stereo_samples);

    g_assert_nonnull(mono);
    g_assert_cmpint(mono_err, ==, 0);
    g_assert_nonnull(stereo);
    g_assert_cmpint(stereo_err, ==, 0);

    long mono_count = src_callback_read(mono, 0.83, OUTPUT_FRAMES, mono_output);
    long stereo_count =
        src_callback_read(stereo, 0.83, OUTPUT_FRAMES, (float *)stereo_output);

    g_assert_cmpint(mono_count, ==, OUTPUT_FRAMES);
    g_assert_cmpint(stereo_count, ==, OUTPUT_FRAMES);
    for (int i = 0; i < OUTPUT_FRAMES; i++) {
        g_assert_cmpfloat_with_epsilon(mono_output[i], stereo_output[i][0],
                                       1e-7f);
        g_assert_cmpfloat_with_epsilon(stereo_output[i][0], stereo_output[i][1],
                                       1e-7f);
    }

    src_delete(mono);
    src_delete(stereo);
}

static void test_mono_matches_duplicated_stereo(void)
{
    static const int converter_types[] = { SRC_SINC_FASTEST, SRC_LINEAR };

    for (int i = 0; i < ARRAY_SIZE(converter_types); i++) {
        run_mono_matches_duplicated_stereo(converter_types[i]);
    }
}

static void run_streaming_equivalence(int converter_type, long callback_frames)
{
    enum {
        INPUT_FRAMES = 2048,
        OUTPUT_FRAMES = 32,
        OUTPUT_BLOCKS = 16,
    };
    static const double rates[] = { 0.55, 0.83, 1.0, 1.19, 1.71 };
    float mono_input[INPUT_FRAMES];
    float stereo_input[INPUT_FRAMES][2];
    float mono_output[OUTPUT_FRAMES];
    float stereo_output[OUTPUT_FRAMES][2];

    for (int i = 0; i < INPUT_FRAMES; i++) {
        mono_input[i] = sinf(i * 0.071f) * 0.75f + cosf(i * 0.019f) * 0.2f;
        stereo_input[i][0] = mono_input[i];
        stereo_input[i][1] = mono_input[i];
    }

    TestSamples mono_samples = {
        .samples = mono_input,
        .frames = INPUT_FRAMES,
        .callback_frames = callback_frames,
        .channels = 1,
    };
    TestSamples stereo_samples = {
        .samples = (float *)stereo_input,
        .frames = INPUT_FRAMES,
        .callback_frames = callback_frames,
        .channels = 2,
    };
    int mono_err;
    int stereo_err;
    SRC_STATE *mono = src_callback_new(sample_callback, converter_type, 1,
                                       &mono_err, &mono_samples);
    SRC_STATE *stereo = src_callback_new(sample_callback, converter_type, 2,
                                         &stereo_err, &stereo_samples);

    g_assert_nonnull(mono);
    g_assert_cmpint(mono_err, ==, 0);
    g_assert_nonnull(stereo);
    g_assert_cmpint(stereo_err, ==, 0);

    for (int block = 0; block < OUTPUT_BLOCKS; block++) {
        double rate = rates[block % ARRAY_SIZE(rates)];
        long mono_count =
            src_callback_read(mono, rate, OUTPUT_FRAMES, mono_output);
        long stereo_count = src_callback_read(stereo, rate, OUTPUT_FRAMES,
                                              (float *)stereo_output);

        g_assert_cmpint(mono_count, ==, stereo_count);
        g_assert_cmpint(mono_count, ==, OUTPUT_FRAMES);
        g_assert_cmpint(mono_samples.offset, ==, stereo_samples.offset);
        g_assert_cmpint(mono_samples.callback_calls, ==,
                        stereo_samples.callback_calls);
        for (int i = 0; i < mono_count; i++) {
            g_assert_cmpfloat_with_epsilon(mono_output[i], stereo_output[i][0],
                                           1e-7f);
            g_assert_cmpfloat_with_epsilon(stereo_output[i][0],
                                           stereo_output[i][1], 1e-7f);
        }
    }

    src_delete(mono);
    src_delete(stereo);
}

static void test_streaming_mono_matches_duplicated_stereo(void)
{
    static const int converter_types[] = { SRC_SINC_FASTEST, SRC_LINEAR };
    static const long callback_frames[] = { 1, 7, 17, 31, 32, 33, 64 };

    for (int converter = 0; converter < ARRAY_SIZE(converter_types);
         converter++) {
        for (int chunk = 0; chunk < ARRAY_SIZE(callback_frames); chunk++) {
            /*
             * libsamplerate 0.2.2's linear converter reads before its input
             * buffer when a callback returns exactly one frame (upstream
             * issue #208 / PR #209). The production voice callback always
             * returns NUM_SAMPLES_PER_FRAME (32), padding with silence when
             * necessary, so that broken third-party schedule is unreachable.
             */
            if (converter_types[converter] == SRC_LINEAR &&
                callback_frames[chunk] == 1) {
                continue;
            }
            run_streaming_equivalence(converter_types[converter],
                                      callback_frames[chunk]);
        }
    }
}

typedef struct ProductionInput {
    float samples[32][2];
    int available;
    int offset;
    int fetches;
    int empty_result;
    bool finite;
} ProductionInput;

typedef struct ProductionAdapter {
    ProductionInput input;
    float stereo[32][2];
    float mono[32];
    int channels;
    int callbacks;
    bool source_finished;
} ProductionAdapter;

typedef struct ObservedSource {
    uint64_t cursor;
    uint64_t fetched;
    uint64_t callbacks;
    uint32_t loop_start;
    uint32_t loop_end;
    uint32_t loops;
    uint32_t notifications;
    int channels;
    bool looping;
    bool active;
} ObservedSource;

typedef struct ObservedAdapter {
    ObservedSource source;
    float stereo[32][2];
    float mono[32];
    bool source_finished;
} ObservedAdapter;

static MCPXAPUResamplerFetchResult fetch_observed_source(
    void *opaque, float samples[][2], int requested)
{
    ObservedSource *source = opaque;
    int count = 0;

    source->callbacks++;
    while (count < requested && source->active) {
        if (source->cursor == source->loop_end) {
            source->notifications++;
            if (!source->looping) {
                source->active = false;
                break;
            }
            source->cursor = source->loop_start;
            source->loops++;
        }

        float phase = 2.0f * (float)M_PI * source->cursor / 48.0f;
        samples[count][0] = sinf(phase);
        samples[count][1] = source->channels == 1 ?
                                samples[count][0] :
                                cosf(phase + 0.35f);
        source->cursor++;
        source->fetched++;
        count++;
    }

    return (MCPXAPUResamplerFetchResult) {
        .frames = count > 0 ? count : -1,
        .end_of_input = !source->active && !source->looping,
    };
}

static long observed_adapter_callback(void *opaque, float **data)
{
    ObservedAdapter *adapter = opaque;

    if (adapter->source_finished) {
        *data = adapter->source.channels == 1 ?
                    adapter->mono : (float *)adapter->stereo;
        return 0;
    }

    bool end_of_input;
    long frames = mcpx_apu_resampler_fill_input_block(
        adapter->source.channels, ARRAY_SIZE(adapter->stereo),
        fetch_observed_source, &adapter->source, adapter->stereo, adapter->mono,
        data, &end_of_input);
    adapter->source_finished = end_of_input;

    return frames;
}

static MCPXAPUResamplerFetchResult fetch_production_input(
    void *opaque, float samples[][2], int requested)
{
    ProductionInput *input = opaque;

    input->fetches++;
    int count = MIN(requested, input->available - input->offset);
    if (count <= 0) {
        return (MCPXAPUResamplerFetchResult) {
            .frames = input->empty_result,
            .end_of_input = input->finite,
        };
    }
    memcpy(samples, &input->samples[input->offset],
           count * sizeof(input->samples[0]));
    input->offset += count;
    return (MCPXAPUResamplerFetchResult) {
        .frames = count,
        .end_of_input = input->finite && input->offset == input->available,
    };
}

static long production_adapter_callback(void *opaque, float **data)
{
    ProductionAdapter *adapter = opaque;

    adapter->callbacks++;
    if (adapter->source_finished) {
        *data = adapter->channels == 1 ?
                    adapter->mono : (float *)adapter->stereo;
        return 0;
    }

    bool end_of_input;
    long frames = mcpx_apu_resampler_fill_input_block(
        adapter->channels, ARRAY_SIZE(adapter->stereo), fetch_production_input,
        &adapter->input, adapter->stereo, adapter->mono, data, &end_of_input);
    adapter->source_finished = end_of_input;

    return frames;
}

static void test_production_input_pads_short_tail(void)
{
    enum { FRAMES = 32 };
    ProductionInput input = {
        .samples = { { 0.25f, -0.5f } },
        .available = 1,
        .empty_result = -1,
    };
    float stereo[FRAMES][2];
    float mono[FRAMES];
    float *data = NULL;
    bool end_of_input;

    g_assert_cmpint(
        mcpx_apu_resampler_fill_input_block(2, FRAMES, fetch_production_input,
                                            &input, stereo, mono, &data,
                                            &end_of_input),
        ==, FRAMES);
    g_assert_false(end_of_input);
    g_assert_true(data == (float *)stereo);
    g_assert_cmpint(input.offset, ==, 1);
    g_assert_cmpint(input.fetches, ==, 2);
    g_assert_cmpfloat(stereo[0][0], ==, 0.25f);
    g_assert_cmpfloat(stereo[0][1], ==, -0.5f);
    for (int i = 1; i < FRAMES; i++) {
        g_assert_cmpfloat(stereo[i][0], ==, 0.0f);
        g_assert_cmpfloat(stereo[i][1], ==, 0.0f);
    }
}

static void test_production_input_preserves_finite_tail(void)
{
    enum { FRAMES = 32 };
    ProductionInput input = {
        .samples = { { 0.25f, -0.5f } },
        .available = 1,
        .empty_result = -1,
        .finite = true,
    };
    float stereo[FRAMES][2] = { 0 };
    float mono[FRAMES] = { 0 };
    float *data = NULL;
    bool end_of_input;

    g_assert_cmpint(
        mcpx_apu_resampler_fill_input_block(2, FRAMES, fetch_production_input,
                                            &input, stereo, mono, &data,
                                            &end_of_input),
        ==, 1);
    g_assert_true(end_of_input);
    g_assert_true(data == (float *)stereo);
    g_assert_cmpint(input.offset, ==, 1);
    g_assert_cmpint(input.fetches, ==, 1);
    g_assert_cmpfloat(stereo[0][0], ==, 0.25f);
    g_assert_cmpfloat(stereo[0][1], ==, -0.5f);
}

static void test_production_input_recovers_after_starvation(void)
{
    enum { FRAMES = 32 };
    ProductionInput input = { 0 };
    float stereo[FRAMES][2];
    float mono[FRAMES];
    float *data = NULL;
    bool end_of_input;

    g_assert_cmpint(
        mcpx_apu_resampler_fill_input_block(1, FRAMES, fetch_production_input,
                                            &input, stereo, mono, &data,
                                            &end_of_input),
        ==, FRAMES);
    g_assert_false(end_of_input);
    g_assert_true(data == mono);
    g_assert_cmpint(input.fetches, ==, 1);
    for (int i = 0; i < FRAMES; i++) {
        g_assert_cmpfloat(mono[i], ==, 0.0f);
    }

    input.samples[0][0] = 0.75f;
    input.samples[0][1] = -0.25f;
    input.available = 1;
    g_assert_cmpint(
        mcpx_apu_resampler_fill_input_block(1, FRAMES, fetch_production_input,
                                            &input, stereo, mono, &data,
                                            &end_of_input),
        ==, FRAMES);
    g_assert_false(end_of_input);
    g_assert_true(data == mono);
    g_assert_cmpint(input.fetches, ==, 3);
    g_assert_cmpfloat(mono[0], ==, 0.75f);
    for (int i = 1; i < FRAMES; i++) {
        g_assert_cmpfloat(mono[i], ==, 0.0f);
    }
}

static void run_production_adapter_short_tail(int converter_type, int channels)
{
    enum { OUTPUT_FRAMES = 32, AVAILABLE_FRAMES = 17 };
    ProductionAdapter adapter = {
        .input = {
            .available = AVAILABLE_FRAMES,
            .empty_result = -1,
        },
        .channels = channels,
    };
    float output[OUTPUT_FRAMES * 2] = { 0 };

    for (int frame = 0; frame < AVAILABLE_FRAMES; frame++) {
        adapter.input.samples[frame][0] = 0.75f * sinf(frame * 0.19f + 0.2f);
        adapter.input.samples[frame][1] = channels == 1 ?
                                              adapter.input.samples[frame][0] :
                                              0.6f * cosf(frame * 0.13f + 0.1f);
    }

    int error;
    SRC_STATE *resampler =
        src_callback_new(production_adapter_callback, converter_type, channels,
                         &error, &adapter);
    g_assert_nonnull(resampler);
    g_assert_cmpint(error, ==, 0);

    long count = src_callback_read(resampler, 1.0, OUTPUT_FRAMES, output);
    g_assert_cmpint(count, ==, OUTPUT_FRAMES);
    g_assert_cmpint(adapter.input.offset, ==, AVAILABLE_FRAMES);
    g_assert_cmpint(adapter.callbacks, >, 0);
    for (int i = 0; i < OUTPUT_FRAMES * channels; i++) {
        g_assert_true(isfinite(output[i]));
    }

    src_delete(resampler);
}

static void test_production_adapter_short_tail(void)
{
    static const int converter_types[] = { SRC_SINC_FASTEST, SRC_LINEAR };

    for (int converter = 0; converter < ARRAY_SIZE(converter_types);
         converter++) {
        for (int channels = 1; channels <= 2; channels++) {
            run_production_adapter_short_tail(converter_types[converter],
                                              channels);
        }
    }
}

static void run_production_adapter_starvation_recovery(int converter_type,
                                                       int channels)
{
    enum { OUTPUT_FRAMES = 32, RECOVERY_FRAMES = 32 };
    ProductionAdapter adapter = {
        .input = { .empty_result = 0 },
        .channels = channels,
    };
    float output[OUTPUT_FRAMES * 2] = { 0 };

    int error;
    SRC_STATE *resampler =
        src_callback_new(production_adapter_callback, converter_type, channels,
                         &error, &adapter);
    g_assert_nonnull(resampler);
    g_assert_cmpint(error, ==, 0);
    g_assert_cmpint(src_callback_read(resampler, 1.0, OUTPUT_FRAMES, output),
                    ==, OUTPUT_FRAMES);
    g_assert_cmpint(adapter.input.offset, ==, 0);

    for (int frame = 0; frame < RECOVERY_FRAMES; frame++) {
        adapter.input.samples[frame][0] = 0.5f;
        adapter.input.samples[frame][1] = channels == 1 ? 0.5f : -0.25f;
    }
    adapter.input.available = RECOVERY_FRAMES;

    bool fetched = false;
    bool audible = false;
    for (int block = 0; block < 6; block++) {
        memset(output, 0, sizeof(output));
        g_assert_cmpint(
            src_callback_read(resampler, 1.0, OUTPUT_FRAMES, output), ==,
            OUTPUT_FRAMES);
        fetched |= adapter.input.offset == RECOVERY_FRAMES;
        for (int i = 0; i < OUTPUT_FRAMES * channels; i++) {
            g_assert_true(isfinite(output[i]));
            audible |= fabsf(output[i]) > 0.01f;
        }
    }
    g_assert_true(fetched);
    g_assert_true(audible);

    src_delete(resampler);
}

static void test_production_adapter_starvation_recovery(void)
{
    static const int converter_types[] = { SRC_SINC_FASTEST, SRC_LINEAR };

    for (int converter = 0; converter < ARRAY_SIZE(converter_types);
         converter++) {
        for (int channels = 1; channels <= 2; channels++) {
            run_production_adapter_starvation_recovery(
                converter_types[converter], channels);
        }
    }
}

static void run_channel_change_at_stream_reset(int converter_type)
{
    enum { INPUT_FRAMES = 256, OUTPUT_FRAMES = 32 };
    float mono_input[INPUT_FRAMES];
    float stereo_input[INPUT_FRAMES][2];
    float recreated_output[OUTPUT_FRAMES][2];
    float fresh_output[OUTPUT_FRAMES][2];

    for (int i = 0; i < INPUT_FRAMES; i++) {
        mono_input[i] = sinf(i * 0.071f) * 0.75f;
        stereo_input[i][0] = sinf(i * 0.037f) * 0.5f;
        stereo_input[i][1] = cosf(i * 0.053f) * 0.4f;
    }

    TestSamples mono_samples = {
        .samples = mono_input,
        .frames = INPUT_FRAMES,
        .callback_frames = 17,
        .channels = 1,
    };
    int err;
    SRC_STATE *resampler = src_callback_new(sample_callback, converter_type, 1,
                                            &err, &mono_samples);
    g_assert_nonnull(resampler);
    g_assert_cmpint(err, ==, 0);

    float warm_output[OUTPUT_FRAMES];
    g_assert_cmpint(
        src_callback_read(resampler, 0.83, OUTPUT_FRAMES, warm_output), ==,
        OUTPUT_FRAMES);
    g_assert_cmpint(mono_samples.offset, >, 0);
    src_delete(resampler);

    TestSamples recreated_samples = {
        .samples = (float *)stereo_input,
        .frames = INPUT_FRAMES,
        .callback_frames = 17,
        .channels = 2,
    };
    TestSamples fresh_samples = recreated_samples;
    resampler = src_callback_new(sample_callback, converter_type, 2, &err,
                                 &recreated_samples);
    g_assert_nonnull(resampler);
    g_assert_cmpint(err, ==, 0);
    SRC_STATE *fresh = src_callback_new(sample_callback, converter_type, 2,
                                        &err, &fresh_samples);
    g_assert_nonnull(fresh);
    g_assert_cmpint(err, ==, 0);

    long recreated_count = src_callback_read(resampler, 1.19, OUTPUT_FRAMES,
                                             (float *)recreated_output);
    long fresh_count =
        src_callback_read(fresh, 1.19, OUTPUT_FRAMES, (float *)fresh_output);

    g_assert_cmpint(recreated_count, ==, fresh_count);
    g_assert_cmpint(recreated_count, ==, OUTPUT_FRAMES);
    g_assert_cmpint(recreated_samples.offset, ==, fresh_samples.offset);
    g_assert_cmpint(recreated_samples.callback_calls, ==,
                    fresh_samples.callback_calls);
    for (int i = 0; i < recreated_count; i++) {
        g_assert_cmpfloat_with_epsilon(recreated_output[i][0],
                                       fresh_output[i][0], 0.0000001f);
        g_assert_cmpfloat_with_epsilon(recreated_output[i][1],
                                       fresh_output[i][1], 0.0000001f);
    }

    src_delete(resampler);
    src_delete(fresh);
}

static void test_channel_change_at_stream_reset(void)
{
    static const int converter_types[] = { SRC_SINC_FASTEST, SRC_LINEAR };

    for (int i = 0; i < ARRAY_SIZE(converter_types); i++) {
        run_channel_change_at_stream_reset(converter_types[i]);
    }
}

static void run_full_reset_discards_resampler_history(int converter_type)
{
    enum { INPUT_FRAMES = 512, OUTPUT_FRAMES = 32 };
    float signal_a[INPUT_FRAMES * 2];
    float signal_b[INPUT_FRAMES * 2];
    float reset_output[OUTPUT_FRAMES * 2];
    float fresh_output[OUTPUT_FRAMES * 2];

    for (int channels = 1; channels <= 2; channels++) {
        for (int frame = 0; frame < INPUT_FRAMES; frame++) {
            for (int channel = 0; channel < channels; channel++) {
                signal_a[frame * channels + channel] =
                    0.75f + 0.2f * sinf(frame * 0.071f + channel);
                signal_b[frame * channels + channel] =
                    -0.75f + 0.2f * cosf(frame * 0.053f + channel);
            }
        }

        TestSamples old_samples = {
            .samples = signal_a,
            .frames = INPUT_FRAMES,
            .callback_frames = 17,
            .channels = channels,
        };
        int err;
        SRC_STATE *resampler = src_callback_new(sample_callback, converter_type,
                                                channels, &err, &old_samples);
        int resampler_channels = channels;
        g_assert_nonnull(resampler);
        g_assert_cmpint(err, ==, 0);

        float warm_output[OUTPUT_FRAMES * 2];
        g_assert_cmpint(
            src_callback_read(resampler, 0.55, OUTPUT_FRAMES, warm_output), ==,
            OUTPUT_FRAMES);
        g_assert_cmpint(old_samples.offset, >, 0);

        mcpx_apu_resampler_destroy(&resampler, &resampler_channels);
        g_assert_null(resampler);
        g_assert_cmpint(resampler_channels, ==, 0);

        TestSamples reset_samples = {
            .samples = signal_b,
            .frames = INPUT_FRAMES,
            .callback_frames = 17,
            .channels = channels,
        };
        TestSamples fresh_samples = reset_samples;
        resampler = src_callback_new(sample_callback, converter_type, channels,
                                     &err, &reset_samples);
        resampler_channels = channels;
        g_assert_nonnull(resampler);
        g_assert_cmpint(err, ==, 0);
        SRC_STATE *fresh = src_callback_new(sample_callback, converter_type,
                                            channels, &err, &fresh_samples);
        g_assert_nonnull(fresh);
        g_assert_cmpint(err, ==, 0);

        long reset_count =
            src_callback_read(resampler, 0.83, OUTPUT_FRAMES, reset_output);
        long fresh_count =
            src_callback_read(fresh, 0.83, OUTPUT_FRAMES, fresh_output);

        g_assert_cmpint(reset_count, ==, fresh_count);
        g_assert_cmpint(reset_count, ==, OUTPUT_FRAMES);
        g_assert_cmpint(reset_samples.offset, ==, fresh_samples.offset);
        g_assert_cmpint(reset_samples.callback_calls, ==,
                        fresh_samples.callback_calls);
        for (int i = 0; i < reset_count * channels; i++) {
            g_assert_cmpfloat_with_epsilon(reset_output[i], fresh_output[i],
                                           0.0000001f);
        }

        mcpx_apu_resampler_destroy(&resampler, &resampler_channels);
        src_delete(fresh);
    }

    SRC_STATE *resampler = NULL;
    int resampler_channels = 2;
    mcpx_apu_resampler_destroy(&resampler, &resampler_channels);
    g_assert_cmpint(resampler_channels, ==, 0);
}

static void test_full_reset_discards_resampler_history(void)
{
    static const int converter_types[] = { SRC_SINC_FASTEST, SRC_LINEAR };

    for (int i = 0; i < ARRAY_SIZE(converter_types); i++) {
        run_full_reset_discards_resampler_history(converter_types[i]);
    }
}

static void run_deterministic_pitch_sweep(int converter_type, int channels)
{
    enum {
        OUTPUT_FRAMES = 32,
        WARMUP_BLOCKS = 8,
        MEASURE_BLOCKS = 32,
    };
    static const double rates[] = { 0.55, 0.83, 1.0, 1.19, 1.71 };

    for (int rate_index = 0; rate_index < ARRAY_SIZE(rates); rate_index++) {
        ObservedAdapter adapter = {
            .source = {
                .loop_end = UINT32_MAX,
                .channels = channels,
                .active = true,
            },
        };
        float output[OUTPUT_FRAMES * 2];
        int error;
        SRC_STATE *resampler = src_callback_new(
            observed_adapter_callback, converter_type, channels, &error,
            &adapter);

        g_assert_nonnull(resampler);
        g_assert_cmpint(error, ==, 0);

        float previous = 0.0f;
        bool have_previous = false;
        int positive_crossings = 0;
        uint64_t previous_fetched = 0;
        for (int block = 0; block < WARMUP_BLOCKS + MEASURE_BLOCKS;
             block++) {
            memset(output, 0, sizeof(output));
            long generated = src_callback_read(
                resampler, rates[rate_index], OUTPUT_FRAMES, output);

            g_assert_cmpint(generated, ==, OUTPUT_FRAMES);
            g_assert_cmpuint(adapter.source.fetched, >=, previous_fetched);
            previous_fetched = adapter.source.fetched;
            for (int frame = 0; frame < OUTPUT_FRAMES; frame++) {
                float left = output[frame * channels];
                g_assert_true(isfinite(left));
                if (channels == 2) {
                    g_assert_true(isfinite(output[frame * channels + 1]));
                }
                if (block >= WARMUP_BLOCKS) {
                    if (have_previous && previous <= 0.0f && left > 0.0f) {
                        positive_crossings++;
                    }
                    previous = left;
                    have_previous = true;
                }
            }
        }

        double expected_crossings =
            (MEASURE_BLOCKS * OUTPUT_FRAMES) / (48.0 * rates[rate_index]);
        g_assert_cmpfloat(fabs(positive_crossings - expected_crossings), <,
                          1.0);
        g_assert_cmpuint(adapter.source.callbacks, >, 0);
        g_assert_cmpuint(adapter.source.notifications, ==, 0);
        g_assert_true(adapter.source.active);
        src_delete(resampler);
    }
}

static void test_deterministic_pitch_sweep(void)
{
    static const int converter_types[] = { SRC_SINC_FASTEST, SRC_LINEAR };

    for (int converter = 0; converter < ARRAY_SIZE(converter_types);
         converter++) {
        for (int channels = 1; channels <= 2; channels++) {
            run_deterministic_pitch_sweep(converter_types[converter],
                                          channels);
        }
    }
}

static void run_loop_transition(int converter_type, int channels)
{
    enum {
        OUTPUT_FRAMES = 32,
        OUTPUT_BLOCKS = 24,
    };
    ObservedAdapter adapter = {
        .source = {
            .cursor = 11,
            .loop_start = 11,
            .loop_end = 59,
            .channels = channels,
            .looping = true,
            .active = true,
        },
    };
    float output[OUTPUT_FRAMES * 2];
    int error;
    SRC_STATE *resampler = src_callback_new(observed_adapter_callback,
                                             converter_type, channels, &error,
                                             &adapter);

    g_assert_nonnull(resampler);
    g_assert_cmpint(error, ==, 0);

    float previous[2] = { 0 };
    bool have_previous = false;
    float max_step = 0.0f;
    for (int block = 0; block < OUTPUT_BLOCKS; block++) {
        memset(output, 0, sizeof(output));
        g_assert_cmpint(src_callback_read(resampler, 1.0, OUTPUT_FRAMES,
                                         output),
                        ==, OUTPUT_FRAMES);
        for (int frame = 0; frame < OUTPUT_FRAMES; frame++) {
            for (int channel = 0; channel < channels; channel++) {
                float value = output[frame * channels + channel];
                g_assert_true(isfinite(value));
                if (have_previous) {
                    max_step = MAX(max_step,
                                   fabsf(value - previous[channel]));
                }
                previous[channel] = value;
            }
            have_previous = true;
        }
    }

    g_assert_cmpuint(adapter.source.loops, >, 0);
    g_assert_cmpuint(adapter.source.notifications, ==, adapter.source.loops);
    g_assert_true(adapter.source.active);
    g_assert_cmpfloat(max_step, <, 0.25f);
    src_delete(resampler);
}

static void test_loop_transition(void)
{
    static const int converter_types[] = { SRC_SINC_FASTEST, SRC_LINEAR };

    for (int converter = 0; converter < ARRAY_SIZE(converter_types);
         converter++) {
        for (int channels = 1; channels <= 2; channels++) {
            run_loop_transition(converter_types[converter], channels);
        }
    }
}

static void run_finite_source_drain(int converter_type, int channels,
                                    double rate)
{
    enum {
        INPUT_FRAMES = 45,
        OUTPUT_FRAMES = 32,
        MAX_OUTPUT_BLOCKS = 8,
    };
    ObservedAdapter adapter = {
        .source = {
            .loop_end = INPUT_FRAMES,
            .channels = channels,
            .active = true,
        },
    };
    float output[OUTPUT_FRAMES * 2];
    uint64_t generated_total = 0;
    uint64_t mixed_total = 0;
    uint32_t completion_notifications = 0;
    bool voice_active = true;
    bool mixed_after_source_end = false;
    int error;
    SRC_STATE *resampler = src_callback_new(observed_adapter_callback,
                                             converter_type, channels, &error,
                                             &adapter);

    g_assert_nonnull(resampler);
    g_assert_cmpint(error, ==, 0);

    for (int block = 0; block < MAX_OUTPUT_BLOCKS && voice_active; block++) {
        memset(output, 0, sizeof(output));
        long generated =
            src_callback_read(resampler, rate, OUTPUT_FRAMES, output);

        g_assert_cmpint(generated, >=, 0);
        g_assert_cmpint(generated, <=, OUTPUT_FRAMES);
        generated_total += generated;
        mixed_total += generated;
        if (!adapter.source.active && generated > 0) {
            mixed_after_source_end = true;
        }
        if (generated == 0 && adapter.source_finished) {
            voice_active = false;
            completion_notifications++;
        }
    }

    long expected = lround(INPUT_FRAMES * rate);
    g_assert_false(voice_active);
    g_assert_true(adapter.source_finished);
    g_assert_true(mixed_after_source_end);
    g_assert_cmpuint(adapter.source.fetched, ==, INPUT_FRAMES);
    g_assert_cmpuint(adapter.source.notifications, ==, 1);
    g_assert_cmpuint(completion_notifications, ==, 1);
    g_assert_cmpuint(mixed_total, ==, generated_total);
    g_assert_cmpint(llabs((long long)generated_total - expected), <=, 1);
    src_delete(resampler);
}

static void test_finite_source_drain(void)
{
    static const int converter_types[] = { SRC_SINC_FASTEST, SRC_LINEAR };
    static const double rates[] = { 0.55, 0.83, 1.0, 1.19, 1.71 };

    for (int converter = 0; converter < ARRAY_SIZE(converter_types);
         converter++) {
        for (int channels = 1; channels <= 2; channels++) {
            for (int rate = 0; rate < ARRAY_SIZE(rates); rate++) {
                run_finite_source_drain(converter_types[converter], channels,
                                        rates[rate]);
            }
        }
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/mcpx/apu/resampler/pack-mono", test_pack_mono);
    g_test_add_func("/mcpx/apu/resampler/expand-mono", test_expand_mono);
    g_test_add_func("/mcpx/apu/resampler/configured-converter-type",
                    test_configured_converter_type);
    g_test_add_func("/mcpx/apu/resampler/converter-equivalence",
                    test_mono_matches_duplicated_stereo);
    g_test_add_func("/mcpx/apu/resampler/streaming-converter-equivalence",
                    test_streaming_mono_matches_duplicated_stereo);
    g_test_add_func("/mcpx/apu/resampler/production-input-short-tail",
                    test_production_input_pads_short_tail);
    g_test_add_func("/mcpx/apu/resampler/production-input-finite-tail",
                    test_production_input_preserves_finite_tail);
    g_test_add_func("/mcpx/apu/resampler/production-input-starvation-recovery",
                    test_production_input_recovers_after_starvation);
    g_test_add_func("/mcpx/apu/resampler/production-adapter-short-tail",
                    test_production_adapter_short_tail);
    g_test_add_func(
        "/mcpx/apu/resampler/production-adapter-starvation-recovery",
        test_production_adapter_starvation_recovery);
    g_test_add_func("/mcpx/apu/resampler/channel-change-at-stream-reset",
                    test_channel_change_at_stream_reset);
    g_test_add_func("/mcpx/apu/resampler/full-reset-discards-history",
                    test_full_reset_discards_resampler_history);
    g_test_add_func("/mcpx/apu/resampler/production-adapter-pitch-sweep",
                    test_deterministic_pitch_sweep);
    g_test_add_func("/mcpx/apu/resampler/production-adapter-loop-transition",
                    test_loop_transition);
    g_test_add_func("/mcpx/apu/resampler/production-adapter-finite-drain",
                    test_finite_source_drain);
    return g_test_run();
}

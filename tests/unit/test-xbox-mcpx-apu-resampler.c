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
    g_assert_cmpint(
        mcpx_apu_resampler_type(CONFIG_AUDIO_VP_RESAMPLER_SINC), ==,
        SRC_SINC_FASTEST);
    g_assert_cmpint(
        mcpx_apu_resampler_type(CONFIG_AUDIO_VP_RESAMPLER_LINEAR), ==,
        SRC_LINEAR);
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
    long stereo_count = src_callback_read(stereo, 0.83, OUTPUT_FRAMES,
                                          (float *)stereo_output);

    g_assert_cmpint(mono_count, ==, OUTPUT_FRAMES);
    g_assert_cmpint(stereo_count, ==, OUTPUT_FRAMES);
    for (int i = 0; i < OUTPUT_FRAMES; i++) {
        g_assert_cmpfloat_with_epsilon(mono_output[i], stereo_output[i][0],
                                       1e-7f);
        g_assert_cmpfloat_with_epsilon(stereo_output[i][0],
                                       stereo_output[i][1], 1e-7f);
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

static void run_streaming_equivalence(int converter_type,
                                      long callback_frames)
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
        mono_input[i] = sinf(i * 0.071f) * 0.75f +
                        cosf(i * 0.019f) * 0.2f;
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
        long mono_count = src_callback_read(mono, rate, OUTPUT_FRAMES,
                                            mono_output);
        long stereo_count = src_callback_read(stereo, rate, OUTPUT_FRAMES,
                                              (float *)stereo_output);

        g_assert_cmpint(mono_count, ==, stereo_count);
        g_assert_cmpint(mono_count, ==, OUTPUT_FRAMES);
        g_assert_cmpint(mono_samples.offset, ==, stereo_samples.offset);
        g_assert_cmpint(mono_samples.callback_calls, ==,
                        stereo_samples.callback_calls);
        for (int i = 0; i < mono_count; i++) {
            g_assert_cmpfloat_with_epsilon(mono_output[i],
                                           stereo_output[i][0], 1e-7f);
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
            run_streaming_equivalence(converter_types[converter],
                                      callback_frames[chunk]);
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
    SRC_STATE *resampler = src_callback_new(
        sample_callback, converter_type, 1, &err, &mono_samples);
    g_assert_nonnull(resampler);
    g_assert_cmpint(err, ==, 0);

    float warm_output[OUTPUT_FRAMES];
    g_assert_cmpint(src_callback_read(resampler, 0.83, OUTPUT_FRAMES,
                                     warm_output),
                    ==, OUTPUT_FRAMES);
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

    long recreated_count = src_callback_read(
        resampler, 1.19, OUTPUT_FRAMES, (float *)recreated_output);
    long fresh_count = src_callback_read(fresh, 1.19, OUTPUT_FRAMES,
                                         (float *)fresh_output);

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
    g_test_add_func("/mcpx/apu/resampler/channel-change-at-stream-reset",
                    test_channel_change_at_stream_reset);
    return g_test_run();
}

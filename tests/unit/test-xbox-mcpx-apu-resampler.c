/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "hw/xbox/mcpx/apu/vp/resample.h"

#include <samplerate.h>

typedef struct TestSamples {
    float *samples;
    long frames;
    long offset;
    long callback_frames;
    int channels;
} TestSamples;

static long sample_callback(void *opaque, float **data)
{
    TestSamples *samples = opaque;

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

static void test_sinc_mono_matches_duplicated_stereo(void)
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
    SRC_STATE *mono = src_callback_new(sample_callback, SRC_SINC_FASTEST, 1,
                                       &mono_err, &mono_samples);
    SRC_STATE *stereo = src_callback_new(sample_callback, SRC_SINC_FASTEST, 2,
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

static void test_sinc_streaming_mono_matches_duplicated_stereo(void)
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
        .callback_frames = 17,
        .channels = 1,
    };
    TestSamples stereo_samples = {
        .samples = (float *)stereo_input,
        .frames = INPUT_FRAMES,
        .callback_frames = 17,
        .channels = 2,
    };
    int mono_err;
    int stereo_err;
    SRC_STATE *mono = src_callback_new(sample_callback, SRC_SINC_FASTEST, 1,
                                       &mono_err, &mono_samples);
    SRC_STATE *stereo = src_callback_new(sample_callback, SRC_SINC_FASTEST, 2,
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

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/mcpx/apu/resampler/pack-mono", test_pack_mono);
    g_test_add_func("/mcpx/apu/resampler/expand-mono", test_expand_mono);
    g_test_add_func("/mcpx/apu/resampler/sinc-equivalence",
                    test_sinc_mono_matches_duplicated_stereo);
    g_test_add_func("/mcpx/apu/resampler/sinc-streaming-equivalence",
                    test_sinc_streaming_mono_matches_duplicated_stereo);
    return g_test_run();
}

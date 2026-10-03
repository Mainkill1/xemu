/* SPDX-License-Identifier: LGPL-2.0-or-later */

#ifndef HW_XBOX_MCPX_APU_VP_RESAMPLE_H
#define HW_XBOX_MCPX_APU_VP_RESAMPLE_H

#include <samplerate.h>

#include "xemu-config.h"

static inline int mcpx_apu_resampler_type(CONFIG_AUDIO_VP_RESAMPLER mode)
{
    switch (mode) {
    case CONFIG_AUDIO_VP_RESAMPLER_LINEAR:
        return SRC_LINEAR;
    case CONFIG_AUDIO_VP_RESAMPLER_SINC:
    default:
        return SRC_SINC_FASTEST;
    }
}

static inline void mcpx_apu_resampler_destroy(SRC_STATE **resampler,
                                               int *channels)
{
    if (*resampler != NULL) {
        src_delete(*resampler);
        *resampler = NULL;
    }
    *channels = 0;
}

static inline void mcpx_apu_pack_mono_samples(const float stereo[][2],
                                               float mono[], int frames)
{
    for (int i = 0; i < frames; i++) {
        mono[i] = stereo[i][0];
    }
}

typedef struct MCPXAPUResamplerFetchResult {
    int frames;
    bool end_of_input;
} MCPXAPUResamplerFetchResult;

typedef MCPXAPUResamplerFetchResult (*MCPXAPUResamplerFetchSamples)(
    void *opaque, float samples[][2], int requested);

static inline long mcpx_apu_resampler_fill_input_block(
    int channels, int frames, MCPXAPUResamplerFetchSamples fetch,
    void *opaque, float stereo[][2], float mono[], float **data,
    bool *end_of_input, int converter_type)
{
    assert(channels == 1 || channels == 2);
    assert(frames > 0);

    int sample_count = 0;
    *end_of_input = false;
    while (sample_count < frames) {
        int remaining = frames - sample_count;
        MCPXAPUResamplerFetchResult result =
            fetch(opaque, &stereo[sample_count], remaining);
        if (result.frames <= 0) {
            *end_of_input = result.end_of_input;
            break;
        }
        assert(result.frames <= remaining);
        if (channels == 1) {
            mcpx_apu_pack_mono_samples(&stereo[sample_count],
                                       &mono[sample_count], result.frames);
        }
        sample_count += result.frames;
        if (result.end_of_input) {
            *end_of_input = true;
            break;
        }
    }

    if (*end_of_input && sample_count == 1 && converter_type == SRC_LINEAR) {
        /*
         * libsamplerate 0.2.2's linear converter reads before the supplied
         * buffer when a callback returns exactly one frame (upstream issue
         * #208 / PR #209). Supplying one silent guard frame keeps the
         * production callback out of that unsafe third-party schedule while
         * preserving the real source cursor and end-of-input state.
         */
        if (channels == 1) {
            mono[sample_count] = 0.0f;
        } else {
            memset(&stereo[sample_count], 0, sizeof(stereo[0]));
        }
        sample_count++;
    }

    if (*end_of_input) {
        *data = channels == 1 ? mono : (float *)stereo;
        return sample_count;
    }

    if (sample_count < frames) {
        if (channels == 1) {
            memset(&mono[sample_count], 0,
                   (frames - sample_count) * sizeof(float));
        } else {
            memset(&stereo[sample_count], 0,
                   (frames - sample_count) * sizeof(stereo[0]));
        }
    }

    *data = channels == 1 ? mono : (float *)stereo;
    return frames;
}

static inline void mcpx_apu_expand_mono_samples(const float mono[],
                                                 float stereo[][2], int frames)
{
    for (int i = 0; i < frames; i++) {
        stereo[i][0] = mono[i];
        stereo[i][1] = mono[i];
    }
}

#endif

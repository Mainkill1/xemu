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

static inline void mcpx_apu_pack_mono_samples(const float stereo[][2],
                                               float mono[], int frames)
{
    for (int i = 0; i < frames; i++) {
        mono[i] = stereo[i][0];
    }
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

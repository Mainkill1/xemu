/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_AUDIO_HLE_MIX_H
#define XEMU_AUDIO_HLE_MIX_H

/* The direct front-channel path preserves the VP's per-bin gains. */
static inline void audio_hle_front_stereo(const float *left, const float *right,
                                          float *stereo, unsigned frames)
{
    for (unsigned i = 0; i < frames; i++) {
        stereo[2 * i] = left[i];
        stereo[2 * i + 1] = right[i];
    }
}
#endif

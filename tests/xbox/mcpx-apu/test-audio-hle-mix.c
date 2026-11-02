/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include "hw/xbox/mcpx/apu/audio-hle-mix.h"

int main(void)
{
    const float left[] = { 0.5f, -0.25f, 0.0f, 1.5f };
    const float right[] = { -0.125f, 0.75f, 0.0f, -2.0f };
    float stereo[8] = { 0 };
    audio_hle_front_stereo(left, right, stereo, 4);
    for (int i = 0; i < 4; i++) {
        assert(stereo[2 * i] == left[i]);
        assert(stereo[2 * i + 1] == right[i]);
    }
    return 0;
}

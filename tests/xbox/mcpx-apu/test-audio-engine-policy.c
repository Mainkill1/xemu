/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include "hw/xbox/mcpx/apu/audio-engine-policy.h"

int main(void)
{
    for (int dsp = 0; dsp <= 1; dsp++) {
        for (int jit = 0; jit <= 1; jit++) {
            AudioEnginePolicy legacy = audio_engine_policy(0, dsp, jit);
            assert(legacy.realtime_dsp == (bool)dsp);
            assert(legacy.jit == (bool)jit);
            assert(!legacy.bypass_dsp);
            AudioEnginePolicy forced = audio_engine_policy(1, dsp, jit);
            assert(forced.realtime_dsp && forced.jit && !forced.bypass_dsp);
            AudioEnginePolicy hle = audio_engine_policy(2, dsp, jit);
            assert(!hle.realtime_dsp && hle.bypass_dsp);
            AudioEnginePolicy invalid = audio_engine_policy(99, dsp, jit);
            assert(invalid.realtime_dsp == legacy.realtime_dsp);
            assert(invalid.jit == legacy.jit && !invalid.bypass_dsp);
        }
    }
    return 0;
}

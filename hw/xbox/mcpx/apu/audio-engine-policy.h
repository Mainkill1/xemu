/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_AUDIO_ENGINE_POLICY_H
#define XEMU_AUDIO_ENGINE_POLICY_H
#include <stdbool.h>
enum { AUDIO_ENGINE_DEFAULT, AUDIO_ENGINE_JIT, AUDIO_ENGINE_HLE };
typedef struct AudioEnginePolicy {
    bool realtime_dsp;
    bool jit;
    bool bypass_dsp;
} AudioEnginePolicy;
static inline AudioEnginePolicy audio_engine_policy(int engine, bool dsp,
                                                    bool jit)
{
    if (engine == AUDIO_ENGINE_JIT) {
        return (AudioEnginePolicy){ true, true, false };
    }
    if (engine == AUDIO_ENGINE_HLE) {
        /* Native stages need the complete frame and a JIT fallback. */
        return (AudioEnginePolicy){ true, true, false };
    }
    return (AudioEnginePolicy){ dsp, jit, false };
}
#endif

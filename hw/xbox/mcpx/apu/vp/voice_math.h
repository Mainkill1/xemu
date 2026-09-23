/*
 * QEMU MCPX Audio Processing Unit implementation
 *
 * Copyright (c) 2026 James Rowe
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef HW_XBOX_MCPX_APU_VP_VOICE_MATH_H
#define HW_XBOX_MCPX_APU_VP_VOICE_MATH_H

#include <math.h>

static inline float mcpx_apu_clamp_sample(float sample)
{
    if (isnan(sample)) {
        return -1.0f;
    }
    return fminf(fmaxf(sample, -1.0f), 1.0f);
}

#endif

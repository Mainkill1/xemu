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

#ifndef HW_XBOX_MCPX_APU_VP_VOICE_FORMAT_H
#define HW_XBOX_MCPX_APU_VP_VOICE_FORMAT_H

#include <stdbool.h>
#include <stdint.h>

#include "hw/xbox/mcpx/apu/apu_regs.h"
#include "qemu/host-utils.h"

typedef struct MCPXAPUVoiceFormat {
    bool stereo;
    uint8_t channels;
    uint8_t sample_size;
    uint8_t container_size;
    bool stream;
    bool loop;
    uint8_t samples_per_block;
    bool persist;
    bool multipass;
    bool linked;
} MCPXAPUVoiceFormat;

static inline uint32_t mcpx_apu_voice_format_field(uint32_t word,
                                                   uint32_t mask)
{
    return (word & mask) >> ctz32(mask);
}

static inline MCPXAPUVoiceFormat mcpx_apu_decode_voice_format(uint32_t word)
{
    MCPXAPUVoiceFormat format = {
        .stereo = mcpx_apu_voice_format_field(
            word, NV_PAVS_VOICE_CFG_FMT_STEREO),
        .sample_size = mcpx_apu_voice_format_field(
            word, NV_PAVS_VOICE_CFG_FMT_SAMPLE_SIZE),
        .container_size = mcpx_apu_voice_format_field(
            word, NV_PAVS_VOICE_CFG_FMT_CONTAINER_SIZE),
        .stream = mcpx_apu_voice_format_field(
            word, NV_PAVS_VOICE_CFG_FMT_DATA_TYPE),
        .loop = mcpx_apu_voice_format_field(
            word, NV_PAVS_VOICE_CFG_FMT_LOOP),
        .samples_per_block = 1 + mcpx_apu_voice_format_field(
            word, NV_PAVS_VOICE_CFG_FMT_SAMPLES_PER_BLOCK),
        .persist = mcpx_apu_voice_format_field(
            word, NV_PAVS_VOICE_CFG_FMT_PERSIST),
        .multipass = mcpx_apu_voice_format_field(
            word, NV_PAVS_VOICE_CFG_FMT_MULTIPASS),
        .linked = mcpx_apu_voice_format_field(
            word, NV_PAVS_VOICE_CFG_FMT_LINKED),
    };

    format.channels = format.stereo ? 2 : 1;
    return format;
}

#endif

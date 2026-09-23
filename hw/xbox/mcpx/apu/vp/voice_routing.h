/* SPDX-License-Identifier: LGPL-2.1-or-later */

#ifndef HW_XBOX_MCPX_APU_VP_VOICE_ROUTING_H
#define HW_XBOX_MCPX_APU_VP_VOICE_ROUTING_H

#include <stdbool.h>
#include <stdint.h>

#include "hw/xbox/mcpx/apu/apu_regs.h"
#include "qemu/host-utils.h"

typedef struct MCPXAPUVoiceRouting {
    uint32_t src;
    uint32_t dst;
    uint32_t clr;
} MCPXAPUVoiceRouting;

static inline uint32_t mcpx_apu_voice_routing_field(uint32_t word,
                                                    uint32_t mask)
{
    return (word & mask) >> ctz32(mask);
}

static inline MCPXAPUVoiceRouting
mcpx_apu_decode_voice_routing(uint32_t cfg_fmt, uint32_t cfg_vbin, bool is_3d,
                              const uint8_t hrtf_submix[4])
{
    MCPXAPUVoiceRouting routing = { 0 };
    int bin[8];

    if (mcpx_apu_voice_routing_field(
            cfg_fmt, NV_PAVS_VOICE_CFG_FMT_MULTIPASS)) {
        int mp_bin = mcpx_apu_voice_routing_field(
            cfg_fmt, NV_PAVS_VOICE_CFG_FMT_MULTIPASS_BIN);

        routing.src |= 1U << mp_bin;
        if (mcpx_apu_voice_routing_field(
                cfg_fmt, NV_PAVS_VOICE_CFG_FMT_CLEAR_MIX)) {
            routing.clr |= 1U << mp_bin;
        }
    }

    if (is_3d) {
        for (int i = 0; i < 4; i++) {
            bin[i] = hrtf_submix[i];
        }
    } else {
        bin[0] = mcpx_apu_voice_routing_field(
            cfg_vbin, NV_PAVS_VOICE_CFG_VBIN_V0BIN);
        bin[1] = mcpx_apu_voice_routing_field(
            cfg_vbin, NV_PAVS_VOICE_CFG_VBIN_V1BIN);
        bin[2] = mcpx_apu_voice_routing_field(
            cfg_vbin, NV_PAVS_VOICE_CFG_VBIN_V2BIN);
        bin[3] = mcpx_apu_voice_routing_field(
            cfg_vbin, NV_PAVS_VOICE_CFG_VBIN_V3BIN);
    }
    bin[4] = mcpx_apu_voice_routing_field(
        cfg_vbin, NV_PAVS_VOICE_CFG_VBIN_V4BIN);
    bin[5] = mcpx_apu_voice_routing_field(
        cfg_vbin, NV_PAVS_VOICE_CFG_VBIN_V5BIN);
    bin[6] = mcpx_apu_voice_routing_field(cfg_fmt,
                                         NV_PAVS_VOICE_CFG_FMT_V6BIN);
    bin[7] = mcpx_apu_voice_routing_field(cfg_fmt,
                                         NV_PAVS_VOICE_CFG_FMT_V7BIN);

    for (int i = 0; i < 8; i++) {
        routing.dst |= 1U << bin[i];
    }
    return routing;
}

#endif

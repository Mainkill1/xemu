/*
 * QEMU MCPX Audio Processing Unit voice mix helpers
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef HW_XBOX_MCPX_APU_VP_VOICE_MIX_H
#define HW_XBOX_MCPX_APU_VP_VOICE_MIX_H

#include <stdint.h>

#include "hw/xbox/mcpx/apu/apu_regs.h"

static inline void mcpx_apu_voice_decode_mix_bins(uint32_t cfg_fmt,
                                                   uint32_t cfg_vbin,
                                                   uint8_t bins[8])
{
    bins[0] = (cfg_vbin & NV_PAVS_VOICE_CFG_VBIN_V0BIN) >> 0;
    bins[1] = (cfg_vbin & NV_PAVS_VOICE_CFG_VBIN_V1BIN) >> 5;
    bins[2] = (cfg_vbin & NV_PAVS_VOICE_CFG_VBIN_V2BIN) >> 10;
    bins[3] = (cfg_vbin & NV_PAVS_VOICE_CFG_VBIN_V3BIN) >> 16;
    bins[4] = (cfg_vbin & NV_PAVS_VOICE_CFG_VBIN_V4BIN) >> 21;
    bins[5] = (cfg_vbin & NV_PAVS_VOICE_CFG_VBIN_V5BIN) >> 26;
    bins[6] = (cfg_fmt & NV_PAVS_VOICE_CFG_FMT_V6BIN) >> 0;
    bins[7] = (cfg_fmt & NV_PAVS_VOICE_CFG_FMT_V7BIN) >> 5;
}

static inline void mcpx_apu_voice_decode_mix_volumes(uint32_t vola,
                                                      uint32_t volb,
                                                      uint32_t volc,
                                                      uint16_t volumes[8])
{
    volumes[0] = (vola & NV_PAVS_VOICE_TAR_VOLA_VOLUME0) >> 4;
    volumes[1] = (vola & NV_PAVS_VOICE_TAR_VOLA_VOLUME1) >> 20;
    volumes[2] = (volb & NV_PAVS_VOICE_TAR_VOLB_VOLUME2) >> 4;
    volumes[3] = (volb & NV_PAVS_VOICE_TAR_VOLB_VOLUME3) >> 20;
    volumes[4] = (volc & NV_PAVS_VOICE_TAR_VOLC_VOLUME4) >> 4;
    volumes[5] = (volc & NV_PAVS_VOICE_TAR_VOLC_VOLUME5) >> 20;
    volumes[6] = (volc & NV_PAVS_VOICE_TAR_VOLC_VOLUME6_B11_8) << 8;
    volumes[6] |= (volb & NV_PAVS_VOICE_TAR_VOLB_VOLUME6_B7_4) << 4;
    volumes[6] |= vola & NV_PAVS_VOICE_TAR_VOLA_VOLUME6_B3_0;
    volumes[7] = (volc & NV_PAVS_VOICE_TAR_VOLC_VOLUME7_B11_8) >> 8;
    volumes[7] |= (volb & NV_PAVS_VOICE_TAR_VOLB_VOLUME7_B7_4) >> 12;
    volumes[7] |= (vola & NV_PAVS_VOICE_TAR_VOLA_VOLUME7_B3_0) >> 16;
}

#endif

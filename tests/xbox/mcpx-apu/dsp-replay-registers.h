/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_DSP_REPLAY_REGISTERS_H
#define XEMU_DSP_REPLAY_REGISTERS_H

#include <stdbool.h>
#include <stdint.h>
#include "hw/xbox/mcpx/apu/dsp/interp/dsp_cpu_regs.h"

/* Match the widening used by dsp_jit_sync_from_vm for legacy C state.
 * Preserve all JIT bits: truncating them would hide 24-bit differences. */
static inline uint32_t dsp_replay_normalize_register(bool jit, unsigned reg,
                                                     uint32_t value)
{
    if (!jit && reg >= DSP_REG_N0 && reg <= DSP_REG_N7 && (value & 0x8000)) {
        value |= 0xff0000;
    }
    if (!jit && reg >= DSP_REG_M0 && reg <= DSP_REG_M7 && value == 0xffff) {
        value = 0xffffff;
    }
    return value;
}

#endif

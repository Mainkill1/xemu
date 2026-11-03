/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_GP_MAC_PROJECT_H
#define XEMU_GP_MAC_PROJECT_H

#include "dsp-jit-state.h"
#include "gp-mix2-hle.h"

#define GP_MAC_PROJECT_CYCLES 64

/* Caller verifies program identity; run-loop budget is not modeled. */
static inline unsigned gp_mac_index(uint32_t x)
{
    return x >= 0x1400 && x < 0x1800 ? x - 0x800 : x;
}
static inline bool gp_mac_project(const DSPJitState *before, DSPJitState *after)
{
    if (!before || !after) {
        return false;
    }
    const Dsp56300State *cpu = &before->cpu;
    const uint32_t *r = cpu->registers;
    unsigned sp = r[DSP_REG_SP];
    if (cpu->pc != 0x49b || cpu->pc_advance != 2 || cpu->pc_on_rep ||
        cpu->loop_rep || cpu->halt_requested || cpu->power_state ||
        cpu->interrupts.pending_bits[0] || cpu->interrupts.pending_bits[1] ||
        cpu->interrupts.pipeline_stage || cpu->interrupts.state ||
        cpu->interrupts.ipl_to_raise || r[DSP_REG_OMR] != 0x300 ||
        (r[DSP_REG_SR] & ~(0x3fU | (1U << DSP_SR_LF))) != 0xc003c0 ||
        r[DSP_REG_M0] != 0xffffff || r[DSP_REG_M1] != 0xffffff || sp < 2 ||
        sp > 15 || r[DSP_REG_LA] != 0x49e || r[DSP_REG_LC] != 16 ||
        r[DSP_REG_SSH] != cpu->stack[0][sp] ||
        r[DSP_REG_SSL] != cpu->stack[1][sp] ||
        !(r[DSP_REG_SR] & (1U << DSP_SR_LF)) ||
        !(r[DSP_REG_SSL] & (1U << DSP_SR_LF))) {
        return false;
    }
    unsigned x = gp_mac_index(r[DSP_REG_R0]), y = gp_mac_index(r[DSP_REG_R1]);
    if (x > DSP_XRAM_SIZE - 16 || y > DSP_XRAM_SIZE - 16 ||
        (x < y + 16 && y < x + 16) || r[DSP_REG_X0] > 0xffffff ||
        (r[DSP_REG_R0] >= 0x1400 && r[DSP_REG_R0] < 0x1800 &&
         r[DSP_REG_R0] > 0x17f0) ||
        (r[DSP_REG_R1] >= 0x1400 && r[DSP_REG_R1] < 0x1800 &&
         r[DSP_REG_R1] > 0x17f0)) {
        return false;
    }
    uint32_t output[16];
    int64_t gain = gp_mix2_signed24(r[DSP_REG_X0]);
    for (unsigned i = 0; i < 16; i++) {
        uint32_t src = before->xram[x + i], dst = before->xram[y + i];
        if (src > 0xffffff || dst > 0xffffff) {
            return false;
        }
        int64_t acc = 2 * gain * gp_mix2_signed24(src) +
                      gp_mix2_signed24(dst) * 0x1000000;
        int64_t word = gp_mix2_floor_shift(gp_mix2_round24(acc), 24);
        if (word < -0x800000 || word > 0x7fffff) {
            return false;
        }
        output[i] = (uint32_t)word & 0xffffff;
    }
    uint32_t last = output[15];
    bool u = !(((last >> 23) ^ (last >> 22)) & 1);
    DSPJitState next = *before;
    memcpy(next.xram + y, output, sizeof(output));
    next.cpu.pc = 0x49f;
    next.cpu.pc_advance = 1;
    next.cpu.cycle_count += GP_MAC_PROJECT_CYCLES;
    next.cpu.registers[DSP_REG_X1] = before->xram[x + 15];
    next.cpu.registers[DSP_REG_A0] = 0;
    next.cpu.registers[DSP_REG_A1] = last;
    next.cpu.registers[DSP_REG_A2] = last & 0x800000 ? 0xff : 0;
    next.cpu.registers[DSP_REG_R0] = (r[DSP_REG_R0] + 16) & 0xffffff;
    next.cpu.registers[DSP_REG_R1] = (r[DSP_REG_R1] + 16) & 0xffffff;
    next.cpu.registers[DSP_REG_SR] =
        (r[DSP_REG_SR] & ~0x3eU) | (u ? 1U << DSP_SR_U : 0) |
        (last & 0x800000 ? 1U << DSP_SR_N : 0) | (!last ? 1U << DSP_SR_Z : 0);
    next.cpu.registers[DSP_REG_SP] = sp - 2;
    next.cpu.registers[DSP_REG_SSH] = cpu->stack[0][sp - 2];
    next.cpu.registers[DSP_REG_SSL] = cpu->stack[1][sp - 2];
    next.cpu.registers[DSP_REG_LA] = cpu->stack[0][sp - 1];
    next.cpu.registers[DSP_REG_LC] = cpu->stack[1][sp - 1];
    *after = next;
    return true;
}

#endif

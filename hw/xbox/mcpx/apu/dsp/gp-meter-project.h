/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_GP_METER_PROJECT_H
#define XEMU_GP_METER_PROJECT_H

#include "dsp-jit-state.h"

#define GP_METER_PROJECT_CYCLES 357

static inline int64_t gp_meter_signed24(uint32_t value)
{
    return value & 0x800000 ? (int64_t)value - 0x1000000 : value;
}

/* Offline stepped projection for one identified 32-sample meter. The caller
 * verifies program identity; run-loop cycle budgets are not modeled. */
static inline bool gp_meter_project(const DSPJitState *before,
                                    DSPJitState *after)
{
    if (!before || !after) {
        return false;
    }
    const Dsp56300State *cpu = &before->cpu;
    const uint32_t *r = cpu->registers;
    uint32_t sp = r[DSP_REG_SP];
    if (cpu->pc != 0x431 || cpu->pc_advance != 1 || cpu->pc_on_rep ||
        cpu->loop_rep || cpu->halt_requested || cpu->power_state ||
        cpu->interrupts.pending_bits[0] || cpu->interrupts.pending_bits[1] ||
        cpu->interrupts.pipeline_stage || cpu->interrupts.state ||
        cpu->interrupts.ipl_to_raise || r[DSP_REG_OMR] != 0x300 ||
        (r[DSP_REG_SR] & ~(0x3fU | (1U << DSP_SR_LF))) != 0xc003c0 ||
        r[DSP_REG_A0] || r[DSP_REG_A1] || r[DSP_REG_A2] ||
        r[DSP_REG_M4] != 0xffffff || sp > 13 ||
        r[DSP_REG_SSH] != cpu->stack[0][sp] ||
        r[DSP_REG_SSL] != cpu->stack[1][sp]) {
        return false;
    }
    uint32_t src = r[DSP_REG_R4], dst = r[DSP_REG_R2];
    src = src >= 0x1400 && src < 0x1800 ? src - 0x800 : src;
    dst = dst >= 0x1400 && dst < 0x1800 ? dst - 0x800 : dst;
    if (src > DSP_XRAM_SIZE - 32 || dst >= DSP_XRAM_SIZE ||
        (dst >= src && dst < src + 32) || before->xram[dst] > 0xffffff) {
        return false;
    }
    int64_t peak = gp_meter_signed24(before->xram[dst]);
    int64_t prior = peak, sum = 0, last = 0;
    for (unsigned i = 0; i < 32; i++) {
        uint32_t word = before->xram[src + i];
        if (word > 0xffffff || word == 0x800000) {
            return false;
        }
        int64_t sample = gp_meter_signed24(word);
        last = sample < 0 ? -sample : sample;
        if (i == 31) {
            prior = peak;
        }
        peak = peak > last ? peak : last;
        sum += last >> 5;
    }
    uint32_t low = (last << 19) & 0xffffff;
    bool u = !((((uint32_t)sum >> 23) ^ ((uint32_t)sum >> 22)) & 1);
    DSPJitState next = *before;
    next.xram[dst] = (uint32_t)peak & 0xffffff;
    next.cpu.pc = 0x43e;
    next.cpu.pc_advance = 1;
    next.cpu.cycle_count += GP_METER_PROJECT_CYCLES;
    next.cpu.registers[DSP_REG_X0] = last;
    next.cpu.registers[DSP_REG_Y0] = 0x040000;
    next.cpu.registers[DSP_REG_A0] = low;
    next.cpu.registers[DSP_REG_A1] = sum;
    next.cpu.registers[DSP_REG_A2] = 0;
    next.cpu.registers[DSP_REG_B0] = 0;
    next.cpu.registers[DSP_REG_B1] = (uint32_t)peak & 0xffffff;
    next.cpu.registers[DSP_REG_B2] = 0;
    next.cpu.registers[DSP_REG_R4] = (r[DSP_REG_R4] + 32) & 0xffffff;
    next.cpu.registers[DSP_REG_SR] =
        (r[DSP_REG_SR] & ~0x3fU) | (prior > last ? 1U << DSP_SR_C : 0) |
        (u ? 1U << DSP_SR_U : 0) | (!sum && !low ? 1U << DSP_SR_Z : 0);
    next.cpu.stack[0][sp + 1] = r[DSP_REG_LA];
    next.cpu.stack[1][sp + 1] = r[DSP_REG_LC];
    next.cpu.stack[0][sp + 2] = 0x433;
    next.cpu.stack[1][sp + 2] = r[DSP_REG_SR];
    *after = next;
    return true;
}
#endif

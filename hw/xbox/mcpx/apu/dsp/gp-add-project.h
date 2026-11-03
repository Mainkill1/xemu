/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_GP_ADD_PROJECT_H
#define XEMU_GP_ADD_PROJECT_H

#include "dsp-jit-state.h"

#define GP_ADD_PROJECT_CYCLES 69

static inline int64_t gp_add_signed24(uint32_t value)
{
    return value & 0x800000 ? (int64_t)value - 0x1000000 : value;
}

/* Offline instruction-stepped projection. The caller verifies the exact
 * program. The run-loop cycle budget is intentionally not modeled. */
static inline bool gp_add_project(const DSPJitState *before, DSPJitState *after)
{
    if (!before || !after) {
        return false;
    }
    const Dsp56300State *cpu = &before->cpu;
    const uint32_t *r = cpu->registers;
    uint32_t sp = r[DSP_REG_SP];
    if (cpu->pc != 0x4a0 || cpu->pc_advance || cpu->pc_on_rep ||
        cpu->loop_rep || cpu->halt_requested || cpu->power_state ||
        cpu->interrupts.pending_bits[0] || cpu->interrupts.pending_bits[1] ||
        cpu->interrupts.pipeline_stage || cpu->interrupts.state ||
        cpu->interrupts.ipl_to_raise || r[DSP_REG_OMR] != 0x300 ||
        (r[DSP_REG_SR] & ~(0x3fU | (1U << DSP_SR_LF))) != 0xc003c0 ||
        r[DSP_REG_M0] != 0xffffff || r[DSP_REG_M1] != 0xffffff || sp > 13 ||
        r[DSP_REG_SSH] != cpu->stack[0][sp] ||
        r[DSP_REG_SSL] != cpu->stack[1][sp]) {
        return false;
    }
    uint32_t r0 = r[DSP_REG_R0], r1 = r[DSP_REG_R1];
    uint32_t x = r0 >= 0x1400 && r0 < 0x1800 ? r0 - 0x800 : r0;
    uint32_t y = r1 >= 0x1400 && r1 < 0x1800 ? r1 - 0x800 : r1;
    if (x > DSP_XRAM_SIZE - 16 || y > DSP_XRAM_SIZE - 16 ||
        (x < y + 16 && y < x + 16)) {
        return false;
    }
    uint32_t output[16];
    for (unsigned i = 0; i < 16; i++) {
        if (before->xram[x + i] > 0xffffff || before->xram[y + i] > 0xffffff) {
            return false;
        }
        int64_t sum = gp_add_signed24(before->xram[x + i]) +
                      gp_add_signed24(before->xram[y + i]);
        if (sum < -0x800000 || sum > 0x7fffff) {
            return false;
        }
        output[i] = (uint32_t)sum & 0xffffff;
    }
    uint32_t source = before->xram[x + 15], previous = before->xram[y + 15];
    uint64_t mask = UINT64_C(0xffffffffffffff);
    uint64_t a = (uint64_t)(gp_add_signed24(source) * 0x1000000) & mask;
    uint64_t b = (uint64_t)(gp_add_signed24(previous) * 0x1000000) & mask;
    uint32_t last = output[15];
    bool carry = (a + b) >> 56;
    bool unnormalized = !(((last >> 23) ^ (last >> 22)) & 1);
    DSPJitState next = *before;
    memcpy(next.xram + y, output, sizeof(output));
    next.cpu.pc = 0x4a6;
    next.cpu.pc_advance = 1;
    next.cpu.cycle_count += GP_ADD_PROJECT_CYCLES;
    next.cpu.registers[DSP_REG_X0] = source;
    next.cpu.registers[DSP_REG_A0] = 0;
    next.cpu.registers[DSP_REG_A1] = last;
    next.cpu.registers[DSP_REG_A2] = last & 0x800000 ? 0xff : 0;
    next.cpu.registers[DSP_REG_R0] = (r0 + 16) & 0xffffff;
    next.cpu.registers[DSP_REG_R1] = (r1 + 16) & 0xffffff;
    next.cpu.registers[DSP_REG_SR] =
        (r[DSP_REG_SR] & ~0x3fU) | (carry ? 1U << DSP_SR_C : 0) |
        (unnormalized ? 1U << DSP_SR_U : 0) |
        (last & 0x800000 ? 1U << DSP_SR_N : 0) | (!last ? 1U << DSP_SR_Z : 0);
    next.cpu.stack[0][sp + 1] = r[DSP_REG_LA];
    next.cpu.stack[1][sp + 1] = r[DSP_REG_LC];
    next.cpu.stack[0][sp + 2] = 0x4a2;
    next.cpu.stack[1][sp + 2] = r[DSP_REG_SR];
    *after = next;
    return true;
}
#endif

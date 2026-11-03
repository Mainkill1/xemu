/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_GP_MIX2_PROJECT_H
#define XEMU_GP_MIX2_PROJECT_H

#include "dsp-jit-state.h"
#include "gp-mix2-hle.h"

#define GP_MIX2_PROJECT_CYCLES 332

/* Offline instruction-stepped projection; caller must recognize the exact
 * program. Run-loop cycle budgets and the following RTS remain DSP work. */
static inline bool gp_mix2_project(const DSPJitState *before,
                                   DSPJitState *after)
{
    if (!before || !after) {
        return false;
    }
    const Dsp56300State *cpu = &before->cpu;
    const uint32_t *r = cpu->registers;
    uint32_t sp = r[DSP_REG_SP], addr[5];
    if (cpu->pc != 0x761 || cpu->pc_advance || cpu->pc_on_rep ||
        cpu->loop_rep || cpu->halt_requested || cpu->power_state ||
        cpu->interrupts.pending_bits[0] || cpu->interrupts.pending_bits[1] ||
        cpu->interrupts.pipeline_stage || cpu->interrupts.state ||
        cpu->interrupts.ipl_to_raise || r[DSP_REG_OMR] != 0x300 ||
        (r[DSP_REG_SR] & ~(0x3fU | (1U << DSP_SR_LF))) != 0xc003c0 || sp > 13 ||
        r[DSP_REG_SSH] != cpu->stack[0][sp] ||
        r[DSP_REG_SSL] != cpu->stack[1][sp]) {
        return false;
    }
    static const uint32_t lengths[5] = { 2, 32, 32, 16, 32 };
    for (unsigned i = 0; i < 5; i++) {
        uint32_t v = r[DSP_REG_R0 + i];
        addr[i] = v >= 0x1400 && v < 0x1800 ? v - 0x800 : v;
        if (r[DSP_REG_M0 + i] != 0xffffff ||
            addr[i] > DSP_XRAM_SIZE - lengths[i]) {
            return false;
        }
    }
    for (unsigned i = 0; i < 4; i++) {
        if (addr[4] < addr[i] + lengths[i] && addr[i] < addr[4] + 32) {
            return false;
        }
    }
    uint32_t output[32];
    if (!gp_mix2_hle(before->xram + addr[1], before->xram + addr[2],
                     before->xram + addr[3], before->xram[addr[0]],
                     before->xram[addr[0] + 1], r[DSP_REG_Y0], r[DSP_REG_Y1],
                     output)) {
        return false;
    }
    DSPJitState next = *before;
    memcpy(next.xram + addr[4], output, sizeof(output));
    next.cpu.pc = 0x77b;
    next.cpu.pc_advance = 1;
    next.cpu.cycle_count += GP_MIX2_PROJECT_CYCLES;
    next.cpu.registers[DSP_REG_X0] = before->xram[addr[0]];
    next.cpu.registers[DSP_REG_X1] = before->xram[addr[3] + 15];
    int64_t half = gp_mix2_floor_shift(gp_mix2_signed24(r[DSP_REG_Y1]), 1);
    next.cpu.registers[DSP_REG_Y1] = (uint32_t)half & 0xffffff;
    next.cpu.registers[DSP_REG_A0] = next.cpu.registers[DSP_REG_B0] = 0;
    next.cpu.registers[DSP_REG_A1] = output[15];
    next.cpu.registers[DSP_REG_B1] = output[31];
    next.cpu.registers[DSP_REG_A2] = output[15] & 0x800000 ? 0xff : 0;
    next.cpu.registers[DSP_REG_B2] = output[31] & 0x800000 ? 0xff : 0;
    for (unsigned i = 0; i < 5; i++) {
        next.cpu.registers[DSP_REG_R0 + i] =
            (r[DSP_REG_R0 + i] + (i ? 16 : 1)) & 0xffffff;
    }
    next.cpu.registers[DSP_REG_N1] = 16;
    next.cpu.registers[DSP_REG_N2] = 16;
    next.cpu.registers[DSP_REG_N4] = 16;
    uint32_t last = output[15];
    bool u = !(((last >> 23) ^ (last >> 22)) & 1);
    next.cpu.registers[DSP_REG_SR] =
        (r[DSP_REG_SR] & ~0x3fU) | (u ? 1U << DSP_SR_U : 0) |
        (last & 0x800000 ? (1U << DSP_SR_N) | (1U << DSP_SR_C) : 0) |
        (!last ? 1U << DSP_SR_Z : 0);
    next.cpu.stack[0][sp + 1] = r[DSP_REG_LA];
    next.cpu.stack[1][sp + 1] = r[DSP_REG_LC];
    next.cpu.stack[0][sp + 2] = 0x76a;
    uint32_t h = (uint32_t)half & 0xffffff;
    bool half_u = !(((h >> 23) ^ (h >> 22)) & 1);
    next.cpu.stack[1][sp + 2] =
        (r[DSP_REG_SR] & ~0x3fU) | (half_u ? 1U << DSP_SR_U : 0) |
        (gp_mix2_signed24(r[DSP_REG_Y1]) < 0 ? 1U << DSP_SR_N : 0) |
        (r[DSP_REG_Y1] == 0 ? 1U << DSP_SR_Z : 0);
    *after = next;
    return true;
}
#endif

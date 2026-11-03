/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_GP_MATRIX_PROJECT_H
#define XEMU_GP_MATRIX_PROJECT_H

#include "dsp-jit-state.h"
#include "gp-mix2-hle.h"

#define GP_MATRIX_PROJECT_CYCLES 357

/* Offline projection of a recognized four-input accumulation. The caller
 * verifies program identity; the run-loop cycle budget is not modeled. */
static inline bool gp_matrix_project(const DSPJitState *before,
                                     DSPJitState *after)
{
    if (!before || !after) {
        return false;
    }
    const Dsp56300State *cpu = &before->cpu;
    const uint32_t *r = cpu->registers;
    uint32_t sp = r[DSP_REG_SP];
    if (cpu->pc != 0x6a2 || cpu->pc_advance != 2 || cpu->pc_on_rep ||
        cpu->loop_rep || cpu->halt_requested || cpu->power_state ||
        cpu->interrupts.pending_bits[0] || cpu->interrupts.pending_bits[1] ||
        cpu->interrupts.pipeline_stage || cpu->interrupts.state ||
        cpu->interrupts.ipl_to_raise || r[DSP_REG_OMR] != 0x300 ||
        (r[DSP_REG_SR] & ~(0x3fU | (1U << DSP_SR_LF))) != 0xc003c0 ||
        r[DSP_REG_N0] != 16 || r[DSP_REG_M0] != 0xffffff ||
        r[DSP_REG_M1] != 0xffffff || r[DSP_REG_M2] != 0xffffff ||
        r[DSP_REG_M4] != 0xffffff || sp > 11 ||
        r[DSP_REG_SSH] != cpu->stack[0][sp] ||
        r[DSP_REG_SSL] != cpu->stack[1][sp]) {
        return false;
    }
    uint32_t src = r[DSP_REG_R1], coeff = r[DSP_REG_R3];
    uint32_t dst = r[DSP_REG_R4];
    src = src >= 0x1400 && src < 0x1800 ? src - 0x800 : src;
    coeff = coeff >= 0x1400 && coeff < 0x1800 ? coeff - 0x800 : coeff;
    dst = dst >= 0x1400 && dst < 0x1800 ? dst - 0x800 : dst;
    if (src > DSP_XRAM_SIZE - 64 || coeff > DSP_XRAM_SIZE - 4 ||
        dst > DSP_XRAM_SIZE - 16 || (dst < src + 64 && src < dst + 16) ||
        (dst < coeff + 4 && coeff < dst + 16)) {
        return false;
    }
    uint32_t output[16];
    for (unsigned i = 0; i < 16; i++) {
        int64_t acc = 0;
        for (unsigned k = 0; k < 4; k++) {
            uint32_t x = before->xram[src + i + 16 * k];
            uint32_t c = before->xram[coeff + k];
            if (x > 0xffffff || c > 0xffffff) {
                return false;
            }
            acc = gp_mix2_round24(acc + 2 * gp_mix2_signed24(x) *
                                            gp_mix2_signed24(c));
            int64_t word = gp_mix2_floor_shift(acc, 24);
            if (word < -0x800000 || word > 0x7fffff) {
                return false;
            }
        }
        output[i] = (uint32_t)gp_mix2_floor_shift(acc, 24) & 0xffffff;
    }
    uint32_t last = output[15];
    bool u = !(((last >> 23) ^ (last >> 22)) & 1);
    DSPJitState next = *before;
    memcpy(next.xram + dst, output, sizeof(output));
    next.cpu.pc = 0x6ae;
    next.cpu.pc_advance = 1;
    next.cpu.cycle_count += GP_MATRIX_PROJECT_CYCLES;
    next.cpu.registers[DSP_REG_X0] = before->xram[src + 15];
    next.cpu.registers[DSP_REG_X1] = before->xram[coeff + 3];
    next.cpu.registers[DSP_REG_A0] = 0;
    next.cpu.registers[DSP_REG_A1] = last;
    next.cpu.registers[DSP_REG_A2] = last & 0x800000 ? 0xff : 0;
    next.cpu.registers[DSP_REG_R0] = (r[DSP_REG_R1] + 79) & 0xffffff;
    next.cpu.registers[DSP_REG_R1] = (r[DSP_REG_R1] + 16) & 0xffffff;
    next.cpu.registers[DSP_REG_R2] = (r[DSP_REG_R3] + 4) & 0xffffff;
    next.cpu.registers[DSP_REG_R4] = (r[DSP_REG_R4] + 16) & 0xffffff;
    next.cpu.registers[DSP_REG_SR] =
        (r[DSP_REG_SR] & ~0x3eU) | (u ? 1U << DSP_SR_U : 0) |
        (last & 0x800000 ? 1U << DSP_SR_N : 0) | (!last ? 1U << DSP_SR_Z : 0);
    next.cpu.stack[0][sp + 1] = r[DSP_REG_LA];
    next.cpu.stack[1][sp + 1] = r[DSP_REG_LC];
    next.cpu.stack[0][sp + 2] = 0x6a4;
    next.cpu.stack[1][sp + 2] = r[DSP_REG_SR];
    next.cpu.stack[0][sp + 3] = 0x6ad;
    next.cpu.stack[1][sp + 3] = 1;
    next.cpu.stack[0][sp + 4] = 0x6a9;
    next.cpu.stack[1][sp + 4] =
        (r[DSP_REG_SR] &
         ~((1U << DSP_SR_E) | (1U << DSP_SR_N) | (1U << DSP_SR_V))) |
        (1U << DSP_SR_U) | (1U << DSP_SR_Z) | (1U << DSP_SR_LF);
    *after = next;
    return true;
}
#endif

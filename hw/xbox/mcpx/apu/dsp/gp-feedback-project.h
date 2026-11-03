/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_GP_FEEDBACK_PROJECT_H
#define XEMU_GP_FEEDBACK_PROJECT_H

#include "dsp-jit-state.h"
#include "gp-feedback-hle.h"

/* Offline, instruction-stepped projection. The caller must recognize the
 * exact program; this does not model a run-loop cycle budget or perform RTS. */
static inline bool gp_feedback_project(const DSPJitState *before,
                                       DSPJitState *after)
{
    if (!before || !after) {
        return false;
    }
    const Dsp56300State *cpu = &before->cpu;
    const uint32_t *r = cpu->registers;
    uint32_t sp = r[DSP_REG_SP];
    if (cpu->pc != 0x788 || cpu->pc_advance != 0 || cpu->pc_on_rep ||
        cpu->loop_rep || cpu->halt_requested || cpu->power_state ||
        cpu->interrupts.pending_bits[0] || cpu->interrupts.pending_bits[1] ||
        cpu->interrupts.pipeline_stage || cpu->interrupts.state ||
        cpu->interrupts.ipl_to_raise || r[DSP_REG_OMR] != 0x300 ||
        (r[DSP_REG_SR] & ~(0x3fU | (1U << DSP_SR_LF))) != 0xc003c0 ||
        r[DSP_REG_M0] != 0xffffff || sp > 13 ||
        r[DSP_REG_SSH] != cpu->stack[0][sp] ||
        r[DSP_REG_SSL] != cpu->stack[1][sp]) {
        return false;
    }
    uint32_t r0 = r[DSP_REG_R0], r1 = r[DSP_REG_R1];
    uint32_t x0 = r0 >= 0x1400 && r0 < 0x1800 ? r0 - 0x800 : r0;
    uint32_t x1 = r1 >= 0x1400 && r1 < 0x1800 ? r1 - 0x800 : r1;
    if (x0 > DSP_XRAM_SIZE - 16 || x1 > DSP_XRAM_SIZE - 3 ||
        (x1 < x0 + 16 && x0 < x1 + 3)) {
        return false;
    }
    uint32_t output[16], final_state;
    if (!gp_feedback_hle(before->xram + x0, before->xram[x1],
                         before->xram[x1 + 1], before->xram[x1 + 2], output,
                         &final_state)) {
        return false;
    }
    int64_t sum = (int64_t)gp_feedback_signed24(output[14]) *
                      gp_feedback_signed24(before->xram[x1 + 1]) +
                  (int64_t)gp_feedback_signed24(before->xram[x0 + 15]) *
                      gp_feedback_signed24(before->xram[x1 + 2]);
    uint64_t acc = (uint64_t)(sum * 2) & UINT64_C(0xffffffffffffff);
    uint32_t a1 = (acc >> 24) & 0xffffff;
    bool unnormalized = !(((a1 >> 23) ^ (a1 >> 22)) & 1);
    DSPJitState next = *before;
    memcpy(next.xram + x0, output, sizeof(output));
    next.xram[x1] = final_state;
    next.cpu.pc = 0x792;
    next.cpu.pc_advance = 1;
    next.cpu.cycle_count += 137;
    next.cpu.registers[DSP_REG_X0] = output[14];
    next.cpu.registers[DSP_REG_X1] = before->xram[x0 + 15];
    next.cpu.registers[DSP_REG_Y0] = before->xram[x1 + 1];
    next.cpu.registers[DSP_REG_Y1] = before->xram[x1 + 2];
    next.cpu.registers[DSP_REG_A0] = acc & 0xffffff;
    next.cpu.registers[DSP_REG_A1] = a1;
    next.cpu.registers[DSP_REG_A2] = (acc >> 48) & 0xff;
    next.cpu.registers[DSP_REG_R0] = (r0 + 16) & 0xffffff;
    next.cpu.registers[DSP_REG_SR] =
        (r[DSP_REG_SR] & ~0x3eU) | (unnormalized ? 1U << DSP_SR_U : 0) |
        (sum < 0 ? 1U << DSP_SR_N : 0) | (sum == 0 ? 1U << DSP_SR_Z : 0);
    next.cpu.stack[0][sp + 1] = r[DSP_REG_LA];
    next.cpu.stack[1][sp + 1] = r[DSP_REG_LC];
    next.cpu.stack[0][sp + 2] = 0x78c;
    next.cpu.stack[1][sp + 2] = r[DSP_REG_SR];
    *after = next;
    return true;
}
#endif

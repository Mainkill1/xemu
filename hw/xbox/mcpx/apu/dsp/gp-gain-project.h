/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_GP_GAIN_PROJECT_H
#define XEMU_GP_GAIN_PROJECT_H

#include "dsp-jit-state.h"
#include "gp-mix2-hle.h"

#define GP_GAIN_PROJECT_CYCLES 53

/* Caller verifies the exact GP program; stepped replay excludes its budget. */
static inline bool gp_gain_project(const DSPJitState *before,
                                   DSPJitState *after)
{
    if (!before || !after) {
        return false;
    }
    const Dsp56300State *cpu = &before->cpu;
    const uint32_t *r = cpu->registers;
    unsigned sp = r[DSP_REG_SP];
    if (cpu->pc != 0x493 || cpu->pc_advance != 0 || cpu->pc_on_rep ||
        cpu->loop_rep || cpu->halt_requested || cpu->power_state ||
        cpu->interrupts.pending_bits[0] || cpu->interrupts.pending_bits[1] ||
        cpu->interrupts.pipeline_stage || cpu->interrupts.state ||
        cpu->interrupts.ipl_to_raise || r[DSP_REG_OMR] != 0x300 ||
        (r[DSP_REG_SR] & ~(0x3fU | (1U << DSP_SR_LF))) != 0xc003c0 ||
        r[DSP_REG_M0] != 0xffffff || r[DSP_REG_X0] > 0xffffff || sp > 13 ||
        r[DSP_REG_SSH] != cpu->stack[0][sp] ||
        r[DSP_REG_SSL] != cpu->stack[1][sp]) {
        return false;
    }
    uint32_t address = r[DSP_REG_R0];
    bool alias = address >= 0x1400 && address < 0x1800;
    if (alias && address > 0x17f0) {
        return false;
    }
    unsigned src = alias ? address - 0x800 : address;
    if (src > DSP_XRAM_SIZE - 16) {
        return false;
    }
    uint32_t samples[16];
    int64_t gain = gp_mix2_signed24(r[DSP_REG_X0]), product = 0;
    for (unsigned i = 0; i < 16; i++) {
        if (before->xram[src + i] > 0xffffff) {
            return false;
        }
        product = 2 * gain * gp_mix2_signed24(before->xram[src + i]);
        int64_t word = gp_mix2_floor_shift(product, 24);
        if (word < -0x800000 || word > 0x7fffff) {
            return false;
        }
        samples[i] = (uint32_t)word & 0xffffff;
    }
    uint64_t acc = (uint64_t)product & UINT64_C(0xffffffffffffff);
    uint32_t high = (acc >> 24) & 0xffffff;
    bool unnormalized = !(((high >> 23) ^ (high >> 22)) & 1);
    DSPJitState next = *before;
    memcpy(next.xram + src, samples, sizeof(samples));
    uint32_t *out = next.cpu.registers;
    next.cpu.pc = 0x498;
    next.cpu.pc_advance = 1;
    next.cpu.cycle_count += GP_GAIN_PROJECT_CYCLES;
    out[DSP_REG_X1] = before->xram[src + 15];
    out[DSP_REG_A0] = acc & 0xffffff;
    out[DSP_REG_A1] = high;
    out[DSP_REG_A2] = (acc >> 48) & 0xff;
    out[DSP_REG_R0] = address + 16;
    out[DSP_REG_SR] = (r[DSP_REG_SR] & ~0x3eU) |
                      (unnormalized ? 1U << DSP_SR_U : 0) |
                      (product < 0 ? 1U << DSP_SR_N : 0) |
                      (product == 0 ? 1U << DSP_SR_Z : 0);
    next.cpu.stack[0][sp + 1] = r[DSP_REG_LA];
    next.cpu.stack[1][sp + 1] = r[DSP_REG_LC];
    next.cpu.stack[0][sp + 2] = 0x495;
    next.cpu.stack[1][sp + 2] = r[DSP_REG_SR];
    *after = next;
    return true;
}
#endif

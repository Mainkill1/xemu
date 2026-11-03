/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_GP_DECIMATE_PROJECT_H
#define XEMU_GP_DECIMATE_PROJECT_H

#include "dsp-jit-state.h"
#include "gp-decimate-hle.h"
#include "gp-mix2-hle.h"

static inline bool gp_decimate_project_range(uint32_t address, unsigned length,
                                             unsigned *mapped)
{
    if (!length || address > UINT32_MAX - (length - 1)) {
        return false;
    }
    uint32_t end = address + length - 1;
    bool alias = address >= 0x1400 && address < 0x1800;
    if ((end >= 0x1400 && end < 0x1800) != alias) {
        return false;
    }
    unsigned start = alias ? address - 0x800 : address;
    if (start >= DSP_XRAM_SIZE || length > DSP_XRAM_SIZE - start) {
        return false;
    }
    *mapped = start;
    return true;
}

/* The caller verifies the exact program; this offline projection does not
 * model the run-loop cycle budget or execute the following DMA operations. */
static inline bool gp_decimate_project(const DSPJitState *before,
                                       DSPJitState *after)
{
    if (!before || !after) {
        return false;
    }
    const Dsp56300State *cpu = &before->cpu;
    const uint32_t *r = cpu->registers;
    uint32_t pc = cpu->pc;
    unsigned sp = r[DSP_REG_SP], phase = r[DSP_REG_A1];
    if ((pc != 0x986 && pc != 0xa02 && pc != 0xa7e && pc != 0xafa) ||
        cpu->pc_advance != 1 || cpu->pc_on_rep || cpu->loop_rep ||
        cpu->halt_requested || cpu->power_state ||
        cpu->interrupts.pending_bits[0] || cpu->interrupts.pending_bits[1] ||
        cpu->interrupts.pipeline_stage || cpu->interrupts.state ||
        cpu->interrupts.ipl_to_raise || r[DSP_REG_OMR] != 0x300 ||
        (r[DSP_REG_SR] & ~0x3fU) != 0xc003c0 || r[DSP_REG_A0] != 0 ||
        r[DSP_REG_A2] != 0 || phase < 1 || phase > 6 || r[DSP_REG_B0] != 0 ||
        r[DSP_REG_B1] != 0 || r[DSP_REG_B2] != 0 || r[DSP_REG_Y0] != 6 ||
        r[DSP_REG_Y1] > 0xffffff || r[DSP_REG_M0] != 0xffffff ||
        r[DSP_REG_M1] != 0xffffff || sp > 11 ||
        r[DSP_REG_SSH] != cpu->stack[0][sp] ||
        r[DSP_REG_SSL] != cpu->stack[1][sp]) {
        return false;
    }
    /* In-place output is safe: every output follows its input read. */
    unsigned src, dst;
    if (!gp_decimate_project_range(r[DSP_REG_R0], 32, &src) ||
        !gp_decimate_project_range(r[DSP_REG_R1], 6, &dst) ||
        (dst > src && dst < src + 32)) {
        return false;
    }
    uint32_t output[6];
    unsigned count, next_phase;
    if (!gp_decimate_hle(before->xram + src, r[DSP_REG_Y1], phase, output,
                         &count, &next_phase)) {
        return false;
    }
    int64_t gain = gp_mix2_signed24(r[DSP_REG_Y1]);
    unsigned current_phase = phase, last_emit = 0;
    for (unsigned i = 0; i < 32; i++) {
        if (current_phase >= 6) {
            last_emit = i;
            current_phase = 0;
        }
        current_phase++;
    }
    int64_t product =
        2 * gain * gp_mix2_signed24(before->xram[src + last_emit]);
    uint32_t high = (uint32_t)gp_mix2_floor_shift(product, 24) & 0xffffff;
    bool unnormalized = !(((high >> 23) ^ (high >> 22)) & 1);
    uint32_t inner_sr = (r[DSP_REG_SR] & ~0x3fU) | (1U << DSP_SR_LF) |
                        (unnormalized ? 1U << DSP_SR_U : 0) |
                        (product < 0 ? 1U << DSP_SR_N : 0) |
                        (product == 0 ? 1U << DSP_SR_Z : 0);
    uint32_t last = before->xram[src + 31];
    if (last_emit == 31) {
        last = high;
    }
    DSPJitState next = *before;
    memcpy(next.xram + dst, output, count * sizeof(*output));
    uint32_t *out = next.cpu.registers;
    next.cpu.pc = pc + 0x10;
    next.cpu.pc_advance = 1;
    next.cpu.cycle_count += 261 + 19 * count;
    out[DSP_REG_X0] = last;
    out[DSP_REG_A0] = 0;
    out[DSP_REG_A1] = next_phase;
    out[DSP_REG_A2] = 0;
    out[DSP_REG_B0] = 0;
    out[DSP_REG_B1] = count;
    out[DSP_REG_B2] = 0;
    out[DSP_REG_R0] = r[DSP_REG_R0] + 32;
    out[DSP_REG_R1] = r[DSP_REG_R1] + count;
    out[DSP_REG_SR] = (r[DSP_REG_SR] & ~0x3fU) | (1U << DSP_SR_U);
    next.cpu.stack[0][sp + 1] = r[DSP_REG_LA];
    next.cpu.stack[1][sp + 1] = r[DSP_REG_LC];
    next.cpu.stack[0][sp + 2] = pc + 2;
    next.cpu.stack[1][sp + 2] = r[DSP_REG_SR];
    next.cpu.stack[0][sp + 3] = pc + 0xf;
    next.cpu.stack[1][sp + 3] = next_phase;
    next.cpu.stack[0][sp + 4] = pc + 0xb;
    next.cpu.stack[1][sp + 4] = inner_sr;
    *after = next;
    return true;
}
#endif

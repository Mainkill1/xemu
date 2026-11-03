/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_GP_DELAY_PROJECT_H
#define XEMU_GP_DELAY_PROJECT_H

#include "dsp-jit-state.h"
#include "gp-mix2-hle.h"

typedef struct GPDelayRange {
    unsigned start, length;
} GPDelayRange;

/* Resolve GP mix-buffer aliases only when the whole range stays contiguous. */
static inline bool gp_delay_range(uint32_t address, unsigned length,
                                  GPDelayRange *range)
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
    *range = (GPDelayRange){ start, length };
    return true;
}

static inline bool gp_delay_store(uint32_t *ram, unsigned address, int64_t acc)
{
    int64_t word = gp_mix2_floor_shift(acc, 24);
    if (word < -0x800000 || word > 0x7fffff) {
        return false;
    }
    ram[address] = (uint32_t)word & 0xffffff;
    return true;
}

/* Offline projection only. The caller verifies program identity; the
 * instruction-stepped replay intentionally does not model run-loop budget. */
static inline bool gp_delay_project(const DSPJitState *before,
                                    DSPJitState *after)
{
    if (!before || !after) {
        return false;
    }
    const Dsp56300State *cpu = &before->cpu;
    const uint32_t *r = cpu->registers;
    unsigned sp = r[DSP_REG_SP], phase = r[DSP_REG_N3];
    if (cpu->pc != 0x944 || cpu->pc_advance != 2 || cpu->pc_on_rep ||
        cpu->loop_rep || cpu->halt_requested || cpu->power_state ||
        cpu->interrupts.pending_bits[0] || cpu->interrupts.pending_bits[1] ||
        cpu->interrupts.pipeline_stage || cpu->interrupts.state ||
        cpu->interrupts.ipl_to_raise || r[DSP_REG_OMR] != 0x300 ||
        (r[DSP_REG_SR] & ~0x3fU) != 0xd003c0 || phase > 10 ||
        r[DSP_REG_N4] != phase || r[DSP_REG_Y1] > 0xffffff || sp > 13 ||
        r[DSP_REG_SSH] != cpu->stack[0][sp] ||
        r[DSP_REG_SSL] != cpu->stack[1][sp]) {
        return false;
    }
    for (unsigned i = 0; i < 8; i++) {
        if (r[DSP_REG_M0 + i] != 0xffffff) {
            return false;
        }
    }
    uint32_t base = r[DSP_REG_R5];
    if (base > DSP_XRAM_SIZE - 64) {
        return false;
    }
    GPDelayRange ranges[10];
    if (!gp_delay_range(r[DSP_REG_R0], 1, &ranges[0]) ||
        !gp_delay_range(r[DSP_REG_R0] + 12, 1, &ranges[1]) ||
        !gp_delay_range(r[DSP_REG_R1], 32, &ranges[2]) ||
        !gp_delay_range(r[DSP_REG_R2], 32, &ranges[3]) ||
        !gp_delay_range(r[DSP_REG_R3], 11, &ranges[4]) ||
        !gp_delay_range(r[DSP_REG_R4], 11, &ranges[5]) ||
        !gp_delay_range(r[DSP_REG_R6], 32, &ranges[6]) ||
        !gp_delay_range(r[DSP_REG_R7], 32, &ranges[7]) ||
        !gp_delay_range(base + 4, 1, &ranges[8]) ||
        !gp_delay_range(base + 63, 1, &ranges[9])) {
        return false;
    }
    for (unsigned i = 0; i < 10; i++) {
        for (unsigned j = i + 1; j < 10; j++) {
            if (ranges[i].start < ranges[j].start + ranges[j].length &&
                ranges[j].start < ranges[i].start + ranges[i].length) {
                return false;
            }
        }
        for (unsigned j = 0; j < ranges[i].length; j++) {
            if (before->xram[ranges[i].start + j] > 0xffffff) {
                return false;
            }
        }
    }
    DSPJitState next = *before;
    uint32_t *m = next.xram;
    unsigned n = phase;
    int64_t y0 = gp_mix2_signed24(m[ranges[9].start]);
    int64_t y1 = gp_mix2_signed24(r[DSP_REG_Y1]);
    bool wet = m[ranges[8].start] & 2;
    int64_t a = 0, b = 0, last_x = 0, last_y = 0;
    for (unsigned i = 0; i < 32; i++) {
        b = 2 * (y0 * gp_mix2_signed24(m[ranges[0].start]) +
                 y1 * gp_mix2_signed24(m[ranges[4].start + n]));
        if (!gp_delay_store(m, ranges[0].start, b)) {
            return false;
        }
        a = 2 * (y0 * gp_mix2_signed24(m[ranges[1].start]) +
                 y1 * gp_mix2_signed24(m[ranges[5].start + n]));
        if (!gp_delay_store(m, ranges[1].start, a)) {
            return false;
        }
        int64_t x = gp_mix2_signed24(m[ranges[2].start + i]);
        int64_t y = gp_mix2_signed24(m[ranges[3].start + i]);
        a += x * 0x1000000;
        b += y * 0x1000000;
        if (!gp_delay_store(m, ranges[4].start + n, a) ||
            !gp_delay_store(m, ranges[5].start + n, b)) {
            return false;
        }
        if (!wet) {
            a = x * 0x1000000;
            b = y * 0x1000000;
        }
        last_x = gp_mix2_signed24(m[ranges[6].start + i]);
        last_y = gp_mix2_signed24(m[ranges[7].start + i]);
        a += last_x * 0x1000000;
        b += last_y * 0x1000000;
        if (!gp_delay_store(m, ranges[6].start + i, a) ||
            !gp_delay_store(m, ranges[7].start + i, b)) {
            return false;
        }
        n = (n + 1) % 11;
    }
    uint64_t b56 = (uint64_t)b & UINT64_C(0xffffffffffffff);
    uint32_t *out = next.cpu.registers;
    next.cpu.pc = 0x96c;
    next.cpu.pc_advance = 1;
    next.cpu.cycle_count += 1511 + (phase != 0) + (wet ? 0 : 128);
    out[DSP_REG_X0] = (uint32_t)last_x & 0xffffff;
    out[DSP_REG_X1] = (uint32_t)last_y & 0xffffff;
    out[DSP_REG_Y0] = m[ranges[8].start];
    out[DSP_REG_B0] = b56 & 0xffffff;
    out[DSP_REG_B1] = (b56 >> 24) & 0xffffff;
    out[DSP_REG_B2] = (b56 >> 48) & 0xff;
    out[DSP_REG_A0] = 0;
    out[DSP_REG_A1] = n;
    out[DSP_REG_A2] = 0;
    out[DSP_REG_R1] = r[DSP_REG_R1] + 32;
    out[DSP_REG_R2] = r[DSP_REG_R2] + 32;
    out[DSP_REG_R6] = r[DSP_REG_R6] + 32;
    out[DSP_REG_R7] = r[DSP_REG_R7] + 32;
    out[DSP_REG_N3] = out[DSP_REG_N4] = n;
    out[DSP_REG_SR] =
        (r[DSP_REG_SR] & ~0x3fU) | (1U << DSP_SR_U) |
        (n ? ((1U << DSP_SR_N) | (1U << DSP_SR_C)) : (1U << DSP_SR_Z));
    next.cpu.stack[0][sp + 1] = r[DSP_REG_LA];
    next.cpu.stack[1][sp + 1] = r[DSP_REG_LC];
    next.cpu.stack[0][sp + 2] = 0x946;
    next.cpu.stack[1][sp + 2] = r[DSP_REG_SR];
    *after = next;
    return true;
}
#endif

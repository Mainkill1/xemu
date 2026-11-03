/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_GP_DELAY_SLICE_PROJECT_H
#define XEMU_GP_DELAY_SLICE_PROJECT_H
#include "gp-delay-project.h"

/* Complete a recognized delay loop after its current iteration. */
static inline bool gp_delay_residual_project(const DSPJitState *before,
                                             DSPJitState *after)
{
    if (!before || !after) {
        return false;
    }
    const Dsp56300State *cpu = &before->cpu;
    const uint32_t *r = cpu->registers;
    unsigned sp = r[DSP_REG_SP], phase = r[DSP_REG_N3], count = r[DSP_REG_LC];
    if (cpu->pc != 0x946 || cpu->pc_advance != 1 || cpu->pc_on_rep ||
        cpu->loop_rep || cpu->halt_requested || cpu->power_state ||
        cpu->interrupts.pending_bits[0] || cpu->interrupts.pending_bits[1] ||
        cpu->interrupts.pipeline_stage || cpu->interrupts.state ||
        cpu->interrupts.ipl_to_raise || r[DSP_REG_OMR] != 0x300 ||
        (r[DSP_REG_SR] & ~0x3fU) != 0xd083c0 || phase > 10 ||
        r[DSP_REG_N4] != phase || r[DSP_REG_Y1] > 0xffffff || sp < 2 ||
        sp > 15 || !count || count > 32 || r[DSP_REG_LA] != 0x96b ||
        r[DSP_REG_SSH] != 0x946 || r[DSP_REG_SSH] != cpu->stack[0][sp] ||
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
        !gp_delay_range(r[DSP_REG_R1], count, &ranges[2]) ||
        !gp_delay_range(r[DSP_REG_R2], count, &ranges[3]) ||
        !gp_delay_range(r[DSP_REG_R3], 11, &ranges[4]) ||
        !gp_delay_range(r[DSP_REG_R4], 11, &ranges[5]) ||
        !gp_delay_range(r[DSP_REG_R6], count, &ranges[6]) ||
        !gp_delay_range(r[DSP_REG_R7], count, &ranges[7]) ||
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
    for (unsigned i = 0; i < count; i++) {
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
    next.cpu.cycle_count += count * (wet ? 47 : 51) + (phase + count) / 11;
    out[DSP_REG_X0] = (uint32_t)last_x & 0xffffff;
    out[DSP_REG_X1] = (uint32_t)last_y & 0xffffff;
    out[DSP_REG_Y0] = m[ranges[8].start];
    out[DSP_REG_B0] = b56 & 0xffffff;
    out[DSP_REG_B1] = (b56 >> 24) & 0xffffff;
    out[DSP_REG_B2] = (b56 >> 48) & 0xff;
    out[DSP_REG_A0] = 0;
    out[DSP_REG_A1] = n;
    out[DSP_REG_A2] = 0;
    out[DSP_REG_R1] = r[DSP_REG_R1] + count;
    out[DSP_REG_R2] = r[DSP_REG_R2] + count;
    out[DSP_REG_R6] = r[DSP_REG_R6] + count;
    out[DSP_REG_R7] = r[DSP_REG_R7] + count;
    out[DSP_REG_N3] = out[DSP_REG_N4] = n;
    out[DSP_REG_SR] =
        (r[DSP_REG_SR] & ~0x3fU) | (1U << DSP_SR_U) |
        (n ? ((1U << DSP_SR_N) | (1U << DSP_SR_C)) : (1U << DSP_SR_Z));
    out[DSP_REG_SR] =
        (out[DSP_REG_SR] & ~((1U << DSP_SR_LF) | (1U << DSP_SR_FV))) |
        (cpu->stack[1][sp] & ((1U << DSP_SR_LF) | (1U << DSP_SR_FV)));
    out[DSP_REG_SP] = sp - 2;
    out[DSP_REG_SSH] = cpu->stack[0][sp - 2];
    out[DSP_REG_SSL] = cpu->stack[1][sp - 2];
    out[DSP_REG_LA] = cpu->stack[0][sp - 1];
    out[DSP_REG_LC] = cpu->stack[1][sp - 1];
    *after = next;
    return true;
}

static inline int64_t gp_delay_slice_acc(const uint32_t *r, unsigned lo,
                                         unsigned hi, unsigned ext)
{
    uint64_t bits = ((uint64_t)(r[ext] & 0xff) << 48) |
                    ((uint64_t)(r[hi] & 0xffffff) << 24) | (r[lo] & 0xffffff);
    return bits & (UINT64_C(1) << 55) ? (int64_t)bits - (INT64_C(1) << 56) :
                                        (int64_t)bits;
}
static inline bool gp_delay_tail_project(const DSPJitState *before,
                                         DSPJitState *after)
{
    const Dsp56300State *cpu = &before->cpu;
    const uint32_t *r = cpu->registers;
    unsigned n = r[DSP_REG_N3], count = r[DSP_REG_LC], sp = r[DSP_REG_SP];
    if (cpu->pc != 0x95e || cpu->pc_advance || cpu->pc_on_rep ||
        cpu->loop_rep || cpu->halt_requested || cpu->power_state ||
        cpu->interrupts.pending_bits[0] || cpu->interrupts.pending_bits[1] ||
        cpu->interrupts.pipeline_stage || cpu->interrupts.state ||
        cpu->interrupts.ipl_to_raise || r[DSP_REG_OMR] != 0x300 ||
        (r[DSP_REG_SR] & ~0x3fU) != 0xd083c0 || !count || count > 32 ||
        n > 10 || r[DSP_REG_N4] != n || sp < 2 || sp > 15 ||
        r[DSP_REG_LA] != 0x96b || r[DSP_REG_SSH] != 0x946 ||
        r[DSP_REG_SSH] != cpu->stack[0][sp] ||
        r[DSP_REG_SSL] != cpu->stack[1][sp] || r[DSP_REG_A0] > 0xffffff ||
        r[DSP_REG_A1] > 0xffffff || r[DSP_REG_A2] > 0xff ||
        r[DSP_REG_B0] > 0xffffff || r[DSP_REG_B1] > 0xffffff ||
        r[DSP_REG_B2] > 0xff)
        return false;
    for (unsigned i = 0; i < 8; i++)
        if (r[DSP_REG_M0 + i] != 0xffffff)
            return false;
    GPDelayRange x, y;
    if (!gp_delay_range(r[DSP_REG_R6], 1, &x) ||
        !gp_delay_range(r[DSP_REG_R7], 1, &y) || x.start == y.start)
        return false;
    if (before->xram[x.start] > 0xffffff || before->xram[y.start] > 0xffffff)
        return false;
    int64_t vx = gp_mix2_signed24(before->xram[x.start]),
            vy = gp_mix2_signed24(before->xram[y.start]);
    int64_t a = gp_delay_slice_acc(r, DSP_REG_A0, DSP_REG_A1, DSP_REG_A2) +
                vx * 0x1000000;
    int64_t b = gp_delay_slice_acc(r, DSP_REG_B0, DSP_REG_B1, DSP_REG_B2) +
                vy * 0x1000000;
    DSPJitState next = *before;
    if (!gp_delay_store(next.xram, x.start, a) ||
        !gp_delay_store(next.xram, y.start, b))
        return false;
    uint64_t bb = (uint64_t)b & UINT64_C(0xffffffffffffff);
    uint32_t *out = next.cpu.registers;
    n = (n + 1) % 11;
    out[DSP_REG_X0] = (uint32_t)vx & 0xffffff;
    out[DSP_REG_X1] = (uint32_t)vy & 0xffffff;
    out[DSP_REG_B0] = bb & 0xffffff;
    out[DSP_REG_B1] = (bb >> 24) & 0xffffff;
    out[DSP_REG_B2] = (bb >> 48) & 0xff;
    out[DSP_REG_A0] = 0;
    out[DSP_REG_A1] = n;
    out[DSP_REG_A2] = 0;
    out[DSP_REG_R6] = (r[DSP_REG_R6] + 1) & 0xffffff;
    out[DSP_REG_R7] = (r[DSP_REG_R7] + 1) & 0xffffff;
    out[DSP_REG_N3] = out[DSP_REG_N4] = n;
    out[DSP_REG_SR] =
        (r[DSP_REG_SR] & ~0x3fU) | (1U << DSP_SR_U) |
        (n ? ((1U << DSP_SR_N) | (1U << DSP_SR_C)) : (1U << DSP_SR_Z));
    next.cpu.cycle_count += 16 + (n == 0);
    next.cpu.pc_advance = 1;
    if (count == 1) {
        out[DSP_REG_SR] =
            (out[DSP_REG_SR] & ~((1U << DSP_SR_LF) | (1U << DSP_SR_FV))) |
            (cpu->stack[1][sp] & ((1U << DSP_SR_LF) | (1U << DSP_SR_FV)));
        out[DSP_REG_SP] = sp - 2;
        out[DSP_REG_SSH] = cpu->stack[0][sp - 2];
        out[DSP_REG_SSL] = cpu->stack[1][sp - 2];
        out[DSP_REG_LA] = cpu->stack[0][sp - 1];
        out[DSP_REG_LC] = cpu->stack[1][sp - 1];
        next.cpu.pc = 0x96c;
    } else {
        out[DSP_REG_LC] = count - 1;
        next.cpu.pc = 0x946;
    }
    *after = next;
    return true;
}

/* Reject slices that cannot contain the continuation before copying state.
 * A tail output may update the control word, so leave aliases to the DSP. */
static inline bool gp_delay_slice_fits(const Dsp56300State *cpu,
                                       const uint32_t *xram, int64_t budget)
{
    const uint32_t *r = cpu->registers;
    unsigned count = r[DSP_REG_LC], phase = r[DSP_REG_N3];
    uint32_t base = r[DSP_REG_R5];
    if (!count || count > 32 || phase > 10 || r[DSP_REG_N4] != phase ||
        base > DSP_XRAM_SIZE - 64 || xram[base + 4] > 0xffffff) {
        return false;
    }
    GPDelayRange left, right;
    if (!gp_delay_range(r[DSP_REG_R6], 1, &left) ||
        !gp_delay_range(r[DSP_REG_R7], 1, &right) || left.start == base + 4 ||
        right.start == base + 4) {
        return false;
    }
    unsigned next_phase = (phase + 1) % 11;
    unsigned iterations = count - 1;
    bool wet = xram[base + 4] & 2;
    unsigned cost = 16 + (next_phase == 0) + iterations * (wet ? 47 : 51) +
                    (next_phase + iterations) / 11;
    return budget > cost;
}

/* Caller verifies program identity; both transitions are transactional. */
static inline bool gp_delay_slice_project(const DSPJitState *before,
                                          DSPJitState *after)
{
    if (!before || !after)
        return false;
    DSPJitState middle;
    if (!gp_delay_tail_project(before, &middle))
        return false;
    if (middle.cpu.pc == 0x96c) {
        *after = middle;
        return true;
    }
    return gp_delay_residual_project(&middle, after);
}

#endif

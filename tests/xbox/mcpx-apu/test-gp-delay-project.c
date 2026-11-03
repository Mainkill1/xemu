/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "hw/xbox/mcpx/apu/dsp/gp-delay-project.h"
#include "hw/xbox/mcpx/apu/dsp/gp-delay-slice-project.h"

static int run_case(unsigned variant)
{
    DSPJitState before = { 0 }, after, unchanged;
    Dsp56300State *cpu = &before.cpu;
    uint32_t *r = cpu->registers;
    cpu->pc = 0x944;
    cpu->pc_advance = 2;
    cpu->cycle_count = 100;
    r[DSP_REG_OMR] = 0x300;
    r[DSP_REG_SR] = 0xd003d0;
    r[DSP_REG_SP] = 2;
    r[DSP_REG_SSH] = cpu->stack[0][2] = 0x913;
    r[DSP_REG_SSL] = cpu->stack[1][2] = 0xd003d0;
    for (unsigned i = 0; i < 8; i++) {
        r[DSP_REG_M0 + i] = 0xffffff;
    }
    r[DSP_REG_R0] = 0x41d;
    r[DSP_REG_R1] = 0x14c0;
    r[DSP_REG_R2] = 0x14e0;
    r[DSP_REG_R3] = 0x412;
    r[DSP_REG_R4] = 0x41e;
    r[DSP_REG_R5] = 0x405;
    r[DSP_REG_R6] = 0xc00;
    r[DSP_REG_R7] = 0xc20;
    r[DSP_REG_N3] = r[DSP_REG_N4] = variant;
    r[DSP_REG_Y1] = 0x300000 + (variant * 3119 & 0xfffff);
    before.xram[0x409] = variant ? 3 : 1;
    before.xram[0x444] = 0x200000 + (variant * 7919 & 0xfffff);
    before.xram[0x41d] = (variant * 127 + 71) & 0xfffff;
    before.xram[0x429] = (variant * 191 + 131) & 0xfffff;
    for (unsigned i = 0; i < 11; i++) {
        before.xram[0x412 + i] = (i * 997 + variant * 31 + 73) & 0xfffff;
        before.xram[0x41e + i] = ((i * 613 + variant * 43 + 83) & 0xfffff) ^
                                 ((i & 1) ? 0xf00000 : 0);
    }
    for (unsigned i = 0; i < 32; i++) {
        before.xram[0xcc0 + i] = ((i * 379 + variant * 11 + 107) & 0xfffff) ^
                                 ((i & 1) ? 0xf00000 : 0);
        before.xram[0xce0 + i] = ((i * 419 + variant * 17 + 431) & 0xfffff) ^
                                 ((i & 2) ? 0xf00000 : 0);
        before.xram[0xc00 + i] = ((i * 217 + variant * 23 + 91) & 0xfffff) ^
                                 ((i & 2) ? 0xf00000 : 0);
        before.xram[0xc20 + i] = ((i * 113 + variant * 29 + 101) & 0xfffff) ^
                                 ((i & 1) ? 0xf00000 : 0);
    }
    static const uint32_t expected[2][4] = {
        { 0x0000c6, 0xf0031a, 0xe023b2, 0xe048f2 },
        { 0xfa00c8, 0xee83bc, 0xde494b, 0xdaf663 },
    };
    if (!gp_delay_project(&before, &after) ||
        after.xram[0xc00] != expected[variant][0] ||
        after.xram[0xc01] != expected[variant][1] ||
        after.xram[0xc0f] != expected[variant][2] ||
        after.xram[0xc1f] != expected[variant][3] || after.cpu.pc != 0x96c ||
        after.cpu.cycle_count != 100 + (variant ? 1512 : 1639) ||
        after.cpu.registers[DSP_REG_A1] != (variant ? 0 : 10)) {
        fprintf(stderr, "delay projection differs from DSP JIT fixture\n");
        return 1;
    }
    memset(&after, 0x5a, sizeof(after));
    unchanged = after;
    r[DSP_REG_N4] = 11;
    if (gp_delay_project(&before, &after) ||
        memcmp(&after, &unchanged, sizeof(after))) {
        fprintf(stderr,
                "invalid ring state was not rejected transactionally\n");
        return 1;
    }
    r[DSP_REG_N4] = variant;
    r[DSP_REG_R7] = 0xc10;
    if (gp_delay_project(&before, &after) ||
        memcmp(&after, &unchanged, sizeof(after))) {
        fprintf(stderr,
                "overlapping buffers were not rejected transactionally\n");
        return 1;
    }
    r[DSP_REG_R7] = 0xc20;
    cpu->interrupts.pending_bits[0] = 1;
    if (gp_delay_project(&before, &after) ||
        memcmp(&after, &unchanged, sizeof(after))) {
        fprintf(stderr, "pending interrupt was not rejected\n");
        return 1;
    }
    cpu->interrupts.pending_bits[0] = 0;
    before.xram[0xcc0] = 0x7fffff;
    before.xram[0xc00] = 0x7fffff;
    if (gp_delay_project(&before, &after) ||
        memcmp(&after, &unchanged, sizeof(after))) {
        fprintf(stderr, "unqualified saturation was not rejected\n");
        return 1;
    }
    return 0;
}
static int run_slice_case(unsigned variant)
{
    DSPJitState before = { 0 }, after, unchanged;
    Dsp56300State *cpu = &before.cpu;
    uint32_t *r = cpu->registers;
    cpu->pc = 0x95e;
    cpu->pc_advance = 0;
    cpu->cycle_count = 39256;
    cpu->cycle_budget = -21;
    r[DSP_REG_OMR] = 0x300;
    r[DSP_REG_SR] = 0xd083d9;
    r[DSP_REG_SP] = 4;
    r[DSP_REG_SSH] = cpu->stack[0][4] = 0x946;
    r[DSP_REG_SSL] = cpu->stack[1][4] = 0xd003d0;
    cpu->stack[0][2] = 0x913;
    cpu->stack[1][2] = 0xd003d0;
    r[DSP_REG_LA] = 0x96b;
    r[DSP_REG_LC] = 16;
    r[DSP_REG_N3] = r[DSP_REG_N4] = variant % 11;
    for (unsigned i = 0; i < 8; i++)
        r[DSP_REG_M0 + i] = 0xffffff;
    r[DSP_REG_R0] = 0x240;
    r[DSP_REG_R1] = 0x100;
    r[DSP_REG_R2] = 0x140;
    r[DSP_REG_R3] = 0x280;
    r[DSP_REG_R4] = 0x2a0;
    r[DSP_REG_R5] = 0x300;
    r[DSP_REG_R6] = 0x180;
    r[DSP_REG_R7] = 0x1c0;
    r[DSP_REG_Y1] = 0x100000;
    r[DSP_REG_Y0] = variant < 32 ? 2 : 0;
    r[DSP_REG_A0] = 0x10203;
    r[DSP_REG_A1] = 0x1000 + variant;
    r[DSP_REG_B0] = 0x30405;
    r[DSP_REG_B1] = 0xffe000 + variant;
    r[DSP_REG_B2] = 0xff;
    for (unsigned i = 0x100; i < 0x340; i++)
        before.xram[i] = (i * 71 + variant * 101) & 0xffff;
    before.xram[0x304] = variant < 32 ? 2 : 0;
    before.xram[0x33f] = 0x200000;
    int expected_cost = variant < 32 ? 722 : 782;
    if (gp_delay_slice_fits(cpu, before.xram, expected_cost) ||
        !gp_delay_slice_fits(cpu, before.xram, expected_cost + 1) ||
        gp_delay_slice_fits(cpu, before.xram, -1)) {
        fprintf(stderr, "delay slice budget boundary is incorrect\n");
        return 1;
    }
    if (!gp_delay_slice_project(&before, &after) || after.cpu.pc != 0x96c ||
        after.cpu.cycle_count != (variant < 32 ? 39978 : 40038) ||
        after.cpu.cycle_budget != -21 ||
        after.cpu.registers[DSP_REG_A1] != (variant < 32 ? 9 : 8) ||
        after.cpu.registers[DSP_REG_B1] != (variant < 32 ? 0xfb88 : 0x10221) ||
        after.cpu.registers[DSP_REG_R6] != 0x190 ||
        after.cpu.registers[DSP_REG_SP] != 2) {
        fprintf(stderr, "delay slice differs from DSP JIT fixture\n");
        return 1;
    }
    memset(&after, 0x5a, sizeof(after));
    unchanged = after;
    cpu->interrupts.pending_bits[0] = 1;
    if (gp_delay_slice_project(&before, &after) ||
        memcmp(&after, &unchanged, sizeof(after))) {
        fprintf(stderr,
                "delay slice accepted an interrupt or changed output\n");
        return 1;
    }
    cpu->interrupts.pending_bits[0] = 0;
    r[DSP_REG_R7] = r[DSP_REG_R6];
    if (gp_delay_slice_project(&before, &after) ||
        memcmp(&after, &unchanged, sizeof(after))) {
        fprintf(stderr, "delay slice accepted overlapping outputs\n");
        return 1;
    }
    r[DSP_REG_R7] = 0x1c0;
    r[DSP_REG_A2] = 0x100;
    if (gp_delay_slice_project(&before, &after) ||
        memcmp(&after, &unchanged, sizeof(after))) {
        fprintf(stderr, "delay slice accepted a malformed accumulator\n");
        return 1;
    }
    r[DSP_REG_A2] = 0;
    r[DSP_REG_SSH] = 0;
    if (gp_delay_slice_project(&before, &after) ||
        memcmp(&after, &unchanged, sizeof(after))) {
        fprintf(stderr, "delay slice accepted a malformed loop stack\n");
        return 1;
    }
    return 0;
}

int main(void)
{
    return run_case(0) || run_case(1) || run_slice_case(15) ||
           run_slice_case(47);
}

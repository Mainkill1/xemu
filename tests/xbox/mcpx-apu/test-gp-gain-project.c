/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "hw/xbox/mcpx/apu/dsp/gp-gain-project.h"

static int run_case(unsigned variant)
{
    static const uint32_t expected[2][4] = {
        { 0x00002a, 0xf99a43, 0x000719, 0xf9a132 },
        { 0xffffd0, 0x0665b8, 0xfff8e1, 0x065ec9 },
    };
    DSPJitState before = { 0 }, after, unchanged;
    Dsp56300State *cpu = &before.cpu;
    uint32_t *r = cpu->registers;
    cpu->pc = 0x493;
    cpu->pc_advance = 0;
    cpu->cycle_count = 100;
    r[DSP_REG_OMR] = 0x300;
    r[DSP_REG_SR] = 0xc003d0;
    r[DSP_REG_SP] = 3;
    r[DSP_REG_SSH] = cpu->stack[0][3] = 0x5ee;
    r[DSP_REG_SSL] = cpu->stack[1][3] = 0xc003d0;
    r[DSP_REG_R0] = 0x100;
    r[DSP_REG_M0] = 0xffffff;
    r[DSP_REG_X0] = variant ? 0xcccccd : 0x333333;
    for (unsigned i = 0; i < 16; i++) {
        before.xram[0x100 + i] = ((i * 317 + variant * 11 + 107) & 0xfffff) ^
                                 ((i & 1) ? 0xf00000 : 0);
    }
    if (!gp_gain_project(&before, &after) ||
        after.xram[0x100] != expected[variant][0] ||
        after.xram[0x101] != expected[variant][1] ||
        after.xram[0x10e] != expected[variant][2] ||
        after.xram[0x10f] != expected[variant][3] || after.cpu.pc != 0x498 ||
        after.cpu.cycle_count != 153 ||
        after.cpu.registers[DSP_REG_A0] != (variant ? 0x2cd46a : 0x6cc534) ||
        after.cpu.registers[DSP_REG_A1] != expected[variant][3] ||
        after.cpu.registers[DSP_REG_SR] != (variant ? 0xc003d0 : 0xc003d8)) {
        fprintf(stderr, "gain projection differs from DSP JIT fixture\n");
        return 1;
    }
    memset(&after, 0x5a, sizeof(after));
    unchanged = after;
    r[DSP_REG_R0] = 0x17f1;
    if (gp_gain_project(&before, &after) ||
        memcmp(&after, &unchanged, sizeof(after))) {
        fprintf(stderr, "alias boundary crossing was not rejected\n");
        return 1;
    }
    r[DSP_REG_R0] = 0x100;
    cpu->interrupts.pending_bits[0] = 1;
    if (gp_gain_project(&before, &after) ||
        memcmp(&after, &unchanged, sizeof(after))) {
        fprintf(stderr, "pending interrupt was not rejected\n");
        return 1;
    }
    cpu->interrupts.pending_bits[0] = 0;
    r[DSP_REG_X0] = 0x800000;
    before.xram[0x100] = 0x800000;
    if (gp_gain_project(&before, &after) ||
        memcmp(&after, &unchanged, sizeof(after))) {
        fprintf(stderr, "unqualified saturation was not rejected\n");
        return 1;
    }
    return 0;
}
int main(void)
{
    return run_case(0) || run_case(1);
}

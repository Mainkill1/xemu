/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "hw/xbox/mcpx/apu/dsp/gp-decimate-project.h"

int main(void)
{
    static const uint32_t expected[5] = {
        0xffec7d, 0xffd1bc, 0xffb704, 0xff9c43, 0xff8182,
    };
    DSPJitState before = { 0 }, after, untouched;
    Dsp56300State *cpu = &before.cpu;
    uint32_t *r = cpu->registers;
    cpu->pc = 0x986;
    cpu->pc_advance = 1;
    cpu->cycle_count = 100;
    r[DSP_REG_OMR] = 0x300;
    r[DSP_REG_SR] = 0xc003d5;
    r[DSP_REG_SP] = 2;
    r[DSP_REG_SSH] = cpu->stack[0][2] = 0x97c;
    r[DSP_REG_SSL] = cpu->stack[1][2] = 0xc003d9;
    r[DSP_REG_A1] = 2;
    r[DSP_REG_Y0] = 6;
    r[DSP_REG_Y1] = 0xcccccd;
    r[DSP_REG_R0] = 0x100;
    r[DSP_REG_R1] = 0x140;
    r[DSP_REG_M0] = r[DSP_REG_M1] = 0xffffff;
    for (unsigned i = 0; i < 32; i++) {
        before.xram[0x100 + i] =
            ((i * 317 + 11 + 107) & 0xfffff) ^ ((i & 1) ? 0xf00000 : 0);
        before.xram[0x140 + i] =
            ((i * 211 + 17 + 431) & 0xfffff) ^ ((i & 2) ? 0xf00000 : 0);
    }
    if (!gp_decimate_project(&before, &after) ||
        memcmp(after.xram + 0x140, expected, sizeof(expected)) ||
        after.cpu.pc != 0x996 || after.cpu.cycle_count != 456 ||
        after.cpu.registers[DSP_REG_A1] != 4 ||
        after.cpu.registers[DSP_REG_B1] != 5 ||
        after.cpu.registers[DSP_REG_X0] != 0xf026d9 ||
        after.cpu.stack[1][6] != 0xc083d8) {
        fprintf(stderr, "decimation projection differs from DSP JIT fixture\n");
        return 1;
    }
    r[DSP_REG_R1] = 0x100;
    if (!gp_decimate_project(&before, &after) ||
        memcmp(after.xram + 0x100, expected, sizeof(expected))) {
        fprintf(stderr, "in-place DSP decimation differs from JIT fixture\n");
        return 1;
    }
    r[DSP_REG_R1] = 0x140;
    memset(&after, 0x5a, sizeof(after));
    untouched = after;
    r[DSP_REG_R1] = 0x110;
    if (gp_decimate_project(&before, &after) ||
        memcmp(&after, &untouched, sizeof(after))) {
        fprintf(stderr, "overlapping decimation was not rejected\n");
        return 1;
    }
    r[DSP_REG_R1] = 0x140;
    r[DSP_REG_Y0] = 5;
    if (gp_decimate_project(&before, &after) ||
        memcmp(&after, &untouched, sizeof(after))) {
        fprintf(stderr, "unqualified threshold was not rejected\n");
        return 1;
    }
    return 0;
}

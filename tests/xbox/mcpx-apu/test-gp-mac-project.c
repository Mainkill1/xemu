/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "hw/xbox/mcpx/apu/dsp/gp-mac-project.h"

int main(void)
{
    static const uint32_t expected[16] = {
        0x0001da, 0xf99cc5, 0xf0047d, 0xe99f69, 0x000721, 0xf9a20c,
        0xf009c5, 0xe9a4b0, 0x000c68, 0xf9a754, 0xf00f0c, 0xe9a9f7,
        0x0011af, 0xf9ac9b, 0xf01453, 0xe9af3e,
    };
    DSPJitState before = { 0 }, after, untouched;
    Dsp56300State *cpu = &before.cpu;
    cpu->pc = 0x49b;
    cpu->pc_advance = 2;
    cpu->cycle_count = 100;
    cpu->registers[DSP_REG_OMR] = 0x300;
    cpu->registers[DSP_REG_SR] = 0xc083d0;
    cpu->registers[DSP_REG_SP] = 7;
    cpu->registers[DSP_REG_SSH] = cpu->stack[0][7] = 0x49b;
    cpu->registers[DSP_REG_SSL] = cpu->stack[1][7] = 0xc083d0;
    cpu->stack[0][6] = 0x624;
    cpu->stack[1][6] = 4;
    cpu->stack[0][5] = 0x624;
    cpu->stack[1][5] = 0xc083d0;
    cpu->registers[DSP_REG_LA] = 0x49e;
    cpu->registers[DSP_REG_LC] = 16;
    cpu->registers[DSP_REG_R0] = 0x100;
    cpu->registers[DSP_REG_R1] = 0x140;
    cpu->registers[DSP_REG_M0] = cpu->registers[DSP_REG_M1] = 0xffffff;
    cpu->registers[DSP_REG_X0] = 0x333333;
    for (unsigned i = 0; i < 16; i++) {
        before.xram[0x100 + i] =
            ((i * 317 + 107) & 0xfffff) ^ ((i & 1) ? 0xf00000 : 0);
        before.xram[0x140 + i] =
            ((i * 211 + 431) & 0xfffff) ^ ((i & 2) ? 0xf00000 : 0);
    }
    if (!gp_mac_project(&before, &after) ||
        memcmp(after.xram + 0x140, expected, sizeof(expected)) ||
        after.cpu.pc != 0x49f || after.cpu.cycle_count != 164 ||
        after.cpu.registers[DSP_REG_SP] != 5 ||
        after.cpu.registers[DSP_REG_LA] != 0x624 ||
        after.cpu.registers[DSP_REG_LC] != 4 ||
        after.cpu.registers[DSP_REG_SR] != 0xc083d8 ||
        after.cpu.registers[DSP_REG_A1] != 0xe9af3e) {
        fprintf(stderr, "MAC projection differs from DSP JIT oracle\n");
        return 1;
    }
    memset(&after, 0x5a, sizeof(after));
    untouched = after;
    cpu->registers[DSP_REG_R1] = 0x108;
    if (gp_mac_project(&before, &after) ||
        memcmp(&after, &untouched, sizeof(after))) {
        fprintf(stderr, "overlapping MAC was not rejected transactionally\n");
        return 1;
    }
    cpu->registers[DSP_REG_R1] = 0x140;
    cpu->registers[DSP_REG_LC] = 15;
    if (gp_mac_project(&before, &after) ||
        memcmp(&after, &untouched, sizeof(after))) {
        fprintf(stderr, "unsupported loop state was not rejected\n");
        return 1;
    }
    cpu->registers[DSP_REG_LC] = 16;
    cpu->registers[DSP_REG_R0] = 0x17f1;
    if (gp_mac_project(&before, &after) ||
        memcmp(&after, &untouched, sizeof(after))) {
        fprintf(stderr, "mix-buffer alias crossing was not rejected\n");
        return 1;
    }
    cpu->registers[DSP_REG_R0] = 0x100;
    cpu->registers[DSP_REG_SSL] = cpu->stack[1][7] = 0xc003d0;
    if (gp_mac_project(&before, &after) ||
        memcmp(&after, &untouched, sizeof(after))) {
        fprintf(stderr, "unqualified loop flags were not rejected\n");
        return 1;
    }
    return 0;
}

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "hw/xbox/mcpx/apu/dsp/gp-matrix-project.h"

static int check(bool condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "%s\n", message);
        return 1;
    }
    return 0;
}

int main(void)
{
    static const uint32_t expected[16] = {
        0xfff84e, 0x03f7fe, 0xfff7ae, 0x03f762, 0xfff712, 0x03f6c2,
        0xfff672, 0x03f622, 0xfff5d4, 0x03f586, 0xfff536, 0x03f4e6,
        0xfff496, 0x03f446, 0xfff3fa, 0x03f3aa,
    };
    DSPJitState before = { 0 }, after, untouched;
    before.cpu.pc = 0x6a2;
    before.cpu.pc_advance = 2;
    before.cpu.cycle_count = 100;
    before.cpu.registers[DSP_REG_OMR] = 0x300;
    before.cpu.registers[DSP_REG_SR] = 0xc083c0;
    before.cpu.registers[DSP_REG_SP] = 3;
    before.cpu.registers[DSP_REG_LA] = 0xabc;
    before.cpu.registers[DSP_REG_LC] = 0x12;
    before.cpu.registers[DSP_REG_R1] = 0x100;
    before.cpu.registers[DSP_REG_R3] = 0x180;
    before.cpu.registers[DSP_REG_R4] = 0x1c0;
    before.cpu.registers[DSP_REG_N0] = 16;
    for (unsigned i = 0; i < 5; i++) {
        before.cpu.registers[DSP_REG_M0 + i] = 0xffffff;
    }
    for (unsigned i = 0; i < 64; i++) {
        before.xram[0x100 + i] =
            ((i * 317 + 15 * 11 + 107) & 0xfffff) ^ ((i & 1) ? 0xf00000 : 0);
    }
    for (unsigned i = 0; i < 4; i++) {
        before.xram[0x180 + i] = 0xf80000;
    }
    int failed = 0;
    failed |=
        check(gp_matrix_project(&before, &after), "valid matrix rejected");
    failed |= check(!memcmp(after.xram + 0x1c0, expected, sizeof(expected)),
                    "matrix samples differ from JIT oracle");
    failed |= check(after.cpu.pc == 0x6ae && after.cpu.cycle_count == 457 &&
                        after.cpu.registers[DSP_REG_A1] == expected[15] &&
                        after.cpu.registers[DSP_REG_R1] == 0x110 &&
                        after.cpu.registers[DSP_REG_R4] == 0x1d0,
                    "matrix CPU projection differs");
    failed |= check(
        after.cpu.stack[0][4] == 0xabc && after.cpu.stack[1][4] == 0x12 &&
            after.cpu.stack[0][5] == 0x6a4 && after.cpu.stack[0][6] == 0x6ad &&
            after.cpu.stack[0][7] == 0x6a9,
        "nested loop stack projection differs");
    memset(&after, 0x5a, sizeof(after));
    untouched = after;
    before.cpu.registers[DSP_REG_R4] = 0x110;
    failed |= check(!gp_matrix_project(&before, &after) &&
                        !memcmp(&after, &untouched, sizeof(after)),
                    "overlap changed output");
    before.cpu.registers[DSP_REG_R4] = 0x1c0;
    before.xram[0x180] = 0x1000000;
    failed |= check(!gp_matrix_project(&before, &after) &&
                        !memcmp(&after, &untouched, sizeof(after)),
                    "invalid coefficient changed output");
    before.xram[0x180] = 0xf80000;
    before.cpu.registers[DSP_REG_SP] = 12;
    failed |= check(!gp_matrix_project(&before, &after) &&
                        !memcmp(&after, &untouched, sizeof(after)),
                    "unsafe nested stack changed output");
    return failed ? 1 : 0;
}

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "hw/xbox/mcpx/apu/dsp/gp-feedback-project.h"

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
        0x0619c6, 0xbe1b55, 0xeed3cf, 0xbcd0e2, 0xf5c7ad, 0xc6dc1e,
        0x00fbad, 0xd27f67, 0x0cc8b1, 0xde5c12, 0x18ab3b, 0xea40cf,
        0x2490cb, 0xf626ae, 0x3076c7, 0x020cb5,
    };
    DSPJitState before = { 0 }, after, untouched;
    before.cpu.pc = 0x788;
    before.cpu.cycle_count = 100;
    before.cpu.registers[DSP_REG_OMR] = 0x300;
    before.cpu.registers[DSP_REG_SR] = 0xc083c0;
    before.cpu.registers[DSP_REG_R0] = 0x100;
    before.cpu.registers[DSP_REG_R1] = 0x200;
    before.cpu.registers[DSP_REG_M0] = 0xffffff;
    before.cpu.registers[DSP_REG_SP] = 3;
    before.cpu.registers[DSP_REG_LA] = 0xabc;
    before.cpu.registers[DSP_REG_LC] = 0x12;
    before.xram[0x200] = 0x100000;
    before.xram[0x201] = 0x300000;
    before.xram[0x202] = 0x480000;
    for (unsigned i = 0; i < 16; i++) {
        before.xram[0x100 + i] =
            ((i * 433217 + 11731) & 0x7fffff) ^ ((i & 1) ? 0x800000 : 0);
    }
    int failed = 0;
    failed |=
        check(gp_feedback_project(&before, &after), "valid state rejected");
    failed |= check(!memcmp(after.xram + 0x100, expected, sizeof(expected)) &&
                        after.xram[0x200] == expected[15],
                    "feedback output differs from JIT oracle");
    failed |= check(after.cpu.pc == 0x792 && after.cpu.cycle_count == 237 &&
                        after.cpu.registers[DSP_REG_R0] == 0x110 &&
                        after.cpu.registers[DSP_REG_A1] == expected[15] &&
                        after.cpu.registers[DSP_REG_X0] == expected[14] &&
                        after.cpu.registers[DSP_REG_X1] == before.xram[0x10f],
                    "projected CPU state differs");
    failed |= check(
        after.cpu.stack[0][4] == 0xabc && after.cpu.stack[1][4] == 0x12 &&
            after.cpu.stack[0][5] == 0x78c && after.cpu.stack[1][5] == 0xc083c0,
        "loop stack projection differs");
    memset(&after, 0x5a, sizeof(after));
    untouched = after;
    before.cpu.registers[DSP_REG_M0] = 0;
    failed |= check(!gp_feedback_project(&before, &after) &&
                        !memcmp(&after, &untouched, sizeof(after)),
                    "unsupported state changed output");
    return failed ? 1 : 0;
}

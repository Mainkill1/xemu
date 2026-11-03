/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "hw/xbox/mcpx/apu/dsp/gp-add-project.h"

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
    /* Synthetic input evaluated by the DSP JIT. */
    static const uint32_t expected[16] = {
        0x0003be, 0xf005ce, 0xf007de, 0xe009ee, 0x000bfe, 0xf00e0e,
        0xf0101e, 0xe0122e, 0x00143e, 0xf0164e, 0xf0185e, 0xe01a6e,
        0x001c7e, 0xf01e8e, 0xf0209e, 0xe022ae,
    };
    DSPJitState before = { 0 }, after, untouched;
    before.cpu.pc = 0x4a0;
    before.cpu.cycle_count = 100;
    before.cpu.registers[DSP_REG_OMR] = 0x300;
    before.cpu.registers[DSP_REG_SR] = 0xc083c0;
    before.cpu.registers[DSP_REG_SP] = 3;
    before.cpu.registers[DSP_REG_LA] = 0xabc;
    before.cpu.registers[DSP_REG_LC] = 0x12;
    before.cpu.registers[DSP_REG_R0] = 0x100;
    before.cpu.registers[DSP_REG_R1] = 0x140;
    before.cpu.registers[DSP_REG_M0] = 0xffffff;
    before.cpu.registers[DSP_REG_M1] = 0xffffff;
    for (unsigned i = 0; i < 16; i++) {
        before.xram[0x100 + i] =
            ((i * 317 + 15 * 11 + 107) & 0xfffff) ^ ((i & 1) ? 0xf00000 : 0);
        before.xram[0x140 + i] =
            ((i * 211 + 15 * 17 + 431) & 0xfffff) ^ ((i & 2) ? 0xf00000 : 0);
    }
    int failed = 0;
    failed |=
        check(gp_add_project(&before, &after), "valid add state rejected");
    failed |= check(!memcmp(after.xram + 0x140, expected, sizeof(expected)),
                    "add samples differ from JIT oracle");
    failed |= check(after.cpu.pc == 0x4a6 && after.cpu.cycle_count == 169 &&
                        after.cpu.registers[DSP_REG_A1] == expected[15] &&
                        after.cpu.registers[DSP_REG_R0] == 0x110 &&
                        after.cpu.registers[DSP_REG_R1] == 0x150,
                    "add CPU projection differs");
    failed |= check(
        after.cpu.stack[0][4] == 0xabc && after.cpu.stack[1][4] == 0x12 &&
            after.cpu.stack[0][5] == 0x4a2 && after.cpu.stack[1][5] == 0xc083c0,
        "add loop stack projection differs");
    memset(&after, 0x5a, sizeof(after));
    untouched = after;
    before.cpu.registers[DSP_REG_R1] = 0x108;
    failed |= check(!gp_add_project(&before, &after) &&
                        !memcmp(&after, &untouched, sizeof(after)),
                    "overlapping add changed output");
    before.cpu.registers[DSP_REG_R1] = 0x140;
    before.xram[0x100] = before.xram[0x140] = 0x7fffff;
    failed |= check(!gp_add_project(&before, &after) &&
                        !memcmp(&after, &untouched, sizeof(after)),
                    "unqualified clipping changed output");
    before.xram[0x100] = 0x1000000;
    failed |= check(!gp_add_project(&before, &after) &&
                        !memcmp(&after, &untouched, sizeof(after)),
                    "invalid input changed output");
    return failed ? 1 : 0;
}

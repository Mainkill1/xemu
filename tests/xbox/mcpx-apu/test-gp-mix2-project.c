/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "hw/xbox/mcpx/apu/dsp/gp-mix2-project.h"

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
    static const uint32_t expected[32] = {
        0xffd380, 0x07bf00, 0x09ab00, 0x119680, 0x038180, 0x0b6d00, 0x0d5900,
        0x154400, 0xff2f80, 0x071b80, 0x090680, 0x10f200, 0x02dd80, 0x0ac980,
        0x0cb480, 0x14a000, 0xff0500, 0x02f500, 0x08e600, 0x0cd600, 0x02c680,
        0x06b700, 0x0ca780, 0x109800, 0xfe8880, 0x027900, 0x086980, 0x0c5980,
        0x024a00, 0x063b00, 0x0c2b00, 0x101b80,
    };
    DSPJitState before = { 0 }, after, untouched;
    before.cpu.pc = 0x761;
    before.cpu.cycle_count = 100;
    before.cpu.registers[DSP_REG_OMR] = 0x300;
    before.cpu.registers[DSP_REG_SR] = 0xc083c0;
    before.cpu.registers[DSP_REG_SP] = 3;
    before.cpu.registers[DSP_REG_LA] = 0xabc;
    before.cpu.registers[DSP_REG_LC] = 0x12;
    before.cpu.registers[DSP_REG_Y0] = 0xf60000;
    before.cpu.registers[DSP_REG_Y1] = 0xf80000;
    for (unsigned i = 0; i < 5; i++) {
        before.cpu.registers[DSP_REG_R0 + i] = 0x100 + 0x40 * i;
        before.cpu.registers[DSP_REG_M0 + i] = 0xffffff;
    }
    before.xram[0x100] = 0xf80000;
    before.xram[0x101] = 0xfc0000;
    for (unsigned i = 0; i < 32; i++) {
        before.xram[0x140 + i] =
            ((i * 317 + 107 + 15 * 11) & 0xffff) ^ ((i & 1) ? 0xff0000 : 0);
        before.xram[0x180 + i] =
            ((i * 211 + 431 + 15 * 17) & 0xffff) ^ ((i & 2) ? 0xff0000 : 0);
        before.xram[0x1c0 + i] =
            ((i * 151 + 229 + 15 * 23) & 0xffff) ^ ((i & 4) ? 0xff0000 : 0);
    }
    int failed = 0;
    failed |=
        check(gp_mix2_project(&before, &after), "valid mixer state rejected");
    failed |= check(!memcmp(after.xram + 0x200, expected, sizeof(expected)),
                    "mixer samples differ from JIT oracle");
    failed |= check(after.cpu.pc == 0x77b && after.cpu.cycle_count == 432 &&
                        after.cpu.registers[DSP_REG_A1] == expected[15] &&
                        after.cpu.registers[DSP_REG_B1] == expected[31] &&
                        after.cpu.registers[DSP_REG_R4] == 0x210,
                    "mixer CPU projection differs");
    failed |=
        check(after.cpu.stack[0][4] == 0xabc && after.cpu.stack[1][4] == 0x12 &&
                  after.cpu.stack[0][5] == 0x76a,
              "mixer loop stack projection differs");
    memset(&after, 0x5a, sizeof(after));
    untouched = after;
    before.cpu.registers[DSP_REG_M1] = 0;
    failed |= check(!gp_mix2_project(&before, &after) &&
                        !memcmp(&after, &untouched, sizeof(after)),
                    "unsupported mixer state changed output");
    return failed ? 1 : 0;
}

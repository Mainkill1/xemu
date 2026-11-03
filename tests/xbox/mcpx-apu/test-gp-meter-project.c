/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "hw/xbox/mcpx/apu/dsp/gp-meter-project.h"

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
    DSPJitState before = { 0 }, after, untouched;
    before.cpu.pc = 0x431;
    before.cpu.pc_advance = 1;
    before.cpu.cycle_count = 100;
    before.cpu.registers[DSP_REG_OMR] = 0x300;
    before.cpu.registers[DSP_REG_SR] = 0xc083c0;
    before.cpu.registers[DSP_REG_SP] = 3;
    before.cpu.registers[DSP_REG_LA] = 0xabc;
    before.cpu.registers[DSP_REG_LC] = 0x12;
    before.cpu.registers[DSP_REG_R4] = 0x100;
    before.cpu.registers[DSP_REG_R2] = 0x180;
    before.cpu.registers[DSP_REG_M4] = 0xffffff;
    before.xram[0x180] = 15 * 0x1000;
    for (unsigned i = 0; i < 32; i++) {
        before.xram[0x100 + i] = ((i * 201137 + 15 * 1009 + 107) & 0x7fffff) ^
                                 ((i & 1) ? 0x800000 : 0);
    }
    int failed = 0;
    failed |= check(gp_meter_project(&before, &after), "valid meter rejected");
    failed |= check(after.xram[0x180] == 0x7cb2c5 &&
                        after.cpu.registers[DSP_REG_A0] == 0x380000 &&
                        after.cpu.registers[DSP_REG_A1] == 0x3e7718 &&
                        after.cpu.registers[DSP_REG_B1] == 0x7cb2c5,
                    "meter result differs from JIT oracle");
    failed |= check(after.cpu.pc == 0x43e && after.cpu.cycle_count == 457 &&
                        after.cpu.registers[DSP_REG_R4] == 0x120 &&
                        after.cpu.stack[0][4] == 0xabc &&
                        after.cpu.stack[1][4] == 0x12 &&
                        after.cpu.stack[0][5] == 0x433,
                    "meter control state differs");
    memset(&after, 0x5a, sizeof(after));
    untouched = after;
    before.xram[0x101] = 0x800000;
    failed |= check(!gp_meter_project(&before, &after) &&
                        !memcmp(&after, &untouched, sizeof(after)),
                    "unqualified minimum value changed output");
    before.xram[0x101] = 0;
    before.cpu.registers[DSP_REG_R2] = 0x108;
    failed |= check(!gp_meter_project(&before, &after) &&
                        !memcmp(&after, &untouched, sizeof(after)),
                    "overlapping meter changed output");
    /* JIT-stepped fixture begins inside the meter DO loop. */
    memset(&before, 0, sizeof(before));
    before.cpu.pc = 0x433;
    before.cpu.pc_advance = 2;
    before.cpu.cycle_count = 27813;
    before.cpu.registers[DSP_REG_OMR] = 0x300;
    before.cpu.registers[DSP_REG_SR] = 0xc083d4;
    before.cpu.registers[DSP_REG_SP] = 7;
    before.cpu.registers[DSP_REG_SSH] = 0x433;
    before.cpu.registers[DSP_REG_SSL] = 0xc083d4;
    before.cpu.registers[DSP_REG_LA] = 0x43d;
    before.cpu.registers[DSP_REG_LC] = 16;
    before.cpu.registers[DSP_REG_A1] = 0xf000;
    before.cpu.registers[DSP_REG_A0] = 0xae15;
    before.cpu.registers[DSP_REG_B1] = 5;
    before.cpu.registers[DSP_REG_R4] = 0x100;
    before.cpu.registers[DSP_REG_R2] = 0x180;
    before.cpu.registers[DSP_REG_M4] = 0xffffff;
    before.cpu.stack[0][7] = 0x433;
    before.cpu.stack[1][7] = 0xc083d4;
    before.cpu.stack[0][6] = 0x42e;
    before.cpu.stack[1][6] = 6;
    before.cpu.stack[0][5] = 0x42d;
    before.cpu.stack[1][5] = 0xc083d4;
    before.xram[0x180] = 15 * 0x1000;
    for (unsigned i = 0; i < 32; i++) {
        before.xram[0x100 + i] = ((i * 201137 + 15 * 1009 + 107) & 0x7fffff) ^
                                 ((i & 1) ? 0x800000 : 0);
    }
    failed |= check(gp_meter_slice_project(&before, &after),
                    "valid meter slice rejected");
    failed |= check(after.cpu.pc == 0x43e && after.cpu.cycle_count == 27989 &&
                        after.cpu.registers[DSP_REG_A0] == 0xb80000 &&
                        after.cpu.registers[DSP_REG_A1] == 0x202b8b &&
                        after.cpu.registers[DSP_REG_B1] == 0x7cb2c5 &&
                        after.cpu.registers[DSP_REG_LC] == 6 &&
                        after.cpu.registers[DSP_REG_SP] == 5,
                    "meter slice differs from JIT oracle");
    memset(&after, 0x5a, sizeof(after));
    untouched = after;
    before.cpu.registers[DSP_REG_SSH] = 0;
    failed |= check(!gp_meter_slice_project(&before, &after) &&
                        !memcmp(&after, &untouched, sizeof(after)),
                    "invalid loop stack changed output");
    before.cpu.registers[DSP_REG_SSH] = 0x433;
    before.cpu.interrupts.pending_bits[0] = 1;
    failed |= check(!gp_meter_slice_project(&before, &after) &&
                        !memcmp(&after, &untouched, sizeof(after)),
                    "pending interrupt changed output");
    before.cpu.interrupts.pending_bits[0] = 0;
    before.cpu.registers[DSP_REG_LC] = 0;
    failed |= check(!gp_meter_slice_project(&before, &after) &&
                        !memcmp(&after, &untouched, sizeof(after)),
                    "empty loop changed output");
    before.cpu.registers[DSP_REG_LC] = 16;
    before.cpu.registers[DSP_REG_R2] = 0x108;
    failed |= check(!gp_meter_slice_project(&before, &after) &&
                        !memcmp(&after, &untouched, sizeof(after)),
                    "overlapping loop changed output");
    before.cpu.registers[DSP_REG_R2] = 0x180;
    before.cpu.registers[DSP_REG_A1] = 0x7fffff;
    failed |= check(!gp_meter_slice_project(&before, &after) &&
                        !memcmp(&after, &untouched, sizeof(after)),
                    "overflowing accumulator changed output");
    return failed ? 1 : 0;
}

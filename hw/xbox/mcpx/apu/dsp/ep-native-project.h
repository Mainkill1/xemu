/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_EP_NATIVE_PROJECT_H
#define XEMU_EP_NATIVE_PROJECT_H

#include "dsp-replay.h"
#include "ep-mix-hle.h"

/* Compute the observed state transition for one identified mixer program.
 * This does not validate the program, device state, or scratch overlay and
 * must not be used to serve output without those separate checks. */
static inline bool ep_native_project_frame(const DSPReplaySnapshot *before,
                                           const uint32_t input[5][256],
                                           DSPReplaySnapshot *after,
                                           uint8_t pcm[1024], uint32_t *calls)
{
    EPMixFrame frame;
    uint32_t regs[64], stack[2][16], cycles, predicted_calls;
    int32_t budget;
    if (!before || !input || !after || !pcm || !calls) {
        return false;
    }
    uint32_t phase = before->jit.pram[1];
    uint32_t slot = before->jit.pram[0x116];
    uint32_t counter = before->jit.pram[7];
    if (phase >= 6 || (slot != 0 && slot != 0x100) ||
        !ep_mix_frame_hle(input, &frame) ||
        !ep_mix_shadow_timing(phase, before->jit.cpu.cycle_budget, &cycles,
                              &predicted_calls, &budget)) {
        return false;
    }
    ep_mix_shadow_registers(before->jit.cpu.registers, counter, slot,
                            before->jit.cpu.stack[0][0],
                            before->jit.cpu.stack[1][0], &frame, regs);
    ep_mix_shadow_stack(before->jit.cpu.stack, phase, regs[57], stack);
    *after = *before;
    for (unsigned ch = 0; ch < 5; ch++) {
        memcpy(after->jit.xram + ch * 256, input[ch], sizeof(input[ch]));
    }
    for (unsigned ch = 0; ch < 2; ch++) {
        memcpy(after->jit.xram + (ch + 5) * 256, frame.mixed[ch],
               sizeof(frame.mixed[ch]));
    }
    after->jit.xram[0x701] = frame.peak[0];
    after->jit.xram[0x702] = frame.peak[1];
    after->jit.xram[0x703] = frame.rms[0];
    after->jit.xram[0x704] = frame.rms[1];
    after->jit.pram[1] = (phase + 1) % 6;
    after->jit.pram[7] = (counter + 1) & 0xffffff;
    after->jit.pram[0x116] = slot ^ 0x100;
    after->jit.pram[0x1a6] = 0x12000 + slot * 4;
    after->jit.pram[0x1ab] = frame.peak[0];
    after->jit.pram[0x1ac] = frame.peak[1];
    after->jit.pram[0x1ad] = frame.rms[0];
    after->jit.pram[0x1ae] = frame.rms[1];
    memcpy(after->jit.cpu.registers, regs, sizeof(regs));
    memcpy(after->jit.cpu.stack, stack, sizeof(stack));
    after->jit.cpu.cycle_count = cycles;
    after->jit.cpu.cycle_budget = budget;
    after->jit.cpu.halt_requested = true;
    after->interrupts = 0;
    memcpy(pcm, frame.pcm, sizeof(frame.pcm));
    *calls = predicted_calls;
    return true;
}
#endif

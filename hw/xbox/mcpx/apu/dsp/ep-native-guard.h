/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_EP_NATIVE_GUARD_H
#define XEMU_EP_NATIVE_GUARD_H

#include "dsp-replay.h"

/* Conservative entry-state filter for the one measured EP program. This is
 * not a program identity check and does not authorize live execution. */
static inline bool ep_native_entry_guard(const DSPReplaySnapshot *s)
{
    static const uint32_t regs[64] = {
        [4] = 0xacda0,   [5] = 1,         [13] = 1,        [16] = 0x300,
        [18] = 0x117,    [20] = 0x1a2,    [21] = 0x1a6,    [22] = 0x700,
        [24] = 4,        [26] = 11,       [29] = 1,        [32] = 0xffffff,
        [33] = 0xffffff, [34] = 0xffffff, [35] = 0xffffff, [36] = 0xffffff,
        [37] = 0xffffff, [38] = 0xffffff, [39] = 0xffffff, [42] = 0xbe0,
        [56] = 0x20,     [57] = 0xc00390, [58] = 0x100300,
    };
    static const uint32_t stack_pc[5] = { 182, 968, 0, 485, 750 };
    if (!s) {
        return false;
    }
    const Dsp56300State *cpu = &s->jit.cpu;
    uint32_t phase = s->jit.pram[1], slot = s->jit.pram[0x116];
    if (phase >= 6 || (slot != 0 && slot != 0x100) ||
        s->jit.pram[7] > 0xffffff ||
        s->jit.pram[0x1a6] != 0x12000 + (slot ^ 0x100) * 4 || cpu->pc != 48 ||
        cpu->pc_advance != 2 || cpu->pc_on_rep || cpu->loop_rep ||
        cpu->halt_requested || cpu->power_state || cpu->cycle_count ||
        s->interrupts != 2 || s->save_cycles || s->dma.configuration ||
        s->dma.control != 16 || s->dma.start_block != 16384 ||
        s->dma.next_block != 16802 || s->dma.dma_read_count != 1 ||
        s->dma.error || s->dma.eol || cpu->interrupts.pending_bits[0] ||
        cpu->interrupts.pending_bits[1] || cpu->interrupts.pipeline_stage ||
        cpu->interrupts.vector_addr != 0xffff ||
        cpu->interrupts.saved_pc != 0xffff || cpu->interrupts.state ||
        cpu->interrupts.ipl_to_raise) {
        return false;
    }
    for (unsigned i = 0; i < 64; i++) {
        if (i != 6 && i != 7 && i != 12 && i != 17 &&
            cpu->registers[i] != regs[i]) {
            return false;
        }
    }
    if (cpu->registers[17] != 0 && cpu->registers[17] != 0x100) {
        return false;
    }
    for (unsigned i = 0; i < 128; i++) {
        int expected = i == 0 || i == 1 || i == 2 || i == 4 ? 3 : -1;
        if (cpu->interrupts.ipl[i] != expected) {
            return false;
        }
    }
    for (unsigned i = 0; i < 4; i++) {
        if (s->jit.pram[0x1ab + i] != s->jit.xram[0x701 + i]) {
            return false;
        }
    }
    for (unsigned i = 0; i < 16; i++) {
        uint32_t pc = i >= 1 && i <= 5 ? stack_pc[i - 1] : 0;
        uint32_t sr = i == 1           ? (phase == 0 ? 0xc00394 : 0xc00399) :
                      i == 2 || i == 4 ? 0xc00394 :
                      i == 5           ? 0xc00390 :
                                         0;
        if (cpu->stack[0][i] != pc || cpu->stack[1][i] != sr) {
            return false;
        }
    }
    return true;
}
#endif

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "dsp-capture-compare.h"

/* Compare values, not structure padding or host byte representation. */
#define CHECK(name, i, left, right)                                  \
    do {                                                             \
        uint64_t l_ = (left), r_ = (right);                          \
        if (l_ != r_) {                                              \
            *difference = (DspCaptureDifference){ name, i, l_, r_ }; \
            return false;                                            \
        }                                                            \
    } while (0)
#define FIELD(f) CHECK(#f, 0, expected->f, actual->f)

bool dsp_capture_equal(const DspJitCapture *expected,
                       const DspJitCapture *actual,
                       DspCaptureDifference *difference)
{
    *difference = (DspCaptureDifference){ 0 };
    FIELD(state.pc);
    FIELD(state.pc_advance);
    FIELD(state.pc_on_rep);
    FIELD(state.cycle_count);
    FIELD(state.cycle_budget);
    FIELD(state.loop_rep);
    FIELD(state.halt_requested);
    FIELD(state.power_state);
    FIELD(state.interrupts.pipeline_stage);
    FIELD(state.interrupts.vector_addr);
    FIELD(state.interrupts.saved_pc);
    FIELD(state.interrupts.state);
    FIELD(state.interrupts.ipl_to_raise);
    for (size_t i = 0; i < 2; i++) {
        CHECK("interrupt.pending_bits", i,
              expected->state.interrupts.pending_bits[i],
              actual->state.interrupts.pending_bits[i]);
    }
    for (size_t i = 0; i < 128; i++) {
        CHECK("interrupt.ipl", i, expected->state.interrupts.ipl[i],
              actual->state.interrupts.ipl[i]);
    }
    for (size_t i = 0; i < 64; i++) {
        CHECK("registers", i, expected->state.registers[i],
              actual->state.registers[i]);
    }
    for (size_t bank = 0; bank < 2; bank++) {
        for (size_t i = 0; i < 16; i++) {
            CHECK("stack", bank * 16 + i, expected->state.stack[bank][i],
                  actual->state.stack[bank][i]);
        }
    }
    for (size_t i = 0; i < DSP_XRAM_SIZE; i++) {
        CHECK("xram", i, expected->xram[i], actual->xram[i]);
    }
    for (size_t i = 0; i < DSP_YRAM_SIZE; i++) {
        CHECK("yram", i, expected->yram[i], actual->yram[i]);
    }
    for (size_t i = 0; i < DSP_PRAM_SIZE; i++) {
        CHECK("pram", i, expected->pram[i], actual->pram[i]);
    }
    FIELD(configuration);
    FIELD(control);
    FIELD(start_block);
    FIELD(next_block);
    FIELD(dma_read_count);
    FIELD(interrupts);
    FIELD(error);
    FIELD(eol);
    FIELD(is_gp);
    return true;
}

bool dsp_c_capture_equal(const DspCCapture *expected, const DspCCapture *actual,
                         DspCaptureDifference *difference)
{
    *difference = (DspCaptureDifference){ 0 };
    FIELD(core.pc);
    FIELD(core.cycle_count);
    FIELD(core.instr_cycle);
    FIELD(core.halt_requested);
    FIELD(core.is_gp);
    FIELD(core.loop_rep);
    FIELD(core.pc_on_rep);
    FIELD(core.cur_inst);
    FIELD(core.cur_inst_len);
    FIELD(core.interrupt_state);
    FIELD(core.interrupt_instr_fetch);
    FIELD(core.interrupt_save_pc);
    FIELD(core.interrupt_counter);
    FIELD(core.interrupt_ipl_to_raise);
    FIELD(core.interrupt_pipeline_count);
    for (size_t i = 0; i < G_N_ELEMENTS(expected->core.interrupt_ipl); i++) {
        CHECK("core.interrupt_ipl", i, expected->core.interrupt_ipl[i],
              actual->core.interrupt_ipl[i]);
        CHECK("core.interrupt_is_pending", i,
              expected->core.interrupt_is_pending[i],
              actual->core.interrupt_is_pending[i]);
    }
    for (size_t i = 0; i < DSP_REG_MAX; i++) {
        CHECK("core.registers", i, expected->core.registers[i],
              actual->core.registers[i]);
    }
    for (size_t bank = 0; bank < 2; bank++) {
        for (size_t i = 0; i < 16; i++) {
            CHECK("core.stack", bank * 16 + i, expected->core.stack[bank][i],
                  actual->core.stack[bank][i]);
        }
    }
#define CHECK_ARRAY(member)                                                \
    do {                                                                   \
        for (size_t i = 0; i < G_N_ELEMENTS(expected->core.member); i++) { \
            CHECK("core." #member, i, expected->core.member[i],            \
                  actual->core.member[i]);                                 \
        }                                                                  \
    } while (0)
    CHECK_ARRAY(xram);
    CHECK_ARRAY(yram);
    CHECK_ARRAY(pram);
    CHECK_ARRAY(mixbuffer);
    CHECK_ARRAY(periph);
#undef CHECK_ARRAY
    FIELD(save_cycles);
    FIELD(exception_debugging);
    FIELD(configuration);
    FIELD(control);
    FIELD(start_block);
    FIELD(next_block);
    FIELD(dma_read_count);
    FIELD(interrupts);
    FIELD(error);
    FIELD(eol);
    FIELD(is_gp);
    return true;
}

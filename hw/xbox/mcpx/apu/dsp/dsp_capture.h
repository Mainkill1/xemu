/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_DSP_CAPTURE_H
#define XEMU_DSP_CAPTURE_H

#include "dsp.h"
#include <dsp56300.h>

/* Native, in-process JIT checkpoint. This is not a portable disk format and
 * does not include guest RAM, FIFO state, or the containing APU. The caller
 * must quiesce the DSP and own its execution context for both operations. */
typedef struct DspJitCapture {
    Dsp56300State state;
    uint32_t xram[DSP_XRAM_SIZE];
    uint32_t yram[DSP_YRAM_SIZE];
    uint32_t pram[DSP_PRAM_SIZE];
    uint32_t configuration, control, start_block, next_block;
    uint32_t dma_read_count, interrupts;
    bool error, eol, is_gp;
} DspJitCapture;

bool dsp_jit_capture(DSPState *dsp, DspJitCapture *capture);
bool dsp_jit_restore(DSPState *dsp, const DspJitCapture *capture);

/* Interpreter-native checkpoint; its core state is not interchangeable with
 * the JIT state and is not a serialized format. */
typedef struct DspCCapture {
    DspCoreState core;
    int save_cycles;
    uint32_t configuration, control, start_block, next_block;
    uint32_t dma_read_count, interrupts;
    bool error, eol, is_gp, exception_debugging;
} DspCCapture;

bool dsp_c_capture(DSPState *dsp, DspCCapture *capture);
bool dsp_c_restore(DSPState *dsp, const DspCCapture *capture);

#endif

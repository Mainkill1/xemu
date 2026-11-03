/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef MCPX_DSP_JIT_STATE_H
#define MCPX_DSP_JIT_STATE_H

#include <dsp56300.h>
#include "dsp.h"

/* Native debug/replay state, independent of the legacy VMState layout. */
typedef struct DSPJitState {
    Dsp56300State cpu;
    uint32_t xram[DSP_XRAM_SIZE];
    uint32_t yram[DSP_YRAM_SIZE];
    uint32_t pram[DSP_PRAM_SIZE];
} DSPJitState;

/*
 * Caller must own/quiesce the DSP. These functions perform no peripheral I/O.
 */
bool dsp_jit_get_native_state(DSPState *dsp, DSPJitState *state);
bool dsp_jit_set_native_state(DSPState *dsp, const DSPJitState *state);
/* Reject program changes before writing anything; retain compiled code. */
bool dsp_jit_set_native_data_state(DSPState *dsp, const DSPJitState *state);

#endif

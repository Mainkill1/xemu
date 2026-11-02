/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_DSP_REPLAY_DUAL_H
#define XEMU_DSP_REPLAY_DUAL_H

#include "dsp-replay-core.h"

typedef struct MCPXAPUState MCPXAPUState;
typedef enum DspDualFixture {
    DSP_DUAL_SCRATCH,
    DSP_DUAL_FIFO,
    DSP_DUAL_EP_SINK,
} DspDualFixture;
typedef enum DspDualSignal {
    DSP_SIGNAL_IMPULSE,
    DSP_SIGNAL_SILENCE,
    DSP_SIGNAL_ALTERNATING,
    DSP_SIGNAL_RAMP,
    DSP_SIGNAL_LEFT,
    DSP_SIGNAL_RIGHT,
    DSP_SIGNAL_EXTREMES,
    DSP_SIGNAL_NEAR_ZERO,
    DSP_SIGNAL_DETERMINISTIC_NOISE,
    DSP_SIGNAL_SWEPT_SINE,
} DspDualSignal;

typedef struct DspDualResult {
    const char *field, *component;
    size_t frame, index, byte_offset, io_events, pcm_nonzero_comparisons;
    size_t dsp_memory_word_comparisons;
    size_t architectural_comparisons;
    size_t native_checkpoint_comparisons;
    uint64_t sample;
    uint64_t expected, actual;
    bool post_subframe_pc_valid;
    bool initial_subset_checked, initial_state_mismatch;
    uint32_t post_subframe_pc[4];
} DspDualResult;

/* Private, single-threaded QEMU address space only. Each engine has separate
 * DSP/APU state and its own RAM image. This supports only the fixed synthetic
 * programs; it does not convert arbitrary backend checkpoints. */
DspReplayStatus
dsp_dual_synthetic(MCPXAPUState *memory_owner, DspDualFixture fixture,
                   DspDualSignal signal, unsigned frames, uint32_t value,
                   bool inject, bool inject_pcm, bool inject_request,
                   bool inject_memory, bool inject_register,
                   bool inject_both_skip_dma, bool inject_both_pcm_silence,
                   bool inject_both_corrupt_event, bool inject_both_corrupt_ram,
                   bool inject_architectural, bool inject_initial_state,
                   unsigned inject_address_reg, unsigned inject_stack,
                   unsigned inject_native_interrupt, bool reference_jit,
                   bool candidate_jit, DspDualResult *result);
#endif

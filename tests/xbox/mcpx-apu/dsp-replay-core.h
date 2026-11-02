/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_DSP_REPLAY_CORE_H
#define XEMU_DSP_REPLAY_CORE_H

#include "hw/xbox/mcpx/apu/dsp/dsp_capture.h"
#include "dsp-replay-io.h"

typedef enum DspReplayStatus {
    DSP_REPLAY_HALTED, /* execution stopped; caller must still compare outputs
                        */
    DSP_REPLAY_TIMEOUT,
    DSP_REPLAY_IO_MISMATCH,
    DSP_REPLAY_STATE_MISMATCH,
    DSP_REPLAY_UNSUPPORTED,
    DSP_REPLAY_INVALID,
} DspReplayStatus;

typedef struct DspReplayMemory {
    uint8_t *memory;
    size_t size;
    DspIoLog *io;
    DspReplayStatus error;
    uint32_t scratch_index;
} DspReplayMemory;

void dsp_replay_scratch(void *opaque, uint8_t *data, uint32_t addr,
                        size_t length, bool write);
void dsp_replay_fifo(void *opaque, uint8_t *data, unsigned index, size_t length,
                     bool write);

/* Single-threaded, isolated test process only. A step can enter the production
 * DMA handler, so a caller must also enforce an external process timeout.
 * Outputs are published only after halt and complete transcript consumption. */
DspReplayStatus dsp_replay_execute(const DspJitCapture *initial,
                                   const uint8_t *memory, size_t memory_size,
                                   DspIoLog *io, unsigned max_steps,
                                   DspJitCapture *result,
                                   uint8_t *result_memory);

#endif

/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_DSP_REPLAY_FRAME_H
#define XEMU_DSP_REPLAY_FRAME_H

#include "dsp-replay-core.h"
#include "hw/xbox/mcpx/apu/apu_regs.h"

typedef struct DspFrameResult {
    DspJitCapture gp, ep;
    int16_t pcm[256][2];
} DspFrameResult;

typedef struct DspFrameMemory {
    const uint8_t *gp, *ep;
    size_t gp_size, ep_size;
    uint8_t *gp_result, *ep_result;
} DspFrameMemory;

typedef struct MCPXAPUState MCPXAPUState;
typedef bool (*DspFrameCheck)(void *opaque, size_t frame,
                              const DspJitCapture *gp, const DspJitCapture *ep,
                              const int16_t pcm[256][2]);

/* Native test-only container for the registers used by production GP/EP
 * callbacks. This is not a portable or complete whole-APU checkpoint. */
typedef struct DspMappedFrame {
    DspFrameResult dsp;
    uint32_t apu_regs[0x20000];
    uint32_t gp_reset, ep_reset;
    bool gp_realtime, ep_realtime;
    int monitor_point;
    unsigned phase;
} DspMappedFrame;

/* The caller supplies a private, initialized APU and a RAM region which is
 * the only mapping in address_space_memory. The BQL and APU execution
 * context must be exclusively owned. This routine mutates that private APU
 * and RAM; it must never receive a live guest. Malformed FIFO/SGE values may
 * assert or hang in production code, so run in a resource-limited process. */
DspReplayStatus
dsp_replay_mapped_frame(MCPXAPUState *apu, const DspMappedFrame *initial,
                        const float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME],
                        const uint8_t *ram, size_t ram_size,
                        DspMappedFrame *result, uint8_t *result_ram);

/* Runs 1..1024 adjacent GP subframes in one private APU and shared RAM.
 * Captures the full 256-sample monitor buffer after each subframe. The final
 * phase is the next phase after the sequence. Caller-visible output buffers
 * must not overlap; no output is published on failure. The private APU/RAM
 * are mutated even on failure. A watchdog is required, as for one frame. */
DspReplayStatus dsp_replay_mapped_sequence(
    MCPXAPUState *apu, const DspMappedFrame *initial, size_t count,
    const float (*mixbins)[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME],
    const uint8_t *ram, size_t ram_size, DspMappedFrame *result,
    uint8_t *result_ram, int16_t (*pcm)[256][2], size_t *failed_frame);

/* As above, additionally records or compares ordered production scratch and
 * FIFO callback transfers. The caller owns a bounded log; in replay mode the
 * actual read data is compared, never replaced by recorded input. Sample
 * indices identify subframe boundaries, not exact intra-frame timing. */
DspReplayStatus dsp_replay_mapped_trace(
    MCPXAPUState *apu, const DspMappedFrame *initial, size_t count,
    const float (*mixbins)[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME],
    const uint8_t *ram, size_t ram_size, DspMappedFrame *result,
    uint8_t *result_ram, int16_t (*pcm)[256][2], size_t *failed_frame,
    DspIoLog *io);

/* Additionally check the captured GP/EP state after every subframe, before
 * continuing execution. On mismatch no caller output buffers are published. */
DspReplayStatus dsp_replay_mapped_trace_checked(
    MCPXAPUState *apu, const DspMappedFrame *initial, size_t count,
    const float (*mixbins)[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME],
    const uint8_t *ram, size_t ram_size, DspMappedFrame *result,
    uint8_t *result_ram, int16_t (*pcm)[256][2], size_t *failed_frame,
    DspIoLog *io, DspFrameCheck check, void *opaque);

/* One GP subframe and, at phase zero, one EP frame. This narrow adapter
 * selects GP monitoring and both DSPs enabled, with independent synthetic
 * scratch memories. Output regions must not overlap. This is not a full APU
 * snapshot or an independent hardware oracle. */
DspReplayStatus dsp_replay_gp_ep_frame(
    const DspJitCapture *gp, const DspJitCapture *ep, unsigned phase,
    const float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME],
    const int16_t prior_pcm[256][2], const DspFrameMemory *memory, DspIoLog *io,
    DspFrameResult *result);

/* Stateful sequence of 1..1024 GP subframes with a transcript per subframe.
 * Inputs and output regions must not overlap. A failure leaves all state,
 * PCM, and memory outputs untouched; logs up to the failure may be consumed.
 * On a frame failure, *failed_frame is its zero-based index. */
DspReplayStatus dsp_replay_gp_ep_sequence(
    const DspJitCapture *gp, const DspJitCapture *ep, unsigned phase,
    size_t count, const float (*mixbins)[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME],
    const int16_t prior_pcm[256][2], const DspFrameMemory *memory,
    DspIoLog *logs, DspFrameResult *result,
    int16_t (*pcm)[NUM_SAMPLES_PER_FRAME][2], size_t *failed_frame);

#endif

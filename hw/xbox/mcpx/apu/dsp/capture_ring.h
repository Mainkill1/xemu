/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_DSP_CAPTURE_RING_H
#define XEMU_DSP_CAPTURE_RING_H

#include "dsp_capture.h"
#include "hw/xbox/mcpx/apu/apu_regs.h"

/* Native handoff between one APU producer and one serializer consumer.
 * These layouts are not a file format and must not be written directly. */
#define DSP_CAPTURE_MAX_EVENTS 1024
#define DSP_CAPTURE_PAYLOAD_BYTES (256 * 1024)
#define DSP_CAPTURE_MAX_SLOTS 8

enum {
    DSP_CAPTURE_SCRATCH = 0,
    DSP_CAPTURE_FIFO = 1,
    DSP_CAPTURE_RAM_PREWRITE = 2,
    DSP_CAPTURE_RAM_READ = 3,
    DSP_CAPTURE_RAM_WRITE = 4,
    DSP_CAPTURE_SGE_READ = 5,
};

enum {
    DSP_CAPTURE_EXTERNAL_UNVERIFIED = 1u << 0,
    DSP_CAPTURE_TRUNCATED = 1u << 1,
    DSP_CAPTURE_UNSUPPORTED_BACKEND = 1u << 2,
    DSP_CAPTURE_DROPPED_BEFORE = 1u << 3,
    DSP_CAPTURE_INCOMPLETE_BOUNDARY = 1u << 4,
    DSP_CAPTURE_DISCONTINUITY = 1u << 5,
    DSP_CAPTURE_PROGRAM_DIFFERENT = 1u << 6,
    DSP_CAPTURE_NONRAM_DESCRIPTOR = 1u << 7,
    DSP_CAPTURE_MAPPING_OBSERVED = 1u << 8,
};

typedef struct DspCaptureEvent {
    uint32_t kind, index, address;
    uint64_t sample;
    uint32_t offset, length;
    bool write;
} DspCaptureEvent;

typedef struct DspCaptureFrame {
    uint64_t frame_id;
    uint32_t flags, phase, dropped_before;
    uint32_t gp_reset, ep_reset, monitor_point;
    bool gp_realtime, ep_realtime;
    bool started, finished;
    DspJitCapture gp_before, ep_before, gp_after, ep_after;
    uint32_t apu_regs[0x20000];
    uint32_t apu_regs_after[0x20000];
    float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME];
    int16_t pcm_before[256][2], pcm_after[256][2];
    uint32_t event_count, payload_size;
    DspCaptureEvent events[DSP_CAPTURE_MAX_EVENTS];
    uint8_t payload[DSP_CAPTURE_PAYLOAD_BYTES];
} DspCaptureFrame;

typedef struct DspCaptureRing DspCaptureRing;
typedef struct MCPXAPUState MCPXAPUState;

/* These operations require APU execution ownership. They do not capture
 * guest RAM or make a frame complete; no filesystem access occurs here. */
bool dsp_capture_frame_start(
    DspCaptureFrame *frame, MCPXAPUState *apu,
    const float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME]);
bool dsp_capture_frame_finish(DspCaptureFrame *frame, MCPXAPUState *apu);
/* Call under the APU lock. A full ring drops the capture but executes the
 * frame once; allocation, serialization and consumer work happen elsewhere. */
void mcpx_apu_capture_dsp_frame(
    MCPXAPUState *apu, DspCaptureRing *ring, uint64_t frame_id,
    float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME]);
#ifdef CONFIG_XEMU_DSP_CAPTURE
/* Owner holds the APU lock and keeps the ring alive until after detachment. */
void mcpx_apu_capture_set_ring_locked(MCPXAPUState *apu, DspCaptureRing *ring);
#endif

/* Capacity must be a power of two so counter wrap preserves slot order.
 * Allocate before capture begins. Destroy only after both owners stop. */
DspCaptureRing *dsp_capture_ring_new(unsigned capacity);
void dsp_capture_ring_free(DspCaptureRing *ring);
DspCaptureFrame *dsp_capture_ring_begin(DspCaptureRing *ring,
                                        uint64_t frame_id);
/* Producer-side; the pending marker survives full-queue drops. */
void dsp_capture_ring_mark_discontinuity(DspCaptureRing *ring);
bool dsp_capture_frame_event(DspCaptureFrame *frame, uint32_t kind,
                             uint32_t index, uint32_t address, bool write,
                             uint64_t sample, const uint8_t *bytes,
                             size_t length);
void dsp_capture_ring_publish(DspCaptureRing *ring);
const DspCaptureFrame *dsp_capture_ring_peek(DspCaptureRing *ring);
void dsp_capture_ring_release(DspCaptureRing *ring);
uint32_t dsp_capture_ring_drops(const DspCaptureRing *ring);

#endif

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/atomic.h"
#include "hw/xbox/mcpx/apu/apu_int.h"
#include "capture_ring.h"
#include "capture_stream.h"

struct DspCaptureSession {
    DspCaptureRing *ring;
    DspCaptureWriter *writer;
    uint64_t attempts;
    uint64_t elapsed;
    unsigned skip;
    unsigned limit;
    bool active;
};

typedef struct FrameCaptureIo {
    DspCaptureFrame *frame;
    void *opaque;
    dsp_scratch_rw_func scratch;
    dsp_fifo_rw_func fifo;
    uint64_t sample;
    unsigned unit;
} FrameCaptureIo;

static void capture_scratch(void *opaque, uint8_t *bytes, uint32_t address,
                            size_t length, bool write)
{
    FrameCaptureIo *io = opaque;
    if (write) {
        dsp_capture_frame_event(io->frame, DSP_CAPTURE_SCRATCH, io->unit,
                                address, true, io->sample, bytes, length);
    }
    io->scratch(io->opaque, bytes, address, length, write);
    if (!write) {
        dsp_capture_frame_event(io->frame, DSP_CAPTURE_SCRATCH, io->unit,
                                address, false, io->sample, bytes, length);
    }
}

static void capture_fifo(void *opaque, uint8_t *bytes, unsigned index,
                         size_t length, bool write)
{
    FrameCaptureIo *io = opaque;
    uint32_t event_index = (io->unit << 16) | index;
    if (write) {
        dsp_capture_frame_event(io->frame, DSP_CAPTURE_FIFO, event_index, 0,
                                true, io->sample, bytes, length);
    }
    io->fifo(io->opaque, bytes, index, length, write);
    if (!write) {
        dsp_capture_frame_event(io->frame, DSP_CAPTURE_FIFO, event_index, 0,
                                false, io->sample, bytes, length);
    }
}

bool dsp_capture_frame_start(
    DspCaptureFrame *frame, MCPXAPUState *apu,
    const float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME])
{
    if (frame && (frame->started || frame->finished)) {
        frame->flags |= DSP_CAPTURE_INCOMPLETE_BOUNDARY;
        return false;
    }
    if (!frame || !apu || !mixbins || !apu->gp.dsp || !apu->ep.dsp ||
        apu->ep_frame_div < 0) {
        if (frame) {
            frame->flags |= DSP_CAPTURE_UNSUPPORTED_BACKEND;
        }
        return false;
    }
    frame->phase = apu->ep_frame_div % 8;
#ifdef CONFIG_XEMU_DSP_CAPTURE
    frame->flags |= DSP_CAPTURE_MAPPING_OBSERVED;
#endif
    frame->gp_reset = apu->gp.regs[NV_PAPU_GPRST];
    frame->ep_reset = apu->ep.regs[NV_PAPU_EPRST];
    frame->monitor_point = apu->monitor.point;
    frame->gp_realtime = apu->gp.realtime;
    frame->ep_realtime = apu->ep.realtime;
    /* Some APU MMIO registers are written atomically without the APU lock.
     * Individual reads avoid a data race; they are not a coherent snapshot. */
    for (size_t i = 0; i < G_N_ELEMENTS(frame->apu_regs); i++) {
        frame->apu_regs[i] = qatomic_read(&apu->regs[i]);
    }
    memcpy(frame->mixbins, mixbins, sizeof(frame->mixbins));
    memcpy(frame->pcm_before, apu->monitor.frame_buf,
           sizeof(frame->pcm_before));
    if (!dsp_jit_capture(apu->gp.dsp, &frame->gp_before) ||
        !dsp_jit_capture(apu->ep.dsp, &frame->ep_before)) {
        frame->flags |= DSP_CAPTURE_UNSUPPORTED_BACKEND;
        return false;
    }
    frame->started = true;
    return true;
}

bool dsp_capture_frame_finish(DspCaptureFrame *frame, MCPXAPUState *apu)
{
    if (!frame || !apu || !apu->gp.dsp || !apu->ep.dsp || !frame->started ||
        frame->finished) {
        if (frame) {
            frame->flags |= DSP_CAPTURE_INCOMPLETE_BOUNDARY;
        }
        return false;
    }
    for (size_t i = 0; i < G_N_ELEMENTS(frame->apu_regs_after); i++) {
        frame->apu_regs_after[i] = qatomic_read(&apu->regs[i]);
    }
    memcpy(frame->pcm_after, apu->monitor.frame_buf, sizeof(frame->pcm_after));
    if (!dsp_jit_capture(apu->gp.dsp, &frame->gp_after) ||
        !dsp_jit_capture(apu->ep.dsp, &frame->ep_after)) {
        frame->flags |= DSP_CAPTURE_UNSUPPORTED_BACKEND;
        return false;
    }
    frame->finished = true;
    if (memcmp(frame->gp_before.pram, frame->gp_after.pram,
               sizeof(frame->gp_before.pram)) ||
        memcmp(frame->ep_before.pram, frame->ep_after.pram,
               sizeof(frame->ep_before.pram))) {
        frame->flags |= DSP_CAPTURE_PROGRAM_DIFFERENT;
    }
    return !(frame->flags &
             (DSP_CAPTURE_TRUNCATED | DSP_CAPTURE_UNSUPPORTED_BACKEND |
              DSP_CAPTURE_INCOMPLETE_BOUNDARY));
}

void mcpx_apu_capture_dsp_frame(
    MCPXAPUState *apu, DspCaptureRing *ring, uint64_t frame_id,
    float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME])
{
    DspCaptureFrame *frame =
        ring ? dsp_capture_ring_begin(ring, frame_id) : NULL;
    FrameCaptureIo contexts[2] = { 0 };
    bool installed = frame && dsp_capture_frame_start(frame, apu, mixbins);
    if (installed) {
#ifdef CONFIG_XEMU_DSP_CAPTURE
        g_assert(!apu->capture_active_frame);
        apu->capture_active_frame = frame;
#endif
        if (frame_id > UINT64_MAX / NUM_SAMPLES_PER_FRAME) {
            frame->flags |= DSP_CAPTURE_TRUNCATED;
        }
        DSPState *units[2] = { apu->gp.dsp, apu->ep.dsp };
        for (unsigned i = 0; i < 2; i++) {
            contexts[i] =
                (FrameCaptureIo){ .frame = frame,
                                  .opaque = units[i]->dma.rw_opaque,
                                  .scratch = units[i]->dma.scratch_rw,
                                  .fifo = units[i]->dma.fifo_rw,
                                  .sample = frame_id * NUM_SAMPLES_PER_FRAME,
                                  .unit = i };
            units[i]->dma.rw_opaque = &contexts[i];
            units[i]->dma.scratch_rw = capture_scratch;
            units[i]->dma.fifo_rw = capture_fifo;
        }
    }
    mcpx_apu_dsp_frame(apu, mixbins);
    if (installed) {
#ifdef CONFIG_XEMU_DSP_CAPTURE
        apu->capture_active_frame = NULL;
#endif
        DSPState *units[2] = { apu->gp.dsp, apu->ep.dsp };
        for (unsigned i = 0; i < 2; i++) {
            units[i]->dma.rw_opaque = contexts[i].opaque;
            units[i]->dma.scratch_rw = contexts[i].scratch;
            units[i]->dma.fifo_rw = contexts[i].fifo;
        }
    }
    if (frame) {
        dsp_capture_frame_finish(frame, apu);
        dsp_capture_ring_publish(ring);
    }
}

DspCaptureSession *dsp_capture_session_start(const char *path, unsigned frames)
{
    if (!path || !*path || !frames || frames > 1024) {
        return NULL;
    }
    DspCaptureSession *session = g_try_new0(DspCaptureSession, 1);
    if (!session) {
        return NULL;
    }
    session->ring = dsp_capture_ring_new(DSP_CAPTURE_MAX_SLOTS);
    if (!session->ring) {
        g_free(session);
        return NULL;
    }
    session->writer = dsp_capture_writer_start(session->ring, path);
    if (!session->writer) {
        dsp_capture_ring_free(session->ring);
        g_free(session);
        return NULL;
    }
    session->limit = frames;
    session->active = true;
    return session;
}

bool dsp_capture_session_active(const DspCaptureSession *session)
{
    return session && session->active;
}

bool dsp_capture_session_set_skip(DspCaptureSession *session, unsigned frames)
{
    if (!session || !session->active || session->elapsed || frames > 1000000) {
        return false;
    }
    session->skip = frames;
    return true;
}

DspCaptureRing *dsp_capture_session_ring(DspCaptureSession *session)
{
    return session ? session->ring : NULL;
}

void mcpx_apu_capture_session_frame(
    MCPXAPUState *apu, DspCaptureSession *session,
    float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME])
{
    if (!session || !session->active) {
        mcpx_apu_dsp_frame(apu, mixbins);
        return;
    }
    if (dsp_capture_writer_failed(session->writer)) {
        session->active = false;
        dsp_capture_writer_finish_async(session->writer);
        mcpx_apu_dsp_frame(apu, mixbins);
        return;
    }
    if (session->elapsed < session->skip) {
        session->elapsed++;
        mcpx_apu_dsp_frame(apu, mixbins);
        return;
    }
    mcpx_apu_capture_dsp_frame(apu, session->ring, session->elapsed++, mixbins);
    session->attempts++;
    if (session->attempts >= session->limit) {
        session->active = false;
        dsp_capture_writer_finish_async(session->writer);
    }
}

void dsp_capture_session_stop(DspCaptureSession *session,
                              DspCaptureWriterResult *result)
{
    g_assert(session);
    dsp_capture_writer_stop(session->writer, result);
    dsp_capture_ring_free(session->ring);
    g_free(session);
}

#ifdef CONFIG_XEMU_DSP_CAPTURE
void mcpx_apu_capture_set_ring_locked(MCPXAPUState *apu, DspCaptureRing *ring)
{
    apu->capture_ring = ring;
    apu->capture_frame_id = 0;
}
#endif

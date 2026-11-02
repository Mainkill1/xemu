/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_DSP_CAPTURE_STREAM_H
#define XEMU_DSP_CAPTURE_STREAM_H

#include "capture_ring.h"

typedef struct DspCaptureWriter DspCaptureWriter;
typedef struct DspCaptureWriterResult {
    uint32_t written, dropped, lost;
    bool failed;
} DspCaptureWriterResult;

/* Starts one consumer thread. The file is created exclusively by that
 * thread; the audio producer never accesses the filesystem. The caller must
 * detach and stop the producer before stopping the writer and freeing the
 * ring. The resulting stream is partial evidence, not a complete fixture. */
DspCaptureWriter *dsp_capture_writer_start(DspCaptureRing *ring,
                                           const char *path);
bool dsp_capture_writer_failed(const DspCaptureWriter *writer);
/* The producer must already be detached. This request never waits for I/O. */
void dsp_capture_writer_finish_async(DspCaptureWriter *writer);
bool dsp_capture_writer_finished(const DspCaptureWriter *writer);
void dsp_capture_writer_stop(DspCaptureWriter *writer,
                             DspCaptureWriterResult *result);

typedef struct DspCaptureSession DspCaptureSession;

/* A bounded first-N-frame session. Frame and active calls belong to the APU
 * execution owner; stop only after that producer is detached and quiescent. */
DspCaptureSession *dsp_capture_session_start(const char *path, unsigned frames);
/* Configure before the first frame; delays capture without skipping execution.
 */
bool dsp_capture_session_set_skip(DspCaptureSession *session, unsigned frames);
bool dsp_capture_session_active(const DspCaptureSession *session);
DspCaptureRing *dsp_capture_session_ring(DspCaptureSession *session);
void mcpx_apu_capture_session_frame(
    MCPXAPUState *apu, DspCaptureSession *session,
    float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME]);
void dsp_capture_session_stop(DspCaptureSession *session,
                              DspCaptureWriterResult *result);

#endif

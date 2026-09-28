/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_SHADER_BROWSER_DRAW_REQUEST_H
#define XEMU_SHADER_BROWSER_DRAW_REQUEST_H

#include "shader-browser-session-provider.hh"
#include "shader-browser-draw-inputs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct XemuShaderDrawIdentity {
    uint8_t identity_hash[XEMU_SHADER_BROWSER_HASH_BYTES];
    uint32_t stage;
} XemuShaderDrawIdentity;

typedef struct XemuShaderDrawGeometry {
    /* Four floats per vertex. Both arrays are copied before returning. */
    const float *positions;
    size_t position_count;
    const uint32_t *indices;
    size_t index_count;
} XemuShaderDrawGeometry;

typedef enum XemuShaderCaptureGoal {
    XEMU_SHADER_CAPTURE_ANY_MATCHING_DRAW,
    XEMU_SHADER_CAPTURE_PREVIEWABLE_GEOMETRY,
    /* Reserved: exact full-state replay is not yet an admitted search goal. */
    XEMU_SHADER_CAPTURE_REPLAYABLE_DRAW,
} XemuShaderCaptureGoal;

typedef enum XemuShaderCaptureRejectReason {
    XEMU_SHADER_CAPTURE_REJECT_NONE,
    XEMU_SHADER_CAPTURE_REJECT_TOPOLOGY,
    XEMU_SHADER_CAPTURE_REJECT_VERTEX_FORMAT,
    XEMU_SHADER_CAPTURE_REJECT_POSITION_UNAVAILABLE,
    XEMU_SHADER_CAPTURE_REJECT_EMPTY,
    XEMU_SHADER_CAPTURE_REJECT_INDICES,
    XEMU_SHADER_CAPTURE_REJECT_DEGENERATE,
    XEMU_SHADER_CAPTURE_REJECT_NONFINITE,
    XEMU_SHADER_CAPTURE_REJECT_BUDGET,
    XEMU_SHADER_CAPTURE_REJECT_INCOMPLETE_STATE,
    XEMU_SHADER_CAPTURE_REJECT_NOT_EMITTED,
    XEMU_SHADER_CAPTURE_REJECT_COUNT,
} XemuShaderCaptureRejectReason;

typedef struct XemuShaderDrawRequestSpec {
    uint8_t identity_hash[XEMU_SHADER_BROWSER_HASH_BYTES];
    uint32_t stage;
    XemuShaderBrowserScope scope;
    uint64_t scope_generation;
    uint64_t session_epoch;
    uint64_t renderer_epoch;
    /* Keep searching through unsupported draws until triangle geometry is
     * owned. */
    int require_geometry;
    int capture_inputs;
    XemuShaderCaptureGoal goal;
} XemuShaderDrawRequestSpec;

typedef enum XemuShaderDrawRequestState {
    XEMU_SHADER_DRAW_REQUEST_IDLE,
    XEMU_SHADER_DRAW_REQUEST_ARMED,
    XEMU_SHADER_DRAW_REQUEST_CAPTURING,
    XEMU_SHADER_DRAW_REQUEST_READY,
    XEMU_SHADER_DRAW_REQUEST_CANCELLED,
    XEMU_SHADER_DRAW_REQUEST_FAILED,
} XemuShaderDrawRequestState;

typedef struct XemuShaderDrawRequestStatus {
    uint64_t request_id;
    uint64_t session_epoch;
    uint64_t renderer_epoch;
    uint64_t frame;
    uint32_t draw;
    uint64_t submission;
    uint64_t skipped_draws;
    uint64_t matching_draws;
    uint64_t rejection_counts[XEMU_SHADER_CAPTURE_REJECT_COUNT];
    XemuShaderCaptureRejectReason last_rejection;
    XemuShaderDrawRequestState state;
} XemuShaderDrawRequestStatus;

uint64_t xemu_shader_draw_request_arm(const XemuShaderDrawRequestSpec *spec);
int xemu_shader_draw_request_is_armed(void);
/* Atomically reserve this request before reading the resolved backend source.
 */
uint64_t xemu_shader_draw_request_claim(
    uint64_t scope_generation, uint64_t renderer_epoch,
    const XemuShaderDrawIdentity *identities, size_t identity_count,
    uint64_t frame, uint32_t draw, uint64_t submission);
void xemu_shader_draw_request_cancel(void);
int xemu_shader_draw_request_copy_status(XemuShaderDrawRequestStatus *status);
/* Copies while Capturing; unpublished until the exact command is emitted. */
int xemu_shader_draw_request_stage_geometry(
    uint64_t token, const XemuShaderDrawGeometry *geometry);
/* Diagnostic reason for this claimed candidate. The emission/retirement path
 * decides when to reject it. This never filters an all-shader recording. */
int xemu_shader_draw_request_note_rejection(
    uint64_t token, XemuShaderCaptureRejectReason reason);
/* A false emission releases the claim; stale tokens cannot affect a rearm. */
int xemu_shader_draw_request_finish(uint64_t token, int emitted,
                                    uint32_t primitive_mode,
                                    uint32_t vertex_count,
                                    uint32_t index_count);

#ifdef __cplusplus
}
#endif

#endif

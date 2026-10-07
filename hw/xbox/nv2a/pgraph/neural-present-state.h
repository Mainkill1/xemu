/*
 * Host-only neural-presentation frame state policy.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef HW_XBOX_NV2A_PGRAPH_NEURAL_PRESENT_STATE_H
#define HW_XBOX_NV2A_PGRAPH_NEURAL_PRESENT_STATE_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define XEMU_NEURAL_MAX_CONSECUTIVE_FAILURES 3U

typedef enum XemuNeuralPresentFeature {
    XEMU_NEURAL_FEATURE_NONE,
    XEMU_NEURAL_FEATURE_PASSTHROUGH,
    XEMU_NEURAL_FEATURE_DLAA,
    XEMU_NEURAL_FEATURE_DLSS_SUPER_RESOLUTION,
    XEMU_NEURAL_FEATURE_DLSS_NR,
} XemuNeuralPresentFeature;

typedef enum XemuNeuralPresentDecisionKind {
    XEMU_NEURAL_DECISION_BYPASS,
    XEMU_NEURAL_DECISION_REUSE,
    XEMU_NEURAL_DECISION_PROCESS,
    XEMU_NEURAL_DECISION_PROCESS_RESET,
} XemuNeuralPresentDecisionKind;

typedef enum XemuNeuralPresentBypassReason {
    XEMU_NEURAL_BYPASS_NONE,
    XEMU_NEURAL_BYPASS_DISABLED,
    XEMU_NEURAL_BYPASS_BACKEND_UNAVAILABLE,
    XEMU_NEURAL_BYPASS_BACKEND_FAILED,
    XEMU_NEURAL_BYPASS_INVALID_COLOR,
    XEMU_NEURAL_BYPASS_INVALID_EXTENT,
    XEMU_NEURAL_BYPASS_PVIDEO,
    XEMU_NEURAL_BYPASS_INTERLACED,
    XEMU_NEURAL_BYPASS_MISSING_DEPTH,
} XemuNeuralPresentBypassReason;

typedef enum XemuNeuralPresentResetReason {
    XEMU_NEURAL_RESET_NONE,
    XEMU_NEURAL_RESET_INITIAL_FRAME,
    XEMU_NEURAL_RESET_EXPLICIT,
    XEMU_NEURAL_RESET_SOURCE_CHANGED,
    XEMU_NEURAL_RESET_EXTENT_CHANGED,
    XEMU_NEURAL_RESET_CONFIGURATION_CHANGED,
    XEMU_NEURAL_RESET_FRAME_DISCONTINUITY,
    XEMU_NEURAL_RESET_BACKEND_RECOVERY,
} XemuNeuralPresentResetReason;

typedef enum XemuNeuralPresentResult {
    XEMU_NEURAL_RESULT_RECORDED,
    XEMU_NEURAL_RESULT_PASSTHROUGH,
    XEMU_NEURAL_RESULT_RETRYABLE_FAILURE,
    XEMU_NEURAL_RESULT_FATAL_FAILURE,
} XemuNeuralPresentResult;

typedef struct XemuNeuralPresentFrameKey {
    uint64_t surface_lifetime_id;
    uint64_t output_generation;
    int64_t surface_draw_time;
    int64_t guest_frame_time;
    uint64_t scanout_address;
    uint32_t display_width;
    uint32_t display_height;
    uint32_t processing_width;
    uint32_t processing_height;
    uint32_t surface_scale_factor;
    uint32_t configuration_generation;
} XemuNeuralPresentFrameKey;

typedef struct XemuNeuralPresentFrameInfo {
    bool enabled;
    bool backend_available;
    bool color_valid;
    bool pvideo_enabled;
    bool interlaced;
    bool backend_requires_depth;
    bool depth_valid;
    XemuNeuralPresentFrameKey key;
} XemuNeuralPresentFrameInfo;

typedef struct XemuNeuralPresentDecision {
    XemuNeuralPresentDecisionKind kind;
    XemuNeuralPresentBypassReason reason;
    XemuNeuralPresentResetReason reset_reason;
    uint64_t reset_epoch;
} XemuNeuralPresentDecision;

typedef struct XemuNeuralPresentState {
    bool have_success;
    bool history_valid;
    bool failure_latched;
    unsigned int consecutive_failures;
    uint64_t reset_epoch;
    uint64_t applied_reset_epoch;
    XemuNeuralPresentResetReason pending_reset_reason;
    XemuNeuralPresentBypassReason last_bypass_reason;
    XemuNeuralPresentFeature active_feature;
    XemuNeuralPresentFrameKey last_success_key;
} XemuNeuralPresentState;

static inline uint64_t xemu_neural_present_advance_reset_epoch(
    XemuNeuralPresentState *state)
{
    state->reset_epoch++;
    if (state->reset_epoch == 0) {
        state->reset_epoch = 1;
    }
    return state->reset_epoch;
}

static inline bool xemu_neural_present_frame_key_equal(
    const XemuNeuralPresentFrameKey *a,
    const XemuNeuralPresentFrameKey *b)
{
    return memcmp(a, b, sizeof(*a)) == 0;
}

static inline bool xemu_neural_present_extent_valid(
    const XemuNeuralPresentFrameKey *key)
{
    return key->display_width != 0 && key->display_height != 0 &&
           key->processing_width != 0 && key->processing_height != 0;
}

static inline XemuNeuralPresentResetReason
xemu_neural_present_required_reset(const XemuNeuralPresentState *state,
                                   const XemuNeuralPresentFrameInfo *frame)
{
    const XemuNeuralPresentFrameKey *last = &state->last_success_key;
    const XemuNeuralPresentFrameKey *next = &frame->key;

    if (state->pending_reset_reason != XEMU_NEURAL_RESET_NONE) {
        return state->pending_reset_reason;
    }
    if (!state->have_success || !state->history_valid) {
        return XEMU_NEURAL_RESET_INITIAL_FRAME;
    }
    if (next->surface_lifetime_id != last->surface_lifetime_id) {
        return XEMU_NEURAL_RESET_SOURCE_CHANGED;
    }
    if (next->display_width != last->display_width ||
        next->display_height != last->display_height ||
        next->processing_width != last->processing_width ||
        next->processing_height != last->processing_height ||
        next->surface_scale_factor != last->surface_scale_factor) {
        return XEMU_NEURAL_RESET_EXTENT_CHANGED;
    }
    if (next->configuration_generation != last->configuration_generation) {
        return XEMU_NEURAL_RESET_CONFIGURATION_CHANGED;
    }
    if (next->guest_frame_time < last->guest_frame_time) {
        return XEMU_NEURAL_RESET_FRAME_DISCONTINUITY;
    }
    return XEMU_NEURAL_RESET_NONE;
}

static inline void xemu_neural_present_set_bypass(
    XemuNeuralPresentState *state, XemuNeuralPresentDecision *decision,
    XemuNeuralPresentBypassReason reason)
{
    decision->kind = XEMU_NEURAL_DECISION_BYPASS;
    decision->reason = reason;
    state->last_bypass_reason = reason;
    state->active_feature = XEMU_NEURAL_FEATURE_NONE;

    /* A bypassed host frame breaks continuity with the last enhanced image. */
    if (state->have_success || state->history_valid) {
        state->have_success = false;
        state->history_valid = false;
        if (state->pending_reset_reason == XEMU_NEURAL_RESET_NONE) {
            xemu_neural_present_advance_reset_epoch(state);
            state->pending_reset_reason =
                XEMU_NEURAL_RESET_FRAME_DISCONTINUITY;
        }
        decision->reset_epoch = state->reset_epoch;
    }
}

static inline void xemu_neural_present_begin_frame(
    XemuNeuralPresentState *state, const XemuNeuralPresentFrameInfo *frame,
    XemuNeuralPresentDecision *decision)
{
    memset(decision, 0, sizeof(*decision));
    decision->reset_epoch = state->reset_epoch;

    if (!frame->enabled) {
        xemu_neural_present_set_bypass(
            state, decision, XEMU_NEURAL_BYPASS_DISABLED);
        return;
    }
    if (state->failure_latched) {
        xemu_neural_present_set_bypass(
            state, decision, XEMU_NEURAL_BYPASS_BACKEND_FAILED);
        return;
    }
    if (!frame->backend_available) {
        xemu_neural_present_set_bypass(
            state, decision, XEMU_NEURAL_BYPASS_BACKEND_UNAVAILABLE);
        return;
    }
    if (!frame->color_valid) {
        xemu_neural_present_set_bypass(
            state, decision, XEMU_NEURAL_BYPASS_INVALID_COLOR);
        return;
    }
    if (!xemu_neural_present_extent_valid(&frame->key)) {
        xemu_neural_present_set_bypass(
            state, decision, XEMU_NEURAL_BYPASS_INVALID_EXTENT);
        return;
    }
    if (frame->pvideo_enabled) {
        xemu_neural_present_set_bypass(
            state, decision, XEMU_NEURAL_BYPASS_PVIDEO);
        return;
    }
    if (frame->interlaced) {
        xemu_neural_present_set_bypass(
            state, decision, XEMU_NEURAL_BYPASS_INTERLACED);
        return;
    }
    if (frame->backend_requires_depth && !frame->depth_valid) {
        xemu_neural_present_set_bypass(
            state, decision, XEMU_NEURAL_BYPASS_MISSING_DEPTH);
        return;
    }

    state->last_bypass_reason = XEMU_NEURAL_BYPASS_NONE;
    if (state->have_success &&
        xemu_neural_present_frame_key_equal(
            &state->last_success_key, &frame->key)) {
        decision->kind = XEMU_NEURAL_DECISION_REUSE;
        return;
    }

    bool reset_was_pending =
        state->pending_reset_reason != XEMU_NEURAL_RESET_NONE;
    decision->reset_reason =
        xemu_neural_present_required_reset(state, frame);
    if (decision->reset_reason != XEMU_NEURAL_RESET_NONE &&
        !reset_was_pending) {
        xemu_neural_present_advance_reset_epoch(state);
    }
    decision->reset_epoch = state->reset_epoch;
    decision->kind = decision->reset_reason == XEMU_NEURAL_RESET_NONE
                         ? XEMU_NEURAL_DECISION_PROCESS
                         : XEMU_NEURAL_DECISION_PROCESS_RESET;
}

static inline void xemu_neural_present_complete_frame(
    XemuNeuralPresentState *state, const XemuNeuralPresentFrameInfo *frame,
    const XemuNeuralPresentDecision *decision, XemuNeuralPresentResult result,
    XemuNeuralPresentFeature feature, bool history_valid)
{
    if (decision->kind != XEMU_NEURAL_DECISION_PROCESS &&
        decision->kind != XEMU_NEURAL_DECISION_PROCESS_RESET) {
        return;
    }

    switch (result) {
    case XEMU_NEURAL_RESULT_RECORDED:
    case XEMU_NEURAL_RESULT_PASSTHROUGH:
        state->last_success_key = frame->key;
        state->have_success = true;
        state->history_valid = history_valid;
        state->failure_latched = false;
        state->consecutive_failures = 0;
        state->active_feature = feature;
        state->applied_reset_epoch = decision->reset_epoch;
        state->pending_reset_reason = XEMU_NEURAL_RESET_NONE;
        break;
    case XEMU_NEURAL_RESULT_RETRYABLE_FAILURE:
        state->have_success = false;
        state->history_valid = false;
        state->active_feature = XEMU_NEURAL_FEATURE_NONE;
        if (state->consecutive_failures < UINT32_MAX) {
            state->consecutive_failures++;
        }
        if (state->consecutive_failures >=
            XEMU_NEURAL_MAX_CONSECUTIVE_FAILURES) {
            state->failure_latched = true;
        } else {
            xemu_neural_present_advance_reset_epoch(state);
            state->pending_reset_reason =
                XEMU_NEURAL_RESET_BACKEND_RECOVERY;
        }
        break;
    case XEMU_NEURAL_RESULT_FATAL_FAILURE:
        state->have_success = false;
        state->history_valid = false;
        state->failure_latched = true;
        state->active_feature = XEMU_NEURAL_FEATURE_NONE;
        break;
    default:
        state->have_success = false;
        state->history_valid = false;
        state->failure_latched = true;
        state->active_feature = XEMU_NEURAL_FEATURE_NONE;
        break;
    }
}

static inline void xemu_neural_present_reset(
    XemuNeuralPresentState *state, XemuNeuralPresentResetReason reason)
{
    xemu_neural_present_advance_reset_epoch(state);
    state->applied_reset_epoch = 0;
    state->pending_reset_reason =
        reason == XEMU_NEURAL_RESET_NONE ? XEMU_NEURAL_RESET_EXPLICIT : reason;
    state->have_success = false;
    state->history_valid = false;
    state->failure_latched = false;
    state->consecutive_failures = 0;
    state->active_feature = XEMU_NEURAL_FEATURE_NONE;
    state->last_bypass_reason = XEMU_NEURAL_BYPASS_NONE;
}

static inline const char *xemu_neural_present_bypass_reason_string(
    XemuNeuralPresentBypassReason reason)
{
    switch (reason) {
    case XEMU_NEURAL_BYPASS_NONE:
        return "none";
    case XEMU_NEURAL_BYPASS_DISABLED:
        return "disabled";
    case XEMU_NEURAL_BYPASS_BACKEND_UNAVAILABLE:
        return "backend unavailable";
    case XEMU_NEURAL_BYPASS_BACKEND_FAILED:
        return "backend failed";
    case XEMU_NEURAL_BYPASS_INVALID_COLOR:
        return "invalid color input";
    case XEMU_NEURAL_BYPASS_INVALID_EXTENT:
        return "invalid extent";
    case XEMU_NEURAL_BYPASS_PVIDEO:
        return "PVIDEO unsupported";
    case XEMU_NEURAL_BYPASS_INTERLACED:
        return "interlaced output unsupported";
    case XEMU_NEURAL_BYPASS_MISSING_DEPTH:
        return "validated depth input unavailable";
    }
    return "unknown";
}

static inline const char *xemu_neural_present_feature_string(
    XemuNeuralPresentFeature feature)
{
    switch (feature) {
    case XEMU_NEURAL_FEATURE_NONE:
        return "none";
    case XEMU_NEURAL_FEATURE_PASSTHROUGH:
        return "pass-through";
    case XEMU_NEURAL_FEATURE_DLAA:
        return "DLAA";
    case XEMU_NEURAL_FEATURE_DLSS_SUPER_RESOLUTION:
        return "DLSS Super Resolution";
    case XEMU_NEURAL_FEATURE_DLSS_NR:
        return "DLSS Neural Rendering";
    }
    return "unknown";
}

#endif

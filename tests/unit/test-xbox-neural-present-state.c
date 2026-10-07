/*
 * Experimental neural-presentation frame state policy.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hw/xbox/nv2a/pgraph/neural-present-state.h"

static XemuNeuralPresentFrameInfo valid_frame(uint64_t generation)
{
    XemuNeuralPresentFrameInfo frame = {
        .enabled = true,
        .backend_available = true,
        .color_valid = true,
        .key = {
            .surface_lifetime_id = 11,
            .output_generation = generation,
            .surface_draw_time = 100 + generation,
            .guest_frame_time = 200 + generation,
            .scanout_address = 0x00100000,
            .display_width = 1280,
            .display_height = 720,
            .processing_width = 1280,
            .processing_height = 720,
            .surface_scale_factor = 1,
            .configuration_generation = 1,
        },
    };
    return frame;
}

static bool disabled_and_unavailable_bypass(void)
{
    XemuNeuralPresentState state = { 0 };
    XemuNeuralPresentFrameInfo frame = valid_frame(1);
    XemuNeuralPresentDecision decision;

    frame.enabled = false;
    xemu_neural_present_begin_frame(&state, &frame, &decision);
    if (decision.kind != XEMU_NEURAL_DECISION_BYPASS ||
        decision.reason != XEMU_NEURAL_BYPASS_DISABLED) {
        return false;
    }

    frame.enabled = true;
    frame.backend_available = false;
    xemu_neural_present_begin_frame(&state, &frame, &decision);
    return decision.kind == XEMU_NEURAL_DECISION_BYPASS &&
           decision.reason == XEMU_NEURAL_BYPASS_BACKEND_UNAVAILABLE;
}

static bool unsupported_frame_types_bypass(void)
{
    XemuNeuralPresentState state = { 0 };
    XemuNeuralPresentFrameInfo frame = valid_frame(1);
    XemuNeuralPresentDecision decision;

    frame.color_valid = false;
    xemu_neural_present_begin_frame(&state, &frame, &decision);
    if (decision.reason != XEMU_NEURAL_BYPASS_INVALID_COLOR) {
        return false;
    }

    frame.color_valid = true;
    frame.pvideo_enabled = true;
    xemu_neural_present_begin_frame(&state, &frame, &decision);
    if (decision.reason != XEMU_NEURAL_BYPASS_PVIDEO) {
        return false;
    }

    frame.pvideo_enabled = false;
    frame.interlaced = true;
    xemu_neural_present_begin_frame(&state, &frame, &decision);
    if (decision.reason != XEMU_NEURAL_BYPASS_INTERLACED) {
        return false;
    }

    frame.interlaced = false;
    frame.backend_requires_depth = true;
    frame.depth_valid = false;
    xemu_neural_present_begin_frame(&state, &frame, &decision);
    return decision.reason == XEMU_NEURAL_BYPASS_MISSING_DEPTH;
}

static bool first_frame_resets_then_duplicate_reuses(void)
{
    XemuNeuralPresentState state = { 0 };
    XemuNeuralPresentFrameInfo frame = valid_frame(1);
    XemuNeuralPresentDecision decision;

    xemu_neural_present_begin_frame(&state, &frame, &decision);
    if (decision.kind != XEMU_NEURAL_DECISION_PROCESS_RESET ||
        decision.reset_reason != XEMU_NEURAL_RESET_INITIAL_FRAME) {
        return false;
    }

    xemu_neural_present_complete_frame(
        &state, &frame, &decision, XEMU_NEURAL_RESULT_RECORDED,
        XEMU_NEURAL_FEATURE_DLSS_NR, true);

    xemu_neural_present_begin_frame(&state, &frame, &decision);
    return decision.kind == XEMU_NEURAL_DECISION_REUSE &&
           state.active_feature == XEMU_NEURAL_FEATURE_DLSS_NR;
}

static bool new_generation_processes_without_reset(void)
{
    XemuNeuralPresentState state = { 0 };
    XemuNeuralPresentFrameInfo first = valid_frame(1);
    XemuNeuralPresentFrameInfo second = valid_frame(2);
    XemuNeuralPresentDecision decision;

    xemu_neural_present_begin_frame(&state, &first, &decision);
    xemu_neural_present_complete_frame(
        &state, &first, &decision, XEMU_NEURAL_RESULT_RECORDED,
        XEMU_NEURAL_FEATURE_DLSS_NR, true);

    xemu_neural_present_begin_frame(&state, &second, &decision);
    return decision.kind == XEMU_NEURAL_DECISION_PROCESS &&
           decision.reset_reason == XEMU_NEURAL_RESET_NONE;
}

static bool source_and_extent_changes_reset_history(void)
{
    XemuNeuralPresentState state = { 0 };
    XemuNeuralPresentFrameInfo first = valid_frame(1);
    XemuNeuralPresentFrameInfo next = valid_frame(2);
    XemuNeuralPresentDecision decision;

    xemu_neural_present_begin_frame(&state, &first, &decision);
    xemu_neural_present_complete_frame(
        &state, &first, &decision, XEMU_NEURAL_RESULT_RECORDED,
        XEMU_NEURAL_FEATURE_DLSS_NR, true);

    next.key.surface_lifetime_id++;
    xemu_neural_present_begin_frame(&state, &next, &decision);
    if (decision.kind != XEMU_NEURAL_DECISION_PROCESS_RESET ||
        decision.reset_reason != XEMU_NEURAL_RESET_SOURCE_CHANGED) {
        return false;
    }

    xemu_neural_present_complete_frame(
        &state, &next, &decision, XEMU_NEURAL_RESULT_RECORDED,
        XEMU_NEURAL_FEATURE_DLSS_NR, true);
    next = valid_frame(3);
    next.key.surface_lifetime_id = state.last_success_key.surface_lifetime_id;
    next.key.display_width = 1920;
    next.key.processing_width = 1920;
    xemu_neural_present_begin_frame(&state, &next, &decision);
    return decision.kind == XEMU_NEURAL_DECISION_PROCESS_RESET &&
           decision.reset_reason == XEMU_NEURAL_RESET_EXTENT_CHANGED;
}

static bool configuration_change_resets_history(void)
{
    XemuNeuralPresentState state = { 0 };
    XemuNeuralPresentFrameInfo first = valid_frame(1);
    XemuNeuralPresentFrameInfo next = valid_frame(2);
    XemuNeuralPresentDecision decision;

    xemu_neural_present_begin_frame(&state, &first, &decision);
    xemu_neural_present_complete_frame(
        &state, &first, &decision, XEMU_NEURAL_RESULT_RECORDED,
        XEMU_NEURAL_FEATURE_DLSS_NR, true);

    next.key.configuration_generation++;
    xemu_neural_present_begin_frame(&state, &next, &decision);
    return decision.kind == XEMU_NEURAL_DECISION_PROCESS_RESET &&
           decision.reset_reason == XEMU_NEURAL_RESET_CONFIGURATION_CHANGED;
}

static bool backwards_guest_time_resets_history(void)
{
    XemuNeuralPresentState state = { 0 };
    XemuNeuralPresentFrameInfo first = valid_frame(10);
    XemuNeuralPresentFrameInfo next = valid_frame(11);
    XemuNeuralPresentDecision decision;

    xemu_neural_present_begin_frame(&state, &first, &decision);
    xemu_neural_present_complete_frame(
        &state, &first, &decision, XEMU_NEURAL_RESULT_RECORDED,
        XEMU_NEURAL_FEATURE_DLSS_NR, true);

    next.key.guest_frame_time = first.key.guest_frame_time - 1;
    xemu_neural_present_begin_frame(&state, &next, &decision);
    return decision.kind == XEMU_NEURAL_DECISION_PROCESS_RESET &&
           decision.reset_reason == XEMU_NEURAL_RESET_FRAME_DISCONTINUITY;
}

static bool temporal_reset_epochs_advance(void)
{
    XemuNeuralPresentState state = { 0 };
    XemuNeuralPresentFrameInfo first = valid_frame(1);
    XemuNeuralPresentFrameInfo next = valid_frame(2);
    XemuNeuralPresentDecision decision;

    xemu_neural_present_begin_frame(&state, &first, &decision);
    if (decision.kind != XEMU_NEURAL_DECISION_PROCESS_RESET ||
        decision.reset_epoch == 0) {
        return false;
    }
    uint64_t first_epoch = decision.reset_epoch;
    xemu_neural_present_complete_frame(
        &state, &first, &decision, XEMU_NEURAL_RESULT_RECORDED,
        XEMU_NEURAL_FEATURE_DLSS_NR, true);

    next.key.surface_lifetime_id++;
    xemu_neural_present_begin_frame(&state, &next, &decision);
    return decision.kind == XEMU_NEURAL_DECISION_PROCESS_RESET &&
           decision.reset_reason == XEMU_NEURAL_RESET_SOURCE_CHANGED &&
           decision.reset_epoch > first_epoch;
}

static bool bypass_after_success_forces_reset(void)
{
    XemuNeuralPresentState state = { 0 };
    XemuNeuralPresentFrameInfo frame = valid_frame(1);
    XemuNeuralPresentDecision decision;

    xemu_neural_present_begin_frame(&state, &frame, &decision);
    xemu_neural_present_complete_frame(
        &state, &frame, &decision, XEMU_NEURAL_RESULT_RECORDED,
        XEMU_NEURAL_FEATURE_DLSS_NR, true);

    frame.pvideo_enabled = true;
    xemu_neural_present_begin_frame(&state, &frame, &decision);
    if (decision.kind != XEMU_NEURAL_DECISION_BYPASS ||
        decision.reason != XEMU_NEURAL_BYPASS_PVIDEO ||
        state.have_success || state.history_valid ||
        state.active_feature != XEMU_NEURAL_FEATURE_NONE) {
        return false;
    }

    frame.pvideo_enabled = false;
    xemu_neural_present_begin_frame(&state, &frame, &decision);
    return decision.kind == XEMU_NEURAL_DECISION_PROCESS_RESET &&
           decision.reset_reason == XEMU_NEURAL_RESET_FRAME_DISCONTINUITY;
}

static bool retryable_failures_latch_after_three(void)
{
    XemuNeuralPresentState state = { 0 };
    XemuNeuralPresentDecision decision;

    for (uint64_t i = 1; i <= XEMU_NEURAL_MAX_CONSECUTIVE_FAILURES; i++) {
        XemuNeuralPresentFrameInfo frame = valid_frame(i);
        xemu_neural_present_begin_frame(&state, &frame, &decision);
        xemu_neural_present_complete_frame(
            &state, &frame, &decision, XEMU_NEURAL_RESULT_RETRYABLE_FAILURE,
            XEMU_NEURAL_FEATURE_NONE, false);
    }

    XemuNeuralPresentFrameInfo next = valid_frame(10);
    xemu_neural_present_begin_frame(&state, &next, &decision);
    return state.failure_latched &&
           decision.kind == XEMU_NEURAL_DECISION_BYPASS &&
           decision.reason == XEMU_NEURAL_BYPASS_BACKEND_FAILED;
}

static bool fatal_failure_latches_and_explicit_reset_recovers(void)
{
    XemuNeuralPresentState state = { 0 };
    XemuNeuralPresentFrameInfo frame = valid_frame(1);
    XemuNeuralPresentDecision decision;

    xemu_neural_present_begin_frame(&state, &frame, &decision);
    xemu_neural_present_complete_frame(
        &state, &frame, &decision, XEMU_NEURAL_RESULT_FATAL_FAILURE,
        XEMU_NEURAL_FEATURE_NONE, false);
    xemu_neural_present_begin_frame(&state, &frame, &decision);
    if (!state.failure_latched ||
        decision.reason != XEMU_NEURAL_BYPASS_BACKEND_FAILED) {
        return false;
    }

    xemu_neural_present_reset(&state, XEMU_NEURAL_RESET_EXPLICIT);
    xemu_neural_present_begin_frame(&state, &frame, &decision);
    return !state.failure_latched &&
           decision.kind == XEMU_NEURAL_DECISION_PROCESS_RESET &&
           decision.reset_reason == XEMU_NEURAL_RESET_EXPLICIT;
}

static bool pass_through_is_not_reported_as_neural_rendering(void)
{
    XemuNeuralPresentState state = { 0 };
    XemuNeuralPresentFrameInfo frame = valid_frame(1);
    XemuNeuralPresentDecision decision;

    xemu_neural_present_begin_frame(&state, &frame, &decision);
    xemu_neural_present_complete_frame(
        &state, &frame, &decision, XEMU_NEURAL_RESULT_PASSTHROUGH,
        XEMU_NEURAL_FEATURE_PASSTHROUGH, false);
    return state.active_feature == XEMU_NEURAL_FEATURE_PASSTHROUGH &&
           state.active_feature != XEMU_NEURAL_FEATURE_DLSS_NR;
}

int main(void)
{
    static const struct {
        const char *name;
        bool (*run)(void);
    } tests[] = {
        { "disabled and unavailable paths bypass",
          disabled_and_unavailable_bypass },
        { "unsupported frame types bypass", unsupported_frame_types_bypass },
        { "first frame resets and duplicate reuses",
          first_frame_resets_then_duplicate_reuses },
        { "new output generation keeps history",
          new_generation_processes_without_reset },
        { "source and extent changes reset history",
          source_and_extent_changes_reset_history },
        { "configuration changes reset history",
          configuration_change_resets_history },
        { "backwards guest time resets history",
          backwards_guest_time_resets_history },
        { "temporal resets advance history epoch",
          temporal_reset_epochs_advance },
        { "bypass after success forces reset",
          bypass_after_success_forces_reset },
        { "retryable failures latch after threshold",
          retryable_failures_latch_after_three },
        { "fatal failure reset recovers",
          fatal_failure_latches_and_explicit_reset_recovers },
        { "pass-through is not DLSS NR",
          pass_through_is_not_reported_as_neural_rendering },
    };
    bool passed = true;

    puts("TAP version 13");
    printf("1..%zu\n", sizeof(tests) / sizeof(tests[0]));
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        bool result = tests[i].run();
        printf("%s %zu - %s\n", result ? "ok" : "not ok", i + 1,
               tests[i].name);
        passed &= result;
    }
    return passed ? 0 : 1;
}

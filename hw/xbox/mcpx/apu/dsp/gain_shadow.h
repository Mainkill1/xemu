/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_DSP_GAIN_SHADOW_H
#define XEMU_DSP_GAIN_SHADOW_H
#include <stdio.h>
#include "dsp.h"

typedef struct DspGainShadow DspGainShadow;
/* Shared candidate kernel. Caller admits route 0..2 and 64 24-bit inputs. */
void dsp_gain_compute_route(unsigned route, const uint32_t input[64],
                            uint32_t output[64]);
typedef struct DspGainRouteStats {
    uint64_t native_calls, fallback_calls, compared_words, matching_words;
    uint64_t differing_words, matched_calls, mismatched_calls, native_wall_ns;
    bool first_valid;
    unsigned first_sample;
    uint32_t first_expected, first_actual;
} DspGainRouteStats;
typedef struct DspGainShadowStats {
    uint64_t admitted_jobs, lle_only_frames, reference_frame_wall_ns;
    DspGainRouteStats route[3];
} DspGainShadowStats;

/* Private worker only. Mask bits select three public gain routes, and fault
 * is -1 or a single flattened sample index 0..191 for comparator testing. */
DspGainShadow *dsp_gain_shadow_new(unsigned mask, int fault);
void dsp_gain_shadow_free(DspGainShadow *shadow);
/* APU execution owner; true iff a private native job was submitted. */
bool dsp_gain_shadow_begin(DspGainShadow *shadow, DSPState *gp, bool enabled);
/* Internal boundary API: caller has admitted the exact program and inputs.
 * Used by focused tests; does not access or mutate a live DSP. */
bool dsp_gain_shadow_submit(DspGainShadow *shadow, const uint32_t input[64]);
void dsp_gain_shadow_finish(DspGainShadow *shadow,
                            const uint32_t reference[192],
                            uint64_t reference_frame_wall_ns);
const DspGainShadowStats *dsp_gain_shadow_stats(const DspGainShadow *shadow);
void dsp_gain_shadow_report(const DspGainShadow *shadow, FILE *file);
#endif

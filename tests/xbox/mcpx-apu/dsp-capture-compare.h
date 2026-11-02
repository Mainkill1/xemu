/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_TEST_DSP_CAPTURE_COMPARE_H
#define XEMU_TEST_DSP_CAPTURE_COMPARE_H

#include "hw/xbox/mcpx/apu/dsp/dsp_capture.h"

typedef struct DspCaptureDifference {
    const char *field;
    size_t index;
    uint64_t expected;
    uint64_t actual;
} DspCaptureDifference;

bool dsp_capture_equal(const DspJitCapture *expected,
                       const DspJitCapture *actual,
                       DspCaptureDifference *difference);
bool dsp_c_capture_equal(const DspCCapture *expected, const DspCCapture *actual,
                         DspCaptureDifference *difference);

#endif

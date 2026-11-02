/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_DSP_CAPTURE_WIRE_H
#define XEMU_DSP_CAPTURE_WIRE_H

#include "hw/xbox/mcpx/apu/dsp/dsp_capture.h"

/* JIT-only state payload. This does not include the APU, RAM or I/O log. */
size_t dsp_jit_wire_size(void);
bool dsp_jit_wire_encode(const DspJitCapture *capture, uint8_t *buffer,
                         size_t length);
bool dsp_jit_wire_decode(const uint8_t *buffer, size_t length,
                         DspJitCapture *capture);

#endif

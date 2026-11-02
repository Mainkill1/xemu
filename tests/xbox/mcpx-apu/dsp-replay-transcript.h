/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_DSP_REPLAY_TRANSCRIPT_H
#define XEMU_DSP_REPLAY_TRANSCRIPT_H

#include "dsp-replay-io.h"

/* XIO1: count and records use fixed-width little-endian fields. Decoding is
 * bounded by the caller's buffers; any error leaves the log invalid. */
bool dsp_io_decode(const uint8_t *data, size_t length, DspIoLog *log,
                   DspIoEvent *events, size_t event_capacity, uint8_t *payload,
                   size_t payload_capacity);

#endif

/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_DSP_REPLAY_IO_H
#define XEMU_DSP_REPLAY_IO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum DspIoKind { DSP_IO_SCRATCH, DSP_IO_FIFO } DspIoKind;

typedef struct DspIoEvent {
    DspIoKind kind;
    uint32_t index, address;
    bool write;
    uint64_t sample;
    size_t offset, length;
} DspIoEvent;

typedef struct DspIoLog {
    DspIoEvent *events;
    uint8_t *bytes;
    size_t event_capacity, byte_capacity, count, bytes_used, cursor;
    bool recording, valid, mismatch, missing_context;
} DspIoLog;

void dsp_io_record_init(DspIoLog *log, DspIoEvent *events,
                        size_t event_capacity, uint8_t *bytes,
                        size_t byte_capacity);
bool dsp_io_record(DspIoLog *log, DspIoKind kind, uint32_t index,
                   uint32_t address, bool write, uint64_t sample,
                   const uint8_t *data, size_t length);
void dsp_io_replay_init(DspIoLog *log);
bool dsp_io_replay(DspIoLog *log, DspIoKind kind, uint32_t index,
                   uint32_t address, bool write, uint64_t sample, uint8_t *data,
                   size_t length);
/* Compare an observed transfer, including read responses, without replacing
 * any bytes in the independent device model. */
bool dsp_io_expect(DspIoLog *log, DspIoKind kind, uint32_t index,
                   uint32_t address, bool write, uint64_t sample,
                   const uint8_t *data, size_t length);
bool dsp_io_complete(const DspIoLog *log);

#endif

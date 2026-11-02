/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "dsp-replay-io.h"

void dsp_io_record_init(DspIoLog *log, DspIoEvent *events,
                        size_t event_capacity, uint8_t *bytes,
                        size_t byte_capacity)
{
    *log = (DspIoLog){ .events = events,
                       .bytes = bytes,
                       .event_capacity = event_capacity,
                       .byte_capacity = byte_capacity,
                       .recording = true,
                       .valid = (!event_capacity || events) &&
                                (!byte_capacity || bytes) };
}

bool dsp_io_record(DspIoLog *log, DspIoKind kind, uint32_t index,
                   uint32_t address, bool write, uint64_t sample,
                   const uint8_t *data, size_t length)
{
    if (!log->recording || !log->valid || !log->events ||
        log->bytes_used > log->byte_capacity ||
        (log->byte_capacity && !log->bytes) ||
        log->count >= log->event_capacity ||
        length > log->byte_capacity - log->bytes_used || (length && !data) ||
        (kind != DSP_IO_SCRATCH && kind != DSP_IO_FIFO)) {
        log->valid = false;
        return false;
    }
    DspIoEvent *event = &log->events[log->count++];
    *event = (DspIoEvent){ kind,   index,           address, write,
                           sample, log->bytes_used, length };
    if (length) {
        memcpy(log->bytes + log->bytes_used, data, length);
    }
    log->bytes_used += length;
    return true;
}

void dsp_io_replay_init(DspIoLog *log)
{
    log->recording = false;
    log->cursor = 0;
    log->mismatch = false;
    log->missing_context = false;
}

static bool io_replay(DspIoLog *log, DspIoKind kind, uint32_t index,
                      uint32_t address, bool write, uint64_t sample,
                      const uint8_t *data, uint8_t *receive, size_t length)
{
    if (!log->recording && log->valid && !log->mismatch &&
        log->cursor >= log->count) {
        log->missing_context = true;
    }
    if (log->recording || !log->valid || log->mismatch || !log->events ||
        log->count > log->event_capacity ||
        log->bytes_used > log->byte_capacity ||
        (log->bytes_used && !log->bytes) || log->cursor >= log->count ||
        (length && !data)) {
        log->mismatch = true;
        return false;
    }
    const DspIoEvent *event = &log->events[log->cursor];
    if (event->kind != kind || event->index != index ||
        event->address != address || event->write != write ||
        event->sample != sample || event->length != length ||
        event->offset > log->bytes_used ||
        length > log->bytes_used - event->offset ||
        (!receive && length &&
         memcmp(data, log->bytes + event->offset, length))) {
        log->mismatch = true;
        return false;
    }
    if (receive && length) {
        memcpy(receive, log->bytes + event->offset, length);
    }
    log->cursor++;
    return true;
}

bool dsp_io_replay(DspIoLog *log, DspIoKind kind, uint32_t index,
                   uint32_t address, bool write, uint64_t sample, uint8_t *data,
                   size_t length)
{
    return io_replay(log, kind, index, address, write, sample, data,
                     write ? NULL : data, length);
}

bool dsp_io_expect(DspIoLog *log, DspIoKind kind, uint32_t index,
                   uint32_t address, bool write, uint64_t sample,
                   const uint8_t *data, size_t length)
{
    return io_replay(log, kind, index, address, write, sample, data, NULL,
                     length);
}

bool dsp_io_complete(const DspIoLog *log)
{
    return log->valid && !log->mismatch && log->count <= log->event_capacity &&
           log->bytes_used <= log->byte_capacity &&
           (!log->count || log->events) && (!log->bytes_used || log->bytes) &&
           (log->recording || log->cursor == log->count);
}

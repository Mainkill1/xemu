/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "dsp-replay-transcript.h"

static uint32_t get32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint64_t get64(const uint8_t *data)
{
    return (uint64_t)get32(data) | ((uint64_t)get32(data + 4) << 32);
}

bool dsp_io_decode(const uint8_t *data, size_t length, DspIoLog *log,
                   DspIoEvent *events, size_t event_capacity, uint8_t *payload,
                   size_t payload_capacity)
{
    dsp_io_record_init(log, events, event_capacity, payload, payload_capacity);
    if (!data || length < 8 || memcmp(data, "XIO1", 4) ||
        get32(data + 4) > event_capacity) {
        goto invalid;
    }
    uint32_t count = get32(data + 4);
    size_t offset = 8;
    for (uint32_t i = 0; i < count; i++) {
        if (length - offset < 24) {
            goto invalid;
        }
        const uint8_t *record = data + offset;
        uint32_t size = get32(record + 20);
        offset += 24;
        if (record[0] > 1 || record[1] > 1 || record[2] || record[3] ||
            size > length - offset ||
            !dsp_io_record(log, (DspIoKind)record[0], get32(record + 4),
                           get32(record + 8), record[1], get64(record + 12),
                           data + offset, size)) {
            goto invalid;
        }
        offset += size;
    }
    if (offset != length) {
        goto invalid;
    }
    dsp_io_replay_init(log);
    return true;
invalid:
    log->valid = false;
    return false;
}

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/atomic.h"
#include <float.h>
#include <glib/gstdio.h>
#include "xemu-version-macro.h"
#include "dsp-replay-library-identity.h"
#include "capture_stream.h"
#include "capture_wire.h"

#define DSP_CAPTURE_STREAM_METADATA_SIZE 272
#define DSP_CAPTURE_STREAM_HEADER_SIZE 304
#define DSP_CAPTURE_META_XEMU_COMMIT 1
#define DSP_CAPTURE_META_XEMU_VERSION 2
#define DSP_CAPTURE_META_DSP_LIBRARY 4

struct DspCaptureWriter {
    DspCaptureRing *ring;
    char *path;
    GThread *thread;
    bool stop, failed, finished;
    uint32_t written, lost;
};

typedef struct Buffer {
    uint8_t *data;
    size_t size, pos;
} Buffer;

static void put_bytes(Buffer *b, const void *bytes, size_t size)
{
    g_assert(size <= b->size - b->pos);
    memcpy(b->data + b->pos, bytes, size);
    b->pos += size;
}

static void put32(Buffer *b, uint32_t value)
{
    uint8_t bytes[4] = { value, value >> 8, value >> 16, value >> 24 };
    put_bytes(b, bytes, sizeof(bytes));
}

static void put64(Buffer *b, uint64_t value)
{
    put32(b, value);
    put32(b, value >> 32);
}

static bool put_string(Buffer *b, const char *value, size_t size)
{
    size_t length = strnlen(value, size);
    if (length == size) {
        return false;
    }
    put_bytes(b, value, length + 1);
    while (length + 1 < size) {
        const uint8_t zero = 0;
        put_bytes(b, &zero, 1);
        length++;
    }
    return true;
}

static bool lowercase_sha256(const char *value)
{
    if (strlen(value) != 64) {
        return false;
    }
    for (size_t i = 0; i < 64; i++) {
        if (!g_ascii_isxdigit(value[i]) || g_ascii_isupper(value[i])) {
            return false;
        }
    }
    return true;
}

static bool write_bytes(FILE *file, const void *bytes, size_t length)
{
    return fwrite(bytes, 1, length, file) == length;
}

static bool write_header(FILE *file)
{
    bool dsp_recorded = !strcmp(DSP_REPLAY_LIBRARY_STATUS, "BUILD_RECORDED");
    bool dsp_unverified = !strcmp(DSP_REPLAY_LIBRARY_STATUS, "UNVERIFIED");
    if (!XEMU_VERSION[0] || (!dsp_recorded && !dsp_unverified) ||
        (dsp_recorded && (!DSP_REPLAY_LIBRARY_VERSION[0] ||
                          !lowercase_sha256(DSP_REPLAY_LIBRARY_SHA256))) ||
        (dsp_unverified &&
         (DSP_REPLAY_LIBRARY_VERSION[0] || DSP_REPLAY_LIBRARY_SHA256[0]))) {
        return false;
    }
    uint8_t data[DSP_CAPTURE_STREAM_HEADER_SIZE] = { 0 };
    Buffer header = { .data = data, .size = sizeof(data) };
    put_bytes(&header, "XCR1", 4);
    put32(&header, 3);
    put32(&header, sizeof(data));
    put32(&header, (XEMU_COMMIT[0] ? DSP_CAPTURE_META_XEMU_COMMIT : 0) |
                       DSP_CAPTURE_META_XEMU_VERSION |
                       (dsp_recorded ? DSP_CAPTURE_META_DSP_LIBRARY : 0));
    int64_t created = g_get_real_time();
    if (created <= 0) {
        return false;
    }
    put64(&header, created);
    if (!put_string(&header, XEMU_COMMIT, 65) ||
        !put_string(&header, XEMU_VERSION, 64) ||
        !put_string(&header, DSP_REPLAY_LIBRARY_STATUS, 16) ||
        !put_string(&header, DSP_REPLAY_LIBRARY_VERSION, 32) ||
        !put_string(&header, DSP_REPLAY_LIBRARY_SHA256, 65)) {
        return false;
    }
    while (header.pos < DSP_CAPTURE_STREAM_METADATA_SIZE) {
        const uint8_t zero = 0;
        put_bytes(&header, &zero, 1);
    }
    GChecksum *checksum = g_checksum_new(G_CHECKSUM_SHA256);
    if (!checksum) {
        return false;
    }
    g_checksum_update(checksum, data, DSP_CAPTURE_STREAM_METADATA_SIZE);
    uint8_t digest[32];
    gsize digest_size = sizeof(digest);
    g_checksum_get_digest(checksum, digest, &digest_size);
    g_checksum_free(checksum);
    if (digest_size != sizeof(digest)) {
        return false;
    }
    put_bytes(&header, digest, sizeof(digest));
    g_assert_cmpuint(header.pos, ==, DSP_CAPTURE_STREAM_HEADER_SIZE);
    return write_bytes(file, data, sizeof(data));
}

static bool write_frame(FILE *file, const DspCaptureFrame *frame)
{
    if (sizeof(float) != 4 || FLT_RADIX != 2 || FLT_MANT_DIG != 24 ||
        FLT_MAX_EXP != 128 || frame->event_count > DSP_CAPTURE_MAX_EVENTS ||
        frame->payload_size > DSP_CAPTURE_PAYLOAD_BYTES) {
        return false;
    }
    size_t payload_used = 0;
    for (size_t i = 0; i < frame->event_count; i++) {
        const DspCaptureEvent *event = &frame->events[i];
        if (event->kind > DSP_CAPTURE_SGE_READ ||
            event->offset != payload_used ||
            event->length > frame->payload_size - payload_used) {
            return false;
        }
        payload_used += event->length;
    }
    if (payload_used != frame->payload_size) {
        return false;
    }
    size_t state_size = dsp_jit_wire_size();
    size_t length = 40 + 4 * state_size + sizeof(frame->apu_regs) * 2 +
                    sizeof(frame->mixbins) + sizeof(frame->pcm_before) * 2 + 8 +
                    24 * frame->event_count + frame->payload_size;
    if (length > UINT32_MAX) {
        return false;
    }
    Buffer b = { .data = g_try_malloc(length), .size = length };
    if (!b.data) {
        return false;
    }
    put_bytes(&b, "XFR1", 4);
    put64(&b, frame->frame_id);
    put32(&b, frame->flags | DSP_CAPTURE_EXTERNAL_UNVERIFIED);
    put32(&b, frame->dropped_before);
    put32(&b, frame->phase);
    put32(&b, frame->gp_reset);
    put32(&b, frame->ep_reset);
    put32(&b, frame->monitor_point);
    const uint8_t controls[4] = { frame->gp_realtime, frame->ep_realtime,
                                  frame->started, frame->finished };
    put_bytes(&b, controls, sizeof(controls));
    const DspJitCapture *states[] = { &frame->gp_before, &frame->ep_before,
                                      &frame->gp_after, &frame->ep_after };
    bool valid = true;
    for (size_t i = 0; i < G_N_ELEMENTS(states); i++) {
        if (!dsp_jit_wire_encode(states[i], b.data + b.pos, state_size)) {
            valid = false;
            goto out;
        }
        b.pos += state_size;
    }
    for (size_t i = 0; i < G_N_ELEMENTS(frame->apu_regs); i++) {
        put32(&b, frame->apu_regs[i]);
    }
    for (size_t i = 0; i < G_N_ELEMENTS(frame->apu_regs_after); i++) {
        put32(&b, frame->apu_regs_after[i]);
    }
    for (size_t i = 0; i < NUM_MIXBINS; i++) {
        for (size_t j = 0; j < NUM_SAMPLES_PER_FRAME; j++) {
            uint32_t bits;
            memcpy(&bits, &frame->mixbins[i][j], sizeof(bits));
            put32(&b, bits);
        }
    }
    for (size_t pass = 0; pass < 2; pass++) {
        const int16_t (*pcm)[2] = pass ? frame->pcm_after : frame->pcm_before;
        for (size_t i = 0; i < 256; i++) {
            for (size_t channel = 0; channel < 2; channel++) {
                uint16_t value = pcm[i][channel];
                const uint8_t bytes[2] = { value, value >> 8 };
                put_bytes(&b, bytes, sizeof(bytes));
            }
        }
    }
    put_bytes(&b, "XCT1", 4);
    put32(&b, frame->event_count);
    for (size_t i = 0; i < frame->event_count; i++) {
        const DspCaptureEvent *event = &frame->events[i];
        const uint8_t header[4] = { event->kind, event->write, 0, 0 };
        put_bytes(&b, header, sizeof(header));
        put32(&b, event->index);
        put32(&b, event->address);
        put64(&b, event->sample);
        put32(&b, event->length);
        put_bytes(&b, frame->payload + event->offset, event->length);
    }
    if (b.pos != length) {
        valid = false;
        goto out;
    }
    GChecksum *checksum = g_checksum_new(G_CHECKSUM_SHA256);
    if (!checksum) {
        valid = false;
        goto out;
    }
    g_checksum_update(checksum, b.data, length);
    uint8_t digest[32];
    gsize digest_size = sizeof(digest);
    g_checksum_get_digest(checksum, digest, &digest_size);
    g_checksum_free(checksum);
    uint8_t header_data[8];
    Buffer header = { .data = header_data, .size = sizeof(header_data) };
    put_bytes(&header, "FRM1", 4);
    put32(&header, length);
    valid = digest_size == sizeof(digest) &&
            write_bytes(file, header_data, sizeof(header_data)) &&
            write_bytes(file, digest, sizeof(digest)) &&
            write_bytes(file, b.data, length);
out:
    g_free(b.data);
    return valid;
}

static gpointer writer_thread(gpointer opaque)
{
    DspCaptureWriter *writer = opaque;
    int flags = O_WRONLY | O_CREAT | O_EXCL | O_BINARY;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    int fd = g_open(writer->path, flags, 0600);
    FILE *file = fd >= 0 ? fdopen(fd, "wb") : NULL;
    if (fd >= 0 && !file) {
        close(fd);
    }
    if (!file || !write_header(file)) {
        qatomic_store_release(&writer->failed, true);
    }
    for (;;) {
        const DspCaptureFrame *frame = dsp_capture_ring_peek(writer->ring);
        if (!frame && qatomic_load_acquire(&writer->stop)) {
            frame = dsp_capture_ring_peek(writer->ring);
            if (!frame) {
                break;
            }
        }
        if (!frame) {
            g_usleep(1000);
            continue;
        }
        if (!qatomic_read(&writer->failed) && writer->written != UINT32_MAX &&
            write_frame(file, frame)) {
            writer->written++;
        } else {
            qatomic_store_release(&writer->failed, true);
            if (writer->lost != UINT32_MAX) {
                writer->lost++;
            }
        }
        dsp_capture_ring_release(writer->ring);
    }
    if (file) {
        if (!qatomic_read(&writer->failed)) {
            uint8_t footer_data[16];
            Buffer footer = { .data = footer_data,
                              .size = sizeof(footer_data) };
            put_bytes(&footer, "END1", 4);
            put32(&footer, writer->written);
            put32(&footer, dsp_capture_ring_drops(writer->ring));
            put32(&footer, writer->lost);
            if (!write_bytes(file, footer_data, sizeof(footer_data)) ||
                fflush(file)) {
                qatomic_store_release(&writer->failed, true);
            }
        }
        if (fclose(file)) {
            qatomic_store_release(&writer->failed, true);
        }
    }
    qatomic_store_release(&writer->finished, true);
    return NULL;
}

DspCaptureWriter *dsp_capture_writer_start(DspCaptureRing *ring,
                                           const char *path)
{
    if (!ring || !path || !*path || strnlen(path, 4097) > 4096) {
        return NULL;
    }
    DspCaptureWriter *writer = g_try_new0(DspCaptureWriter, 1);
    if (!writer) {
        return NULL;
    }
    size_t length = strlen(path) + 1;
    writer->path = g_try_malloc(length);
    if (!writer->path) {
        g_free(writer);
        return NULL;
    }
    memcpy(writer->path, path, length);
    writer->ring = ring;
    GError *error = NULL;
    writer->thread =
        g_thread_try_new("dsp-capture-writer", writer_thread, writer, &error);
    if (!writer->thread) {
        g_clear_error(&error);
        g_free(writer->path);
        g_free(writer);
        return NULL;
    }
    return writer;
}

bool dsp_capture_writer_failed(const DspCaptureWriter *writer)
{
    return !writer || qatomic_load_acquire(&writer->failed);
}

void dsp_capture_writer_finish_async(DspCaptureWriter *writer)
{
    g_assert(writer);
    qatomic_store_release(&writer->stop, true);
}

bool dsp_capture_writer_finished(const DspCaptureWriter *writer)
{
    return writer && qatomic_load_acquire(&writer->finished);
}

void dsp_capture_writer_stop(DspCaptureWriter *writer,
                             DspCaptureWriterResult *result)
{
    g_assert(writer);
    dsp_capture_writer_finish_async(writer);
    g_thread_join(writer->thread);
    if (result) {
        *result =
            (DspCaptureWriterResult){ .written = writer->written,
                                      .dropped =
                                          dsp_capture_ring_drops(writer->ring),
                                      .lost = writer->lost,
                                      .failed = qatomic_read(&writer->failed) };
    }
    g_free(writer->path);
    g_free(writer);
}

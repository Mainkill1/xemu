/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/atomic.h"
#include "capture_ring.h"

struct DspCaptureRing {
    DspCaptureFrame *slots;
    unsigned capacity;
    uint32_t head, tail, drops;
    uint32_t pending_drops; /* producer-owned until copied into a frame */
    bool pending_discontinuity;
    /* Each flag is accessed only by its designated thread. */
    bool producer_open, consumer_open;
};

DspCaptureRing *dsp_capture_ring_new(unsigned capacity)
{
    if (!capacity || capacity > DSP_CAPTURE_MAX_SLOTS ||
        (capacity & (capacity - 1))) {
        return NULL;
    }
    DspCaptureRing *ring = g_try_new0(DspCaptureRing, 1);
    if (!ring) {
        return NULL;
    }
    ring->slots = g_try_new0(DspCaptureFrame, capacity);
    if (!ring->slots) {
        g_free(ring);
        return NULL;
    }
    ring->capacity = capacity;
    return ring;
}

void dsp_capture_ring_free(DspCaptureRing *ring)
{
    if (ring) {
        g_free(ring->slots);
        g_free(ring);
    }
}

DspCaptureFrame *dsp_capture_ring_begin(DspCaptureRing *ring, uint64_t frame_id)
{
    if (!ring || ring->producer_open) {
        return NULL;
    }
    uint32_t head = qatomic_read(&ring->head);
    uint32_t tail = qatomic_load_acquire(&ring->tail);
    if ((uint32_t)(head - tail) >= ring->capacity) {
        uint32_t drops = qatomic_read(&ring->drops);
        if (drops != UINT32_MAX) {
            qatomic_set(&ring->drops, drops + 1);
        }
        if (ring->pending_drops != UINT32_MAX) {
            ring->pending_drops++;
        }
        return NULL;
    }
    DspCaptureFrame *frame = &ring->slots[head % ring->capacity];
    memset(frame, 0, sizeof(*frame));
    frame->frame_id = frame_id;
    /* RAM and concurrent guest writes require separate proof. */
    frame->flags = DSP_CAPTURE_EXTERNAL_UNVERIFIED;
    if (ring->pending_discontinuity) {
        frame->flags |= DSP_CAPTURE_DISCONTINUITY;
        ring->pending_discontinuity = false;
    }
    frame->dropped_before = ring->pending_drops;
    if (frame->dropped_before) {
        frame->flags |= DSP_CAPTURE_DROPPED_BEFORE;
        ring->pending_drops = 0;
    }
    ring->producer_open = true;
    return frame;
}

void dsp_capture_ring_mark_discontinuity(DspCaptureRing *ring)
{
    if (ring) {
        ring->pending_discontinuity = true;
    }
}

bool dsp_capture_frame_event(DspCaptureFrame *frame, uint32_t kind,
                             uint32_t index, uint32_t address, bool write,
                             uint64_t sample, const uint8_t *bytes,
                             size_t length)
{
    if (!frame || (frame->flags & DSP_CAPTURE_TRUNCATED)) {
        return false;
    }
    if (frame->event_count >= DSP_CAPTURE_MAX_EVENTS ||
        frame->payload_size > DSP_CAPTURE_PAYLOAD_BYTES ||
        length > DSP_CAPTURE_PAYLOAD_BYTES - frame->payload_size ||
        (length && !bytes)) {
        frame->flags |= DSP_CAPTURE_TRUNCATED;
        return false;
    }
    DspCaptureEvent *event = &frame->events[frame->event_count++];
    *event = (DspCaptureEvent){ .kind = kind,
                                .index = index,
                                .address = address,
                                .sample = sample,
                                .offset = frame->payload_size,
                                .length = length,
                                .write = write };
    if (length) {
        memcpy(frame->payload + frame->payload_size, bytes, length);
    }
    frame->payload_size += length;
    return true;
}

void dsp_capture_ring_publish(DspCaptureRing *ring)
{
    g_assert(ring && ring->producer_open);
    DspCaptureFrame *frame =
        &ring->slots[qatomic_read(&ring->head) % ring->capacity];
    if (!frame->started || !frame->finished) {
        frame->flags |= DSP_CAPTURE_INCOMPLETE_BOUNDARY;
    }
    ring->producer_open = false;
    qatomic_store_release(&ring->head, qatomic_read(&ring->head) + 1);
}

const DspCaptureFrame *dsp_capture_ring_peek(DspCaptureRing *ring)
{
    if (!ring || ring->consumer_open) {
        return NULL;
    }
    uint32_t tail = qatomic_read(&ring->tail);
    if (tail == qatomic_load_acquire(&ring->head)) {
        return NULL;
    }
    ring->consumer_open = true;
    return &ring->slots[tail % ring->capacity];
}

void dsp_capture_ring_release(DspCaptureRing *ring)
{
    g_assert(ring && ring->consumer_open);
    ring->consumer_open = false;
    qatomic_store_release(&ring->tail, qatomic_read(&ring->tail) + 1);
}

uint32_t dsp_capture_ring_drops(const DspCaptureRing *ring)
{
    return ring ? qatomic_read(&ring->drops) : 0;
}

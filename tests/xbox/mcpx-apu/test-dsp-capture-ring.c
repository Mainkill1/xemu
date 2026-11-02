/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/atomic.h"
#include "hw/xbox/mcpx/apu/dsp/capture_ring.h"

static void test_fifo_and_overflow(void)
{
    DspCaptureRing *ring = dsp_capture_ring_new(2);
    g_assert_nonnull(ring);
    DspCaptureFrame *frame = dsp_capture_ring_begin(ring, 10);
    g_assert_nonnull(frame);
    g_assert_cmpuint(frame->flags, ==, DSP_CAPTURE_EXTERNAL_UNVERIFIED);
    uint8_t bytes[] = { 1, 2, 3 };
    g_assert_true(dsp_capture_frame_event(frame, 1, 7, 9, true, 32, bytes,
                                          sizeof(bytes)));
    g_assert_null(dsp_capture_ring_peek(ring)); /* not published yet */
    dsp_capture_ring_publish(ring);
    frame = dsp_capture_ring_begin(ring, 11);
    g_assert_nonnull(frame);
    dsp_capture_ring_publish(ring);
    g_assert_null(dsp_capture_ring_begin(ring, 12));
    g_assert_cmpuint(dsp_capture_ring_drops(ring), ==, 1);
    dsp_capture_ring_mark_discontinuity(ring);
    const DspCaptureFrame *read = dsp_capture_ring_peek(ring);
    g_assert_nonnull(read);
    g_assert_cmpuint(read->frame_id, ==, 10);
    g_assert_true(read->flags & DSP_CAPTURE_INCOMPLETE_BOUNDARY);
    g_assert_cmpuint(read->events[0].sample, ==, 32);
    g_assert_cmpmem(read->payload, read->payload_size, bytes, sizeof(bytes));
    dsp_capture_ring_release(ring);
    frame = dsp_capture_ring_begin(ring, 12);
    g_assert_nonnull(frame);
    g_assert_true(frame->flags & DSP_CAPTURE_DROPPED_BEFORE);
    g_assert_true(frame->flags & DSP_CAPTURE_DISCONTINUITY);
    g_assert_cmpuint(frame->dropped_before, ==, 1);
    g_assert_cmpuint(frame->event_count, ==, 0);
    g_assert_cmpuint(frame->payload_size, ==, 0);
    g_assert_false(dsp_capture_frame_event(frame, 1, 0, 0, false, 0, bytes,
                                           DSP_CAPTURE_PAYLOAD_BYTES + 1));
    g_assert_true(frame->flags & DSP_CAPTURE_TRUNCATED);
    g_assert_cmpuint(frame->event_count, ==, 0);
    dsp_capture_ring_publish(ring);
    read = dsp_capture_ring_peek(ring);
    g_assert_cmpuint(read->frame_id, ==, 11);
    dsp_capture_ring_release(ring);
    read = dsp_capture_ring_peek(ring);
    g_assert_cmpuint(read->frame_id, ==, 12);
    g_assert_true(read->flags & DSP_CAPTURE_TRUNCATED);
    dsp_capture_ring_release(ring);
    g_assert_null(dsp_capture_ring_peek(ring));
    frame = dsp_capture_ring_begin(ring, 13);
    g_assert_nonnull(frame);
    g_assert_false(frame->flags & DSP_CAPTURE_DISCONTINUITY);
    for (unsigned i = 0; i < DSP_CAPTURE_MAX_EVENTS; i++) {
        g_assert_true(
            dsp_capture_frame_event(frame, 0, 0, i, false, 0, NULL, 0));
    }
    g_assert_false(dsp_capture_frame_event(frame, 0, 0, 0, false, 0, NULL, 0));
    g_assert_true(frame->flags & DSP_CAPTURE_TRUNCATED);
    dsp_capture_ring_publish(ring);
    g_assert_nonnull(dsp_capture_ring_peek(ring));
    dsp_capture_ring_release(ring);
    dsp_capture_ring_free(ring);
    g_assert_null(dsp_capture_ring_new(0));
    g_assert_null(dsp_capture_ring_new(3));
    g_assert_null(dsp_capture_ring_new(DSP_CAPTURE_MAX_SLOTS + 1));
}

typedef struct StressContext {
    DspCaptureRing *ring;
    bool done;
    uint32_t consumed;
} StressContext;

static gpointer consume_frames(gpointer opaque)
{
    StressContext *context = opaque;
    uint64_t previous = 0;
    bool first = true;
    for (;;) {
        const DspCaptureFrame *frame = dsp_capture_ring_peek(context->ring);
        if (!frame) {
            if (qatomic_load_acquire(&context->done)) {
                frame = dsp_capture_ring_peek(context->ring);
                if (!frame) {
                    break;
                }
            } else {
                g_thread_yield();
                continue;
            }
        }
        g_assert_true(first || frame->frame_id > previous);
        first = false;
        previous = frame->frame_id;
        g_assert_cmpuint(frame->event_count, ==, 1);
        g_assert_cmpuint(frame->events[0].sample, ==, frame->frame_id * 32);
        g_assert_cmpuint(frame->payload[0], ==, (uint8_t)frame->frame_id);
        context->consumed++;
        dsp_capture_ring_release(context->ring);
    }
    return NULL;
}

static void test_single_producer_consumer(void)
{
    enum { COUNT = 10000 };
    StressContext context = { .ring = dsp_capture_ring_new(4) };
    g_assert_nonnull(context.ring);
    GThread *thread = g_thread_new("capture-test", consume_frames, &context);
    for (uint64_t i = 0; i < COUNT; i++) {
        DspCaptureFrame *frame = dsp_capture_ring_begin(context.ring, i);
        if (frame) {
            uint8_t byte = i;
            g_assert_true(dsp_capture_frame_event(frame, 0, 0, 0, false, i * 32,
                                                  &byte, 1));
            dsp_capture_ring_publish(context.ring);
        }
    }
    qatomic_store_release(&context.done, true);
    g_thread_join(thread);
    g_assert_cmpuint(context.consumed + dsp_capture_ring_drops(context.ring),
                     ==, COUNT);
    dsp_capture_ring_free(context.ring);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/dsp/capture-ring/fifo-overflow", test_fifo_and_overflow);
    g_test_add_func("/dsp/capture-ring/concurrent",
                    test_single_producer_consumer);
    return g_test_run();
}

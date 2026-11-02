/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include <glib/gstdio.h>
#include "xemu-version-macro.h"
#include "dsp-replay-library-identity.h"
#include "hw/xbox/mcpx/apu/dsp/capture_stream.h"
#include "hw/xbox/mcpx/apu/dsp/capture_wire.h"

static uint32_t read32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
           (uint32_t)p[3] << 24;
}

static uint64_t read64(const uint8_t *p)
{
    return (uint64_t)read32(p) | (uint64_t)read32(p + 4) << 32;
}

static void test_async_write(void)
{
    GError *error = NULL;
    char *dir = g_dir_make_tmp("xemu-dsp-stream-XXXXXX", &error);
    g_assert_no_error(error);
    char *path = g_build_filename(dir, "capture.bin", NULL);
    DspCaptureRing *ring = dsp_capture_ring_new(2);
    DspCaptureWriter *writer = dsp_capture_writer_start(ring, path);
    g_assert_nonnull(writer);
    DspCaptureFrame *frame = dsp_capture_ring_begin(ring, 42);
    g_assert_nonnull(frame);
    frame->started = frame->finished = true;
    frame->flags |= DSP_CAPTURE_MAPPING_OBSERVED;
    frame->phase = 7;
    frame->gp_before.is_gp = frame->gp_after.is_gp = true;
    frame->apu_regs[3] = 0x12345678;
    frame->apu_regs_after[3] = 0x87654321;
    frame->mixbins[0][0] = 0.5f;
    frame->pcm_after[0][0] = -123;
    const uint8_t payload[] = { 0x56, 0x34, 0x12, 0 };
    g_assert_true(dsp_capture_frame_event(frame, DSP_CAPTURE_SCRATCH, 0, 12,
                                          true, 42 * 32, payload,
                                          sizeof(payload)));
    const uint8_t descriptor[] = { 0x00, 0x40, 0x00, 0x00 };
    g_assert_true(dsp_capture_frame_event(frame, DSP_CAPTURE_SGE_READ, 0,
                                          0x1000, false, 42 * 32, descriptor,
                                          sizeof(descriptor)));
    dsp_capture_ring_publish(ring);
    dsp_capture_writer_finish_async(writer);
    for (unsigned i = 0; i < 500 && !dsp_capture_writer_finished(writer); i++) {
        g_usleep(1000);
    }
    g_assert_true(dsp_capture_writer_finished(writer));
    DspCaptureWriterResult result;
    dsp_capture_writer_stop(writer, &result);
    g_assert_false(result.failed);
    g_assert_cmpuint(result.written, ==, 1);
    g_assert_cmpuint(result.dropped, ==, 0);
    g_assert_cmpuint(result.lost, ==, 0);
    gchar *bytes = NULL;
    gsize size = 0;
    g_assert_true(g_file_get_contents(path, &bytes, &size, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(size, >, 344 + 4 * dsp_jit_wire_size());
    g_assert_cmpmem(bytes, 4, "XCR1", 4);
    g_assert_cmpuint(read32((uint8_t *)bytes + 4), ==, 3);
    g_assert_cmpuint(read32((uint8_t *)bytes + 8), ==, 304);
    uint32_t expected_flags =
        (XEMU_COMMIT[0] ? 1 : 0) | (XEMU_VERSION[0] ? 2 : 0) |
        (!strcmp(DSP_REPLAY_LIBRARY_STATUS, "BUILD_RECORDED") ? 4 : 0);
    g_assert_cmpuint(read32((uint8_t *)bytes + 12), ==, expected_flags);
    g_assert_cmpuint(read64((uint8_t *)bytes + 16), >, 0);
    g_assert_cmpstr(bytes + 24, ==, XEMU_COMMIT);
    g_assert_cmpstr(bytes + 89, ==, XEMU_VERSION);
    g_assert_cmpstr(bytes + 153, ==, DSP_REPLAY_LIBRARY_STATUS);
    g_assert_cmpstr(bytes + 169, ==, DSP_REPLAY_LIBRARY_VERSION);
    g_assert_cmpstr(bytes + 201, ==, DSP_REPLAY_LIBRARY_SHA256);
    g_assert_cmpmem(bytes + 266, 6, "\0\0\0\0\0\0", 6);
    gchar *header_digest =
        g_compute_checksum_for_data(G_CHECKSUM_SHA256, (guchar *)bytes, 272);
    for (unsigned i = 0; i < 32; i++) {
        char hex[3] = { header_digest[2 * i], header_digest[2 * i + 1], 0 };
        g_assert_cmpuint((uint8_t)bytes[272 + i], ==, strtoul(hex, NULL, 16));
    }
    g_assert_cmpmem(bytes + 304, 4, "FRM1", 4);
    uint32_t length = read32((uint8_t *)bytes + 308);
    g_assert_cmpuint(size, ==, 344 + length + 16);
    gchar *digest = g_compute_checksum_for_data(G_CHECKSUM_SHA256,
                                                (guchar *)bytes + 344, length);
    for (unsigned i = 0; i < 32; i++) {
        char hex[3] = { digest[2 * i], digest[2 * i + 1], 0 };
        g_assert_cmpuint((uint8_t)bytes[312 + i], ==, strtoul(hex, NULL, 16));
    }
    g_assert_cmpmem(bytes + 344, 4, "XFR1", 4);
    g_assert_cmpuint(read32((uint8_t *)bytes + 344 + 12), &,
                     DSP_CAPTURE_EXTERNAL_UNVERIFIED);
    const uint8_t *record = (const uint8_t *)bytes + 344;
    g_assert_cmpuint(read32(record + 4), ==, 42);
    g_assert_cmpuint(read32(record + 20), ==, 7);
    DspJitCapture *decoded = g_new0(DspJitCapture, 1);
    g_assert_true(
        dsp_jit_wire_decode(record + 40, dsp_jit_wire_size(), decoded));
    g_assert_true(decoded->is_gp);
    g_free(decoded);
    size_t regs = 40 + 4 * dsp_jit_wire_size();
    g_assert_cmphex(read32(record + regs + 3 * 4), ==, 0x12345678);
    g_assert_cmphex(read32(record + regs + 0x80000 + 3 * 4), ==, 0x87654321);
    size_t mixbins = regs + 0x100000;
    g_assert_cmphex(read32(record + mixbins), ==, 0x3f000000);
    size_t pcm_after = mixbins + 4096 + 1024;
    g_assert_cmphex(record[pcm_after], ==, 0x85);
    g_assert_cmphex(record[pcm_after + 1], ==, 0xff);
    size_t events = pcm_after + 1024;
    g_assert_cmpmem(record + events, 4, "XCT1", 4);
    g_assert_cmpuint(read32(record + events + 4), ==, 2);
    g_assert_cmpuint(record[events + 8], ==, DSP_CAPTURE_SCRATCH);
    g_assert_cmpuint(record[events + 9], ==, 1);
    g_assert_cmpuint(read32(record + events + 16), ==, 12);
    g_assert_cmpuint(read32(record + events + 20), ==, 42 * 32);
    g_assert_cmpuint(read32(record + events + 28), ==, 4);
    g_assert_cmpmem(record + events + 32, 4, payload, 4);
    g_assert_cmpuint(record[events + 36], ==, DSP_CAPTURE_SGE_READ);
    g_assert_cmpuint(read32(record + events + 44), ==, 0x1000);
    g_assert_cmpmem(record + events + 60, 4, descriptor, 4);
    g_assert_cmpmem(bytes + 344 + length, 4, "END1", 4);
    g_assert_cmpuint(read32((uint8_t *)bytes + 344 + length + 4), ==, 1);
    g_free(header_digest);
    g_free(digest);
    g_free(bytes);
    dsp_capture_ring_free(ring);
    g_assert_cmpint(g_unlink(path), ==, 0);
    g_assert_cmpint(g_rmdir(dir), ==, 0);
    g_free(path);
    g_free(dir);
}

static void test_existing_file_is_not_overwritten(void)
{
    GError *error = NULL;
    char *dir = g_dir_make_tmp("xemu-dsp-stream-XXXXXX", &error);
    g_assert_no_error(error);
    char *path = g_build_filename(dir, "capture.bin", NULL);
    g_assert_true(g_file_set_contents(path, "existing", -1, &error));
    g_assert_no_error(error);
    DspCaptureRing *ring = dsp_capture_ring_new(1);
    DspCaptureWriter *writer = dsp_capture_writer_start(ring, path);
    g_assert_nonnull(writer);
    DspCaptureWriterResult result;
    dsp_capture_writer_stop(writer, &result);
    g_assert_true(result.failed);
    gchar *contents = NULL;
    g_assert_true(g_file_get_contents(path, &contents, NULL, &error));
    g_assert_no_error(error);
    g_assert_cmpstr(contents, ==, "existing");
    g_free(contents);
    dsp_capture_ring_free(ring);
    g_assert_cmpint(g_unlink(path), ==, 0);
    g_assert_cmpint(g_rmdir(dir), ==, 0);
    g_free(path);
    g_free(dir);
}

static void test_bad_frame_and_drop_are_reported(void)
{
    GError *error = NULL;
    char *dir = g_dir_make_tmp("xemu-dsp-stream-XXXXXX", &error);
    g_assert_no_error(error);
    char *path = g_build_filename(dir, "capture.bin", NULL);
    DspCaptureRing *ring = dsp_capture_ring_new(1);
    DspCaptureFrame *frame = dsp_capture_ring_begin(ring, 0);
    g_assert_nonnull(frame);
    frame->started = frame->finished = true;
    uint8_t byte = 7;
    g_assert_true(dsp_capture_frame_event(frame, DSP_CAPTURE_SCRATCH, 0, 0,
                                          true, 0, &byte, 1));
    frame->events[0].offset = 2; /* damaged handoff must not be serialized */
    dsp_capture_ring_publish(ring);
    g_assert_null(dsp_capture_ring_begin(ring, 1));
    DspCaptureWriter *writer = dsp_capture_writer_start(ring, path);
    g_assert_nonnull(writer);
    DspCaptureWriterResult result;
    dsp_capture_writer_stop(writer, &result);
    g_assert_true(result.failed);
    g_assert_cmpuint(result.written, ==, 0);
    g_assert_cmpuint(result.lost, ==, 1);
    g_assert_cmpuint(result.dropped, ==, 1);
    gchar *contents = NULL;
    gsize size = 0;
    g_assert_true(g_file_get_contents(path, &contents, &size, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(size, ==, 304); /* no successful footer */
    g_free(contents);
    dsp_capture_ring_free(ring);
    g_assert_cmpint(g_unlink(path), ==, 0);
    g_assert_cmpint(g_rmdir(dir), ==, 0);
    g_free(path);
    g_free(dir);
}

static int emit_fixture(const char *path)
{
    DspCaptureRing *ring = dsp_capture_ring_new(1);
    DspCaptureFrame *frame = dsp_capture_ring_begin(ring, 0);
    frame->started = frame->finished = true;
    frame->flags |= DSP_CAPTURE_MAPPING_OBSERVED;
    frame->gp_before.is_gp = frame->gp_after.is_gp = true;
    frame->phase = 3;
    const uint8_t data[] = { 1, 2, 3, 4 };
    const uint8_t old[] = { 0, 0, 0, 0 };
    dsp_capture_frame_event(frame, DSP_CAPTURE_SCRATCH, 0, 4, true, 0, data,
                            sizeof(data));
    dsp_capture_frame_event(frame, DSP_CAPTURE_SGE_READ, 0, 0x1000, false, 0,
                            data, sizeof(data));
    dsp_capture_frame_event(frame, DSP_CAPTURE_RAM_PREWRITE, 0, 0x1400, false,
                            0, old, sizeof(old));
    dsp_capture_frame_event(frame, DSP_CAPTURE_RAM_WRITE, 0, 0x1400, true, 0,
                            data, sizeof(data));
    dsp_capture_ring_publish(ring);
    DspCaptureWriter *writer = dsp_capture_writer_start(ring, path);
    if (!writer) {
        dsp_capture_ring_free(ring);
        return 1;
    }
    DspCaptureWriterResult result;
    dsp_capture_writer_stop(writer, &result);
    dsp_capture_ring_free(ring);
    return result.failed || result.written != 1;
}

int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "--emit-fixture")) {
        return emit_fixture(argv[2]);
    }
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/dsp/capture-stream/async-write", test_async_write);
    g_test_add_func("/dsp/capture-stream/no-overwrite",
                    test_existing_file_is_not_overwritten);
    g_test_add_func("/dsp/capture-stream/bad-frame-and-drop",
                    test_bad_frame_and_drop_are_reported);
    return g_test_run();
}

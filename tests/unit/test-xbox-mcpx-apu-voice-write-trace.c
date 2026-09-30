/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "glib/gstdio.h"
#include "hw/xbox/mcpx/apu/vp/voice-write-trace.h"

static char *trace_path(void)
{
    char *path;
    int fd = g_file_open_tmp("xemu-voice-write-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    return path;
}

static void test_counts_and_sample_cost(void)
{
    g_autofree char *path = trace_path();
    McpxApuVoiceWriteTrace *trace = mcpx_apu_voice_write_trace_open(path);
    g_assert_nonnull(trace);
    mcpx_apu_voice_write_trace_record(trace, 0x34, 3, 0xabcd, 0xabcd, true);
    mcpx_apu_voice_write_trace_sample(trace, 0x34, 3, false, 700);
    mcpx_apu_voice_write_trace_record(trace, 0x34, 3, 0xabcd, 0xabce, true);
    mcpx_apu_voice_write_trace_frame(trace, 5);
    mcpx_apu_voice_write_trace_frame(trace, 7);
    mcpx_apu_voice_write_trace_close(trace);
    g_autofree char *text = NULL;
    g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
    g_assert_nonnull(strstr(text, "summary,2,12,7,8\n"));
    g_assert_nonnull(strstr(text, "field,52,3,2,1,1,2,0,0,1,700\n"));
    g_unlink(path);
}

static void test_sparse_sampling_and_phase_separation(void)
{
    g_autofree char *path = trace_path();
    McpxApuVoiceWriteTrace *trace = mcpx_apu_voice_write_trace_open(path);
    unsigned changed_samples = 0, unchanged_samples = 0;
    for (unsigned i = 0; i < 1024 * 1024; i++) {
        bool changed = i & 1;
        if (mcpx_apu_voice_write_trace_record(trace, 0x34, 3, 0, changed,
                                              true)) {
            if (changed) {
                changed_samples++;
            } else {
                unchanged_samples++;
            }
        }
    }
    /* A periodic mixed-class workload must sample both classes sparsely. */
    g_assert_cmpuint(changed_samples, >, 350);
    g_assert_cmpuint(changed_samples, <, 700);
    g_assert_cmpuint(unchanged_samples, >, 350);
    g_assert_cmpuint(unchanged_samples, <, 700);
    mcpx_apu_voice_write_trace_record(trace, 0x34, 11, 0, 0, true);
    mcpx_apu_voice_write_trace_sample(trace, 0x34, 3, true, 100);
    mcpx_apu_voice_write_trace_sample(trace, 0x34, 3, true, 200);
    mcpx_apu_voice_write_trace_close(trace);
    g_autofree char *text = NULL;
    g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
    g_assert_nonnull(
        strstr(text, "field,52,3,1048576,524288,524288,1048576,2,300,0,0\n"));
    g_assert_nonnull(strstr(text, "field,52,11,1,0,1,1,0,0,0,0\n"));
    g_unlink(path);
}

static gpointer concurrent_writer(gpointer data)
{
    McpxApuVoiceWriteTrace *trace = data;
    for (unsigned i = 0; i < 10000; i++) {
        mcpx_apu_voice_write_trace_record(trace, 0x58, 16, 1, 1, true);
    }
    return NULL;
}

static void test_concurrent_writers(void)
{
    g_autofree char *path = trace_path();
    McpxApuVoiceWriteTrace *trace = mcpx_apu_voice_write_trace_open(path);
    GThread *threads[4];
    for (unsigned i = 0; i < 4; i++) {
        threads[i] = g_thread_new("voice-trace", concurrent_writer, trace);
    }
    for (unsigned i = 0; i < 4; i++) {
        g_thread_join(threads[i]);
    }
    mcpx_apu_voice_write_trace_close(trace);
    g_autofree char *text = NULL;
    g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
    g_assert_nonnull(strstr(text, "field,88,16,40000,0,40000,40000,0,0,0,0\n"));
    g_unlink(path);
}

static void test_unsupported_offset_and_disabled_trace(void)
{
    g_assert_null(mcpx_apu_voice_write_trace_open(NULL));
    g_assert_null(mcpx_apu_voice_write_trace_open(""));
    g_assert_null(mcpx_apu_voice_write_trace_open("/no-such-xemu-dir/trace"));
    g_assert_false(mcpx_apu_voice_write_trace_record(NULL, 0, 0, 0, 0, true));
    mcpx_apu_voice_write_trace_sample(NULL, 0, 0, false, 1);
    mcpx_apu_voice_write_trace_frame(NULL, 1);
    mcpx_apu_voice_write_trace_close(NULL);
    g_autofree char *path = trace_path();
    McpxApuVoiceWriteTrace *trace = mcpx_apu_voice_write_trace_open(path);
    mcpx_apu_voice_write_trace_record(trace, 0x80, 20, 0, 1, false);
    mcpx_apu_voice_write_trace_record(trace, 1, 20, 0, 0, false);
    mcpx_apu_voice_write_trace_close(trace);
    g_autofree char *text = NULL;
    g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
    g_assert_nonnull(strstr(text, "field,128,16,2,1,1,0,0,0,0,0\n"));
    g_unlink(path);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/apu/voice-write/counts",
                    test_counts_and_sample_cost);
    g_test_add_func("/xbox/apu/voice-write/sampling",
                    test_sparse_sampling_and_phase_separation);
    g_test_add_func("/xbox/apu/voice-write/concurrent",
                    test_concurrent_writers);
    g_test_add_func("/xbox/apu/voice-write/disabled-and-range",
                    test_unsupported_offset_and_disabled_trace);
    return g_test_run();
}

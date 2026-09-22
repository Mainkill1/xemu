/*
 * MCPX APU performance telemetry tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include <glib/gstdio.h>

#include "hw/xbox/mcpx/apu/perf.h"

static void test_disabled(void)
{
    McpxApuPerfTelemetry perf;

    g_assert_false(mcpx_apu_perf_init(&perf, NULL, 4, 0));
    mcpx_apu_perf_record_worker(&perf, 0, 2, 100, 20, 140);
    mcpx_apu_perf_record_dispatch(&perf, 2, 1, 3, 5, 120, 160);
    mcpx_apu_perf_record_audio_queue(&perf, 4096, 2048, 6144);
    mcpx_apu_perf_record_frame(&perf, 300, 1000);
    mcpx_apu_perf_finalize(&perf, 1000);
}

static void test_cumulative_jsonl(void)
{
    g_autofree char *path = NULL;
    g_autofree char *contents = NULL;
    McpxApuPerfTelemetry perf;
    size_t length;
    int fd;

    fd = g_file_open_tmp("xemu-apu-perf-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);

    g_assert_true(mcpx_apu_perf_init(&perf, path, 4, 100));
    mcpx_apu_perf_record_worker(&perf, 0, 3, 100, 20, 140);
    mcpx_apu_perf_record_worker(&perf, 1, 0, 0, 0, 0);
    mcpx_apu_perf_record_worker(&perf, 0, 2, 80, 10, 110);
    mcpx_apu_perf_record_dispatch(&perf, 5, 2, 3, 7, 150, 210);
    mcpx_apu_perf_record_audio_queue(&perf, 1024, 2048, 6144);
    mcpx_apu_perf_record_audio_queue(&perf, 8192, 2048, 6144);
    mcpx_apu_perf_record_frame(&perf, 700, 500000);
    mcpx_apu_perf_record_frame(&perf, 600, 1000200);
    mcpx_apu_perf_finalize(&perf, 1000300);

    g_assert_true(g_file_get_contents(path, &contents, &length, NULL));
    g_assert_cmpuint(length, >, 0);
    g_assert_nonnull(strstr(contents, "\"type\":\"schema\""));
    g_assert_nonnull(strstr(contents, "\"schema_version\":1"));
    g_assert_nonnull(strstr(contents, "\"num_workers\":4"));
    g_assert_nonnull(strstr(contents, "\"type\":\"sample\""));
    g_assert_nonnull(strstr(contents, "\"frames\":2"));
    g_assert_nonnull(strstr(contents, "\"frame_budget_overruns\":1"));
    g_assert_nonnull(strstr(contents, "\"dispatches\":1"));
    g_assert_nonnull(strstr(contents, "\"queued_voices\":5"));
    g_assert_nonnull(strstr(contents, "\"scheduled_workers\":2"));
    g_assert_nonnull(strstr(contents, "\"worker_wakeups\":3"));
    g_assert_nonnull(strstr(contents, "\"useful_worker_wakeups\":2"));
    g_assert_nonnull(strstr(contents, "\"empty_worker_wakeups\":1"));
    g_assert_nonnull(strstr(contents, "\"audio_queue_samples\":2"));
    g_assert_nonnull(strstr(contents, "\"audio_low_watermark_samples\":1"));
    g_assert_nonnull(strstr(contents, "\"audio_high_watermark_samples\":1"));
    g_assert_nonnull(strstr(contents, "\"assigned_voices\":[5,0,0,0]"));
    g_assert_nonnull(strstr(contents, "\"worker_processing_us\":[180,0,0,0]"));
    g_assert_nonnull(strstr(contents, "\"worker_reduction_us\":[30,0,0,0]"));
    g_assert_nonnull(strstr(contents, "\"worker_total_us\":[250,0,0,0]"));
    g_assert_nonnull(strstr(contents, "\"worker_max_us\":[140,0,0,0]"));

    g_unlink(path);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/mcpx/apu/perf/disabled", test_disabled);
    g_test_add_func("/mcpx/apu/perf/cumulative-jsonl", test_cumulative_jsonl);

    return g_test_run();
}

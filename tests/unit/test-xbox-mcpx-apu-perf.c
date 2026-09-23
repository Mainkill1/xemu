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
    mcpx_apu_perf_record_dispatch(&perf, 2, 1, 1, 1, 0, 3, 5, 120, 160);
    mcpx_apu_perf_record_audio_queue(&perf, 4096, 2048, 6144);
    mcpx_apu_perf_record_frame(&perf, 300, 1000);
    mcpx_apu_perf_finalize(&perf, 1000);
}

static void test_rate_buckets(void)
{
    McpxApuPerfTelemetry perf = { .enabled = true };
    McpxApuPerfRateBatch batch = { 0 };
    uint64_t dispatch = mcpx_apu_perf_begin_dispatch(&perf);

    mcpx_apu_perf_record_voice_rate(&perf, &batch, 0, false, 1.0f,
                                    dispatch);
    mcpx_apu_perf_record_voice_rate(&perf, &batch, 1, true, 1.0f,
                                    dispatch);
    mcpx_apu_perf_record_voice_rate(&perf, &batch, 2, false, 0.99f,
                                    dispatch);
    mcpx_apu_perf_record_voice_rate(&perf, &batch, 3, false, 1.01f,
                                    dispatch);
    mcpx_apu_perf_record_voice_rate(&perf, &batch, 4, false, 0.989f,
                                    dispatch);
    mcpx_apu_perf_record_voice_rate(&perf, &batch, 5, false, 1.011f,
                                    dispatch);
    mcpx_apu_perf_record_voice_rate(&perf, &batch, 6, false, NAN,
                                    dispatch);

    g_assert_cmpuint(batch.samples, ==, 7);
    g_assert_cmpuint(batch.exact_unity, ==, 2);
    g_assert_cmpuint(batch.near_unity, ==, 2);
    g_assert_cmpuint(batch.other, ==, 3);
    g_assert_cmpuint(batch.exact_unity_mono, ==, 1);
    g_assert_cmpuint(batch.exact_unity_stereo, ==, 1);
    g_assert_cmpuint(batch.observation_starts, ==, 7);
    g_assert_cmpuint(batch.exact_unity_starts, ==, 2);
    g_assert_cmpuint(batch.class_transitions, ==, 0);
    g_assert_cmpuint(batch.exact_unity_entries, ==, 0);
    g_assert_cmpuint(batch.exact_unity_exits, ==, 0);
}

static void test_rate_transition_accounting(void)
{
    McpxApuPerfTelemetry perf = { .enabled = true };
    McpxApuPerfRateBatch batch = { 0 };
    uint64_t dispatch;

    dispatch = mcpx_apu_perf_begin_dispatch(&perf);
    mcpx_apu_perf_record_voice_rate(&perf, &batch, 0, false, 1.0f,
                                    dispatch);
    mcpx_apu_perf_record_voice_rate(&perf, &batch, 1, true, 0.75f,
                                    dispatch);

    dispatch = mcpx_apu_perf_begin_dispatch(&perf);
    mcpx_apu_perf_record_voice_rate(&perf, &batch, 0, false, 0.995f,
                                    dispatch);
    mcpx_apu_perf_record_voice_rate(&perf, &batch, 1, true, 1.0f,
                                    dispatch);

    dispatch = mcpx_apu_perf_begin_dispatch(&perf);
    mcpx_apu_perf_record_voice_rate(&perf, &batch, 0, false, 1.0f,
                                    dispatch);
    mcpx_apu_perf_record_voice_rate(&perf, &batch, 1, true, 0.75f,
                                    dispatch);

    mcpx_apu_perf_begin_dispatch(&perf);
    dispatch = mcpx_apu_perf_begin_dispatch(&perf);
    mcpx_apu_perf_record_voice_rate(&perf, &batch, 0, false, 1.0f,
                                    dispatch);

    g_assert_cmpuint(batch.samples, ==, 7);
    g_assert_cmpuint(batch.exact_unity, ==, 4);
    g_assert_cmpuint(batch.near_unity, ==, 1);
    g_assert_cmpuint(batch.other, ==, 2);
    g_assert_cmpuint(batch.exact_unity_mono, ==, 3);
    g_assert_cmpuint(batch.exact_unity_stereo, ==, 1);
    g_assert_cmpuint(batch.observation_starts, ==, 3);
    g_assert_cmpuint(batch.exact_unity_starts, ==, 2);
    g_assert_cmpuint(batch.class_transitions, ==, 4);
    g_assert_cmpuint(batch.exact_unity_entries, ==, 2);
    g_assert_cmpuint(batch.exact_unity_exits, ==, 2);
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
    McpxApuPerfRateBatch rate_batch = { 0 };
    uint64_t dispatch = mcpx_apu_perf_begin_dispatch(&perf);
    mcpx_apu_perf_record_voice_rate(&perf, &rate_batch, 0, false, 1.0f,
                                    dispatch);
    mcpx_apu_perf_record_voice_rate(&perf, &rate_batch, 1, true, 0.995f,
                                    dispatch);
    mcpx_apu_perf_merge_rate_batch(&perf, &rate_batch);
    mcpx_apu_perf_record_worker(&perf, 0, 3, 100, 20, 140);
    mcpx_apu_perf_record_worker(&perf, 1, 0, 0, 0, 0);
    mcpx_apu_perf_record_worker(&perf, 0, 2, 80, 10, 110);
    mcpx_apu_perf_record_dispatch(&perf, 5, 2, 3, 1, 1, 3, 7, 150, 210);
    mcpx_apu_perf_record_audio_queue(&perf, 1024, 2048, 6144);
    mcpx_apu_perf_record_audio_queue(&perf, 8192, 2048, 6144);
    mcpx_apu_perf_record_frame(&perf, 700, 500000);
    mcpx_apu_perf_record_frame(&perf, 600, 1000200);
    mcpx_apu_perf_finalize(&perf, 1000300);

    g_assert_true(g_file_get_contents(path, &contents, &length, NULL));
    g_assert_cmpuint(length, >, 0);
    g_assert_nonnull(strstr(contents, "\"type\":\"schema\""));
    g_assert_nonnull(strstr(contents, "\"schema_version\":3"));
    g_assert_nonnull(strstr(contents, "\"num_workers\":4"));
    g_assert_nonnull(strstr(contents, "\"type\":\"sample\""));
    g_assert_nonnull(strstr(contents, "\"frames\":2"));
    g_assert_nonnull(strstr(contents, "\"frame_budget_overruns\":1"));
    g_assert_nonnull(strstr(contents, "\"dispatches\":1"));
    g_assert_nonnull(strstr(contents, "\"queued_voices\":5"));
    g_assert_nonnull(strstr(contents, "\"resampled_mono_voices\":3"));
    g_assert_nonnull(strstr(contents, "\"resampled_stereo_voices\":1"));
    g_assert_nonnull(strstr(contents, "\"multipass_voices\":1"));
    g_assert_nonnull(strstr(contents, "\"resampler_rate_samples\":2"));
    g_assert_nonnull(strstr(contents,
                            "\"resampler_rate_exact_unity\":1"));
    g_assert_nonnull(strstr(contents,
                            "\"resampler_rate_near_unity\":1"));
    g_assert_nonnull(strstr(contents, "\"resampler_rate_other\":0"));
    g_assert_nonnull(strstr(contents,
                            "\"resampler_rate_exact_unity_mono\":1"));
    g_assert_nonnull(strstr(contents,
                            "\"resampler_rate_exact_unity_stereo\":0"));
    g_assert_nonnull(strstr(contents,
                            "\"resampler_rate_observation_starts\":2"));
    g_assert_nonnull(strstr(contents,
                            "\"resampler_rate_exact_unity_starts\":1"));
    g_assert_nonnull(strstr(contents,
                            "\"resampler_rate_class_transitions\":0"));
    g_assert_nonnull(strstr(contents,
                            "\"resampler_rate_exact_unity_entries\":0"));
    g_assert_nonnull(strstr(contents,
                            "\"resampler_rate_exact_unity_exits\":0"));
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
    g_test_add_func("/mcpx/apu/perf/rate-buckets", test_rate_buckets);
    g_test_add_func("/mcpx/apu/perf/rate-transitions",
                    test_rate_transition_accounting);
    g_test_add_func("/mcpx/apu/perf/cumulative-jsonl", test_cumulative_jsonl);

    return g_test_run();
}

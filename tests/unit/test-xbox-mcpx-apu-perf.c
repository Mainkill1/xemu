/*
 * MCPX APU performance telemetry tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include <math.h>
#include <glib/gstdio.h>
#ifdef __linux__
#include <fcntl.h>
#endif

#include "qapi/error.h"
#include "qobject/qjson.h"
#include "qobject/qobject.h"
#include "hw/xbox/mcpx/apu/perf.h"

static void test_disabled(void)
{
    McpxApuPerfTelemetry perf;
    McpxApuPerfRateBatch batch = { 0 };

    g_assert_false(mcpx_apu_perf_init(&perf, NULL, 4, 0));
    uint64_t dispatch = mcpx_apu_perf_begin_dispatch(&perf);
    g_assert_cmpuint(dispatch, ==, 0);
    mcpx_apu_perf_record_voice_rate(&perf, &batch, 0, false, 1.0f,
                                    dispatch);
    mcpx_apu_perf_merge_rate_batch(&perf, &batch);
    g_assert_cmpuint(batch.samples, ==, 0);
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
    mcpx_apu_perf_record_frame(&perf, 200, 1000250);
    mcpx_apu_perf_finalize(&perf, 1000300);

    g_assert_true(g_file_get_contents(path, &contents, &length, NULL));
    g_assert_cmpuint(length, >, 0);
    g_assert_nonnull(strstr(contents, "\"type\":\"schema\""));
    g_assert_nonnull(strstr(contents, "\"schema_version\":4"));
    g_assert_nonnull(strstr(contents, "\"num_workers\":4"));
    g_assert_nonnull(strstr(contents, "\"type\":\"sample\""));
    g_assert_nonnull(strstr(contents, "\"frames\":3"));
    g_assert_nonnull(strstr(contents, "\"timestamp_us\":1000300"));
    g_assert_nonnull(strstr(contents, "\"dropped_samples\":0"));
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

    g_auto(GStrv) lines = g_strsplit(contents, "\n", -1);
    for (int i = 0; lines[i] && lines[i][0]; i++) {
        QObject *obj = qobject_from_json(lines[i], &error_abort);
        g_assert_nonnull(obj);
        qobject_unref(obj);
    }

    g_unlink(path);
}

static void test_failed_open_is_reported_after_finalize(void)
{
    McpxApuPerfTelemetry perf;
    g_autofree char *dir = g_dir_make_tmp("xemu-apu-perf-XXXXXX", NULL);
    g_autofree char *path = NULL;

    g_assert_nonnull(dir);
    path = g_build_filename(dir, "missing", "log", NULL);
    g_assert_true(mcpx_apu_perf_init(&perf, path, 2, 0));
    mcpx_apu_perf_record_frame(&perf, 100, G_USEC_PER_SEC);
    mcpx_apu_perf_finalize(&perf, G_USEC_PER_SEC + 1);
    g_assert_cmpint(perf.sink_error, ==, MCPX_APU_PERF_SINK_OPEN);
    g_assert_cmpint(perf.sink_errno, ==, ENOENT);
    g_assert_cmpuint(perf.dropped_samples, >, 0);
    g_assert_cmpint(g_rmdir(dir), ==, 0);
}

#ifdef __linux__
static void test_failed_flush_is_reported_after_finalize(void)
{
    McpxApuPerfTelemetry perf;

    if (!g_file_test("/dev/full", G_FILE_TEST_EXISTS)) {
        g_test_skip("/dev/full is unavailable");
        return;
    }
    g_assert_true(mcpx_apu_perf_init(&perf, "/dev/full", 2, 0));
    mcpx_apu_perf_record_frame(&perf, 100, G_USEC_PER_SEC);
    mcpx_apu_perf_finalize(&perf, G_USEC_PER_SEC + 1);
    g_assert_cmpint(perf.sink_error, ==, MCPX_APU_PERF_SINK_FLUSH);
    g_assert_cmpint(perf.sink_errno, ==, ENOSPC);
}

static gpointer drain_pipe(gpointer opaque)
{
    int fd = GPOINTER_TO_INT(opaque);
    char buf[4096];

    while (read(fd, buf, sizeof(buf)) > 0) {
    }
    return NULL;
}

typedef struct PerfProducer {
    McpxApuPerfTelemetry *perf;
    GMutex lock;
    GCond cond;
    bool complete;
} PerfProducer;

static gpointer produce_frames(gpointer opaque)
{
    PerfProducer *producer = opaque;

    for (int i = 1; i <= 1000; i++) {
        mcpx_apu_perf_record_frame(producer->perf, 100,
                                   i * G_USEC_PER_SEC);
    }
    g_mutex_lock(&producer->lock);
    producer->complete = true;
    g_cond_signal(&producer->cond);
    g_mutex_unlock(&producer->lock);
    return NULL;
}

static void test_blocked_sink_drops_without_blocking_producer(void)
{
    McpxApuPerfTelemetry perf;
    PerfProducer producer = { .perf = &perf };
    int fds[2];
    char filler[4096] = { 0 };
    g_autofree char *path = NULL;
    GThread *producer_thread, *drainer;
    bool completed_before_drain;
    int flags;

    g_assert_cmpint(pipe(fds), ==, 0);
    flags = fcntl(fds[1], F_GETFL);
    g_assert_cmpint(fcntl(fds[1], F_SETFL, flags | O_NONBLOCK), ==, 0);
    while (write(fds[1], filler, sizeof(filler)) > 0) {
    }
    g_assert_cmpint(errno, ==, EAGAIN);
    g_assert_cmpint(fcntl(fds[1], F_SETFL, flags), ==, 0);

    path = g_strdup_printf("/proc/self/fd/%d", fds[1]);
    g_assert_true(mcpx_apu_perf_init(&perf, path, 2, 0));

    g_mutex_init(&producer.lock);
    g_cond_init(&producer.cond);
    producer_thread = g_thread_new("apu-perf-producer", produce_frames,
                                   &producer);
    g_mutex_lock(&producer.lock);
    int64_t deadline = g_get_monotonic_time() + 5 * G_USEC_PER_SEC;
    while (!producer.complete &&
           g_cond_wait_until(&producer.cond, &producer.lock, deadline)) {
    }
    completed_before_drain = producer.complete;
    g_mutex_unlock(&producer.lock);

    drainer = g_thread_new("apu-perf-drain", drain_pipe,
                           GINT_TO_POINTER(fds[0]));
    g_thread_join(producer_thread);
    mcpx_apu_perf_finalize(&perf, 1001 * G_USEC_PER_SEC);
    close(fds[1]);
    g_thread_join(drainer);
    close(fds[0]);
    g_cond_clear(&producer.cond);
    g_mutex_clear(&producer.lock);

    g_assert_true(completed_before_drain);
    g_assert_cmpuint(perf.dropped_samples, >, 0);
    g_assert_cmpint(perf.sink_error, ==, 0);
}
#endif

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/mcpx/apu/perf/disabled", test_disabled);
    g_test_add_func("/mcpx/apu/perf/rate-buckets", test_rate_buckets);
    g_test_add_func("/mcpx/apu/perf/rate-transitions",
                    test_rate_transition_accounting);
    g_test_add_func("/mcpx/apu/perf/cumulative-jsonl", test_cumulative_jsonl);
    g_test_add_func("/mcpx/apu/perf/failed-open",
                    test_failed_open_is_reported_after_finalize);
#ifdef __linux__
    g_test_add_func("/mcpx/apu/perf/failed-flush",
                    test_failed_flush_is_reported_after_finalize);
    g_test_add_func("/mcpx/apu/perf/blocked-sink",
                    test_blocked_sink_drops_without_blocking_producer);
#endif

    return g_test_run();
}

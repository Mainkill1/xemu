/*
 * Opt-in Vulkan wait ownership telemetry.
 *
 * No file is opened and each call is a single branch unless XEMU_VK_PERF_LOG
 * is set. Records are aggregated in memory and emitted once per guest frame.
 */

#include "renderer.h"

static const char *finish_reason_names[VK_FINISH_REASON_COUNT] = {
    [VK_FINISH_REASON_VERTEX_BUFFER_DIRTY] = "vertex_buffer_dirty",
    [VK_FINISH_REASON_SURFACE_CREATE] = "surface_create",
    [VK_FINISH_REASON_SURFACE_DOWN] = "surface_down",
    [VK_FINISH_REASON_NEED_BUFFER_SPACE] = "need_buffer_space",
    [VK_FINISH_REASON_FRAMEBUFFER_DIRTY] = "framebuffer_dirty",
    [VK_FINISH_REASON_PRESENTING] = "presenting",
    [VK_FINISH_REASON_FLIP_STALL] = "flip_stall",
    [VK_FINISH_REASON_FLUSH] = "flush",
    [VK_FINISH_REASON_STALLED] = "stalled",
    [VK_FINISH_REASON_TEXTURE_DIRTY] = "texture_dirty",
};

static const char *single_time_reason_names[VK_SINGLE_TIME_REASON_COUNT] = {
    [VK_SINGLE_TIME_PVIDEO_UPLOAD] = "pvideo_upload",
    [VK_SINGLE_TIME_DISPLAY_RENDER] = "display_render",
    [VK_SINGLE_TIME_SURFACE_DOWNLOAD] = "surface_download",
    [VK_SINGLE_TIME_SURFACE_CREATE] = "surface_create",
    [VK_SINGLE_TIME_SURFACE_UPLOAD] = "surface_upload",
    [VK_SINGLE_TIME_TEXTURE_UPLOAD] = "texture_upload",
    [VK_SINGLE_TIME_DUMMY_TEXTURE_CREATE] = "dummy_texture_create",
};

static void write_names(FILE *file, const char *key, const char **names,
                        size_t count)
{
    fprintf(file, ",\"%s\":[", key);
    for (size_t i = 0; i < count; i++) {
        fprintf(file, "%s\"%s\"", i ? "," : "", names[i]);
    }
    fputc(']', file);
}

static void write_stat_array(FILE *file, const char *key,
                             const PGRAPHVkWaitStats *stats, size_t count,
                             size_t member_offset)
{
    fprintf(file, ",\"%s\":[", key);
    for (size_t i = 0; i < count; i++) {
        const uint64_t *value = (const uint64_t *)
            ((const uint8_t *)&stats[i] + member_offset);
        fprintf(file, "%s%" PRIu64, i ? "," : "", *value);
    }
    fputc(']', file);
}

void pgraph_vk_perf_init(PGRAPHVkState *r)
{
    const char *path = g_getenv("XEMU_VK_PERF_LOG");
    if (path == NULL || path[0] == '\0') {
        return;
    }

    r->perf.file = qemu_fopen(path, "w");
    if (r->perf.file == NULL) {
        fprintf(stderr, "nv2a: failed to open Vulkan perf log '%s'\n", path);
        return;
    }
    r->perf.enabled = true;
    r->perf.last_flush_us = qemu_clock_get_us(QEMU_CLOCK_REALTIME);
    fprintf(r->perf.file, "{\"type\":\"schema\",\"schema_version\":1");
    write_names(r->perf.file, "finish_reasons", finish_reason_names,
                ARRAY_SIZE(finish_reason_names));
    write_names(r->perf.file, "single_time_callers", single_time_reason_names,
                ARRAY_SIZE(single_time_reason_names));
    fprintf(r->perf.file, "}\n");
}

void pgraph_vk_perf_finalize(PGRAPHVkState *r)
{
    if (r->perf.file != NULL) {
        fflush(r->perf.file);
        fclose(r->perf.file);
    }
    r->perf.file = NULL;
    r->perf.enabled = false;
}

void pgraph_vk_perf_record_finish_call(PGRAPHVkState *r, FinishReason reason)
{
    assert(reason < VK_FINISH_REASON_COUNT);
    if (r->perf.enabled) {
        r->perf.finish[reason].call_count++;
    }
}

void pgraph_vk_perf_record_finish_submit(PGRAPHVkState *r,
                                         FinishReason reason,
                                         uint64_t submit_cpu_us,
                                         uint64_t wait_us,
                                         uint64_t staged_bytes,
                                         uint64_t submit_info_count,
                                         uint64_t command_buffer_count)
{
    if (!r->perf.enabled) {
        return;
    }
    assert(reason < VK_FINISH_REASON_COUNT);
    PGRAPHVkWaitStats *stats = &r->perf.finish[reason];
    stats->submit_count++;
    stats->submit_cpu_us += submit_cpu_us;
    stats->wait_count++;
    stats->wait_us += wait_us;
    r->perf.submit_info_count += submit_info_count;
    r->perf.command_buffer_count += command_buffer_count;
    r->perf.staged_bytes += staged_bytes;
    r->perf.peak_in_flight_submission_count = MAX(
        r->perf.peak_in_flight_submission_count, 1);
    r->perf.newest_submitted_serial = ++r->perf.submission_serial;
}

void pgraph_vk_perf_record_single_time_submit(PGRAPHVkState *r,
                                               SingleTimeReason reason,
                                               uint64_t submit_cpu_us,
                                               uint64_t wait_us,
                                               uint64_t staged_bytes)
{
    if (!r->perf.enabled) {
        return;
    }
    assert(reason < VK_SINGLE_TIME_REASON_COUNT);
    PGRAPHVkWaitStats *stats = &r->perf.single_time[reason];
    stats->call_count++;
    stats->submit_count++;
    stats->submit_cpu_us += submit_cpu_us;
    stats->wait_count++;
    stats->wait_us += wait_us;
    r->perf.submit_info_count++;
    r->perf.command_buffer_count++;
    r->perf.staged_bytes += staged_bytes;
    r->perf.peak_in_flight_submission_count = MAX(
        r->perf.peak_in_flight_submission_count, 1);
    r->perf.newest_submitted_serial = ++r->perf.submission_serial;
}

void pgraph_vk_perf_frame(PGRAPHVkState *r)
{
    PGRAPHVkPerfTelemetry *perf = &r->perf;
    if (!perf->enabled) {
        return;
    }

    uint64_t submit_count = 0;
    for (size_t i = 0; i < ARRAY_SIZE(perf->finish); i++) {
        submit_count += perf->finish[i].submit_count;
    }
    for (size_t i = 0; i < ARRAY_SIZE(perf->single_time); i++) {
        submit_count += perf->single_time[i].submit_count;
    }
    double staged_bytes_per_submit = submit_count ?
        (double)perf->staged_bytes / submit_count : 0.0;
    double submit_infos_per_submit = submit_count ?
        (double)perf->submit_info_count / submit_count : 0.0;
    double command_buffers_per_submit = submit_count ?
        (double)perf->command_buffer_count / submit_count : 0.0;
    int64_t now = qemu_clock_get_us(QEMU_CLOCK_REALTIME);

    fprintf(perf->file,
            "{\"type\":\"frame\",\"schema_version\":1"
            ",\"timestamp_us\":%" PRId64 ",\"guest_frame\":%" PRIu64,
            now, ++perf->frame);
    write_stat_array(perf->file, "finish_count_per_guest_frame", perf->finish,
                     ARRAY_SIZE(perf->finish),
                     offsetof(PGRAPHVkWaitStats, call_count));
    write_stat_array(perf->file, "finish_submit_count_per_guest_frame",
                     perf->finish, ARRAY_SIZE(perf->finish),
                     offsetof(PGRAPHVkWaitStats, submit_count));
    write_stat_array(perf->file, "finish_submit_cpu_us_per_guest_frame",
                     perf->finish, ARRAY_SIZE(perf->finish),
                     offsetof(PGRAPHVkWaitStats, submit_cpu_us));
    write_stat_array(perf->file, "fence_wait_count_per_guest_frame",
                     perf->finish, ARRAY_SIZE(perf->finish),
                     offsetof(PGRAPHVkWaitStats, wait_count));
    write_stat_array(perf->file, "finish_wait_us_per_guest_frame", perf->finish,
                     ARRAY_SIZE(perf->finish),
                     offsetof(PGRAPHVkWaitStats, wait_us));
    write_stat_array(perf->file, "fence_wait_us_per_guest_frame", perf->finish,
                     ARRAY_SIZE(perf->finish),
                     offsetof(PGRAPHVkWaitStats, wait_us));
    write_stat_array(perf->file, "single_time_submit_count_per_guest_frame",
                     perf->single_time, ARRAY_SIZE(perf->single_time),
                     offsetof(PGRAPHVkWaitStats, submit_count));
    write_stat_array(perf->file, "single_time_submit_cpu_us_per_guest_frame",
                     perf->single_time, ARRAY_SIZE(perf->single_time),
                     offsetof(PGRAPHVkWaitStats, submit_cpu_us));
    write_stat_array(perf->file, "queue_wait_idle_count_per_guest_frame",
                     perf->single_time, ARRAY_SIZE(perf->single_time),
                     offsetof(PGRAPHVkWaitStats, wait_count));
    write_stat_array(perf->file, "single_time_wait_us_per_guest_frame",
                     perf->single_time, ARRAY_SIZE(perf->single_time),
                     offsetof(PGRAPHVkWaitStats, wait_us));
    write_stat_array(perf->file, "queue_wait_idle_us_per_guest_frame",
                     perf->single_time, ARRAY_SIZE(perf->single_time),
                     offsetof(PGRAPHVkWaitStats, wait_us));
    fprintf(perf->file,
            ",\"vk_queue_submit_calls_per_guest_frame\":%" PRIu64
            ",\"vk_submit_infos_per_guest_frame\":%" PRIu64
            ",\"command_buffers_per_guest_frame\":%" PRIu64
            ",\"staged_bytes_per_guest_frame\":%" PRIu64
            ",\"staged_bytes_per_submit\":%.3f"
            ",\"submit_infos_per_submit\":%.3f"
            ",\"command_buffers_per_submit\":%.3f"
            ",\"in_flight_submission_count\":%" PRIu64
            ",\"peak_in_flight_submission_count\":%" PRIu64
            ",\"oldest_in_flight_serial\":%" PRIu64
            ",\"newest_submitted_serial\":%" PRIu64
            ",\"retirement_queue_objects\":%" PRIu64
            ",\"retirement_queue_bytes\":%" PRIu64 "}\n",
            submit_count, perf->submit_info_count, perf->command_buffer_count,
            perf->staged_bytes, staged_bytes_per_submit,
            submit_infos_per_submit, command_buffers_per_submit,
            perf->in_flight_submission_count,
            perf->peak_in_flight_submission_count,
            perf->oldest_in_flight_serial, perf->newest_submitted_serial,
            perf->retirement_queue_objects, perf->retirement_queue_bytes);

    if (now - perf->last_flush_us >= G_USEC_PER_SEC) {
        fflush(perf->file);
        perf->last_flush_us = now;
    }

    memset(perf->finish, 0, sizeof(perf->finish));
    memset(perf->single_time, 0, sizeof(perf->single_time));
    perf->submit_info_count = 0;
    perf->command_buffer_count = 0;
    perf->staged_bytes = 0;
    perf->peak_in_flight_submission_count =
        perf->in_flight_submission_count;
    perf->oldest_in_flight_serial = 0;
    perf->newest_submitted_serial = 0;
    perf->retirement_queue_objects = 0;
    perf->retirement_queue_bytes = 0;
}

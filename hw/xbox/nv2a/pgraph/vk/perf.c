/*
 * Opt-in Vulkan wait ownership telemetry.
 *
 * No file is opened and each call is a single branch unless XEMU_VK_PERF_LOG
 * is set. Records are aggregated in memory and emitted once per guest frame.
 */

#include "renderer.h"

/*
 * Morrowind issues about 102 VERTEX_BUFFER_DIRTY submissions per guest frame.
 * Time the first eight occurrences of every reason so rare paths remain exact,
 * then one in sixteen hot occurrences. This retains roughly fourteen timing
 * samples for that hot path while removing about 86% of its clock reads.
 */
#define VK_PERF_INITIAL_TIMED_SUBMITS 8
#define VK_PERF_HOT_SAMPLE_STRIDE 16

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

static const char *cpu_region_names[VK_PERF_CPU_REGION_COUNT] = {
    [VK_PERF_CPU_DRAW_BEGIN_SURFACE_UPDATE] = "draw_begin_surface_update",
    [VK_PERF_CPU_DRAW_FLUSH] = "draw_flush",
    [VK_PERF_CPU_PIPELINE_PREPARE] = "pipeline_prepare",
    [VK_PERF_CPU_BIND_TEXTURES] = "bind_textures",
    [VK_PERF_CPU_TEXTURE_UPLOAD] = "texture_upload",
    [VK_PERF_CPU_UPDATE_DESCRIPTOR_SETS] = "update_descriptor_sets",
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

static void write_cpu_stat_array(FILE *file, const char *key,
                                 const PGRAPHVkCpuStats *stats,
                                 size_t member_offset)
{
    fprintf(file, ",\"%s\":[", key);
    for (size_t i = 0; i < VK_PERF_CPU_REGION_COUNT; i++) {
        const uint64_t *value = (const uint64_t *)
            ((const uint8_t *)&stats[i] + member_offset);
        fprintf(file, "%s%" PRIu64, i ? "," : "", *value);
    }
    fputc(']', file);
}

static bool env_enabled(const char *name)
{
    return g_strcmp0(g_getenv(name), "1") == 0;
}

static void write_perf_frame(FILE *file, const PGRAPHVkPerfFrame *frame);

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
    r->perf.full_timing = env_enabled("XEMU_VK_PERF_FULL_TIMING");
    r->perf.deferred = env_enabled("XEMU_VK_PERF_DEFERRED");
    if (r->perf.deferred) {
        r->perf.deferred_frames = g_try_new0(
            PGRAPHVkPerfFrame, VK_PERF_DEFERRED_FRAME_CAPACITY);
        if (r->perf.deferred_frames == NULL) {
            r->perf.deferred_allocation_failed = true;
            fprintf(stderr, "nv2a: failed to allocate Vulkan deferred perf frames\n");
        }
    }
    r->perf.last_flush_us = qemu_clock_get_us(QEMU_CLOCK_REALTIME);
    fprintf(r->perf.file,
            "{\"type\":\"schema\",\"schema_version\":6"
            ",\"duration_sampling\":{\"initial_per_reason_per_frame\":%u"
            ",\"hot_stride\":%u",
            VK_PERF_INITIAL_TIMED_SUBMITS, VK_PERF_HOT_SAMPLE_STRIDE);
    if (r->perf.full_timing) {
        fprintf(r->perf.file, ",\"mode\":\"all\"");
    }
    fputc('}', r->perf.file);
    if (r->perf.deferred) {
        fprintf(r->perf.file,
                ",\"deferred_capture\":{\"capacity\":%u"
                ",\"allocation_failed\":%s}",
                VK_PERF_DEFERRED_FRAME_CAPACITY,
                r->perf.deferred_allocation_failed ? "true" : "false");
    }
    write_names(r->perf.file, "finish_reasons", finish_reason_names,
                ARRAY_SIZE(finish_reason_names));
    write_names(r->perf.file, "single_time_callers", single_time_reason_names,
                ARRAY_SIZE(single_time_reason_names));
    write_names(r->perf.file, "cpu_regions", cpu_region_names,
                ARRAY_SIZE(cpu_region_names));
    fprintf(r->perf.file,
            ",\"need_buffer_space_capacity_triggers\":{\"schema_version\":1"
            ",\"descriptor_only\":\"need_descriptor_write_reset && !need_ubo_staging_buffer_reset\""
            ",\"ubo_staging_only\":\"!need_descriptor_write_reset && need_ubo_staging_buffer_reset\""
            ",\"both\":\"need_descriptor_write_reset && need_ubo_staging_buffer_reset\"}");
    fprintf(r->perf.file, "}\n");
}

void pgraph_vk_perf_finalize(PGRAPHVkState *r)
{
    PGRAPHVkPerfTelemetry *perf = &r->perf;

    if (perf->file != NULL) {
        if (perf->deferred) {
            for (uint32_t i = 0; i < perf->deferred_frame_count; i++) {
                write_perf_frame(perf->file, &perf->deferred_frames[i]);
            }
            fprintf(perf->file,
                    "{\"type\":\"deferred_capture\",\"schema_version\":6"
                    ",\"capacity\":%u,\"stored_frames\":%u"
                    ",\"dropped_frames\":%u,\"overflow\":%s"
                    ",\"incomplete\":%s}\n",
                    VK_PERF_DEFERRED_FRAME_CAPACITY,
                    perf->deferred_frame_count, perf->deferred_frame_dropped,
                    perf->deferred_frame_dropped ? "true" : "false",
                    (perf->deferred_frame_dropped ||
                     perf->deferred_allocation_failed) ? "true" : "false");
        }
        if (fflush(perf->file) != 0 || ferror(perf->file)) {
            fprintf(stderr, "nv2a: failed to write Vulkan perf log\n");
        }
        if (fclose(perf->file) != 0) {
            fprintf(stderr, "nv2a: failed to close Vulkan perf log\n");
        }
    }
    g_free(perf->deferred_frames);
    perf->deferred_frames = NULL;
    perf->file = NULL;
    perf->enabled = false;
}

void pgraph_vk_perf_record_finish_call(PGRAPHVkState *r, FinishReason reason)
{
    assert(reason < VK_FINISH_REASON_COUNT);
    if (r->perf.enabled) {
        r->perf.finish[reason].call_count++;
    }
}

bool pgraph_vk_perf_should_time_finish(PGRAPHVkState *r, FinishReason reason)
{
    if (!r->perf.enabled) {
        return false;
    }
    assert(reason < VK_FINISH_REASON_COUNT);
    if (r->perf.full_timing) {
        return true;
    }
    uint64_t occurrence = r->perf.finish[reason].call_count;
    return occurrence <= VK_PERF_INITIAL_TIMED_SUBMITS ||
           occurrence % VK_PERF_HOT_SAMPLE_STRIDE == 0;
}

void pgraph_vk_perf_record_finish_submit(PGRAPHVkState *r,
                                         FinishReason reason,
                                         bool timed,
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
    stats->wait_count++;
    if (timed) {
        stats->timed_submit_count++;
        stats->submit_cpu_us += submit_cpu_us;
        stats->wait_us += wait_us;
    }
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
    stats->timed_submit_count++;
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

void pgraph_vk_perf_record_vertex_staging_copy(PGRAPHVkState *r,
                                                uint64_t bytes)
{
    if (r->perf.enabled) {
        r->perf.vertex_staged_bytes += bytes;
        r->perf.vertex_staging_copy_count++;
    }
}

void pgraph_vk_perf_record_vertex_staging_growth(PGRAPHVkState *r)
{
    if (r->perf.enabled) {
        r->perf.vertex_staging_capacity_growth_count++;
    }
}

void pgraph_vk_perf_record_vertex_staging_fallback(PGRAPHVkState *r)
{
    if (r->perf.enabled) {
        r->perf.vertex_staging_fallback_finish_count++;
    }
}

void pgraph_vk_perf_record_bc_upload(PGRAPHVkState *r, bool native,
                                     uint64_t source_bytes,
                                     uint64_t staged_bytes,
                                     uint64_t prepare_cpu_us)
{
    if (!r->perf.enabled) {
        return;
    }
    if (native) {
        r->perf.native_bc_upload_count++;
        r->perf.native_bc_source_bytes += source_bytes;
        r->perf.native_bc_staged_bytes += staged_bytes;
        r->perf.native_bc_prepare_cpu_us += prepare_cpu_us;
    } else {
        r->perf.decoded_bc_upload_count++;
        r->perf.decoded_bc_source_bytes += source_bytes;
        r->perf.decoded_bc_staged_bytes += staged_bytes;
        r->perf.decoded_bc_prepare_cpu_us += prepare_cpu_us;
    }
}

void pgraph_vk_perf_record_cpu_region(PGRAPHVkState *r, PerfCpuRegion region,
                                      uint64_t cpu_us)
{
    assert(region < VK_PERF_CPU_REGION_COUNT);
    if (r->perf.enabled) {
        r->perf.cpu_regions[region].call_count++;
        r->perf.cpu_regions[region].cpu_us += cpu_us;
    }
}

void pgraph_vk_perf_record_need_buffer_space_capacity(
    PGRAPHVkState *r, bool descriptor_capacity, bool ubo_staging_capacity)
{
    if (!r->perf.enabled) {
        return;
    }
    assert(descriptor_capacity || ubo_staging_capacity);
    if (descriptor_capacity) {
        if (ubo_staging_capacity) {
            r->perf.need_buffer_space_both_count++;
        } else {
            r->perf.need_buffer_space_descriptor_only_count++;
        }
    } else {
        r->perf.need_buffer_space_ubo_only_count++;
    }
}

static void capture_perf_frame(PGRAPHVkState *r, PGRAPHVkPerfFrame *frame)
{
    PGRAPHVkPerfTelemetry *perf = &r->perf;

    *frame = (PGRAPHVkPerfFrame) {
        .timestamp_us = qemu_clock_get_us(QEMU_CLOCK_REALTIME),
        .guest_frame = ++perf->frame,
        .submit_info_count = perf->submit_info_count,
        .command_buffer_count = perf->command_buffer_count,
        .staged_bytes = perf->staged_bytes,
        .vertex_staged_bytes = perf->vertex_staged_bytes,
        .vertex_staging_copy_count = perf->vertex_staging_copy_count,
        .vertex_staging_capacity =
            r->storage_buffers[BUFFER_VERTEX_RAM_STAGING].buffer_size,
        .vertex_staging_capacity_growth_count =
            perf->vertex_staging_capacity_growth_count,
        .vertex_staging_fallback_finish_count =
            perf->vertex_staging_fallback_finish_count,
        .native_bc_upload_count = perf->native_bc_upload_count,
        .native_bc_source_bytes = perf->native_bc_source_bytes,
        .native_bc_staged_bytes = perf->native_bc_staged_bytes,
        .native_bc_prepare_cpu_us = perf->native_bc_prepare_cpu_us,
        .decoded_bc_upload_count = perf->decoded_bc_upload_count,
        .decoded_bc_source_bytes = perf->decoded_bc_source_bytes,
        .decoded_bc_staged_bytes = perf->decoded_bc_staged_bytes,
        .decoded_bc_prepare_cpu_us = perf->decoded_bc_prepare_cpu_us,
        .in_flight_submission_count = perf->in_flight_submission_count,
        .peak_in_flight_submission_count =
            perf->peak_in_flight_submission_count,
        .oldest_in_flight_serial = perf->oldest_in_flight_serial,
        .newest_submitted_serial = perf->newest_submitted_serial,
        .retirement_queue_objects = perf->retirement_queue_objects,
        .retirement_queue_bytes = perf->retirement_queue_bytes,
        .need_buffer_space_descriptor_only_count =
            perf->need_buffer_space_descriptor_only_count,
        .need_buffer_space_ubo_only_count =
            perf->need_buffer_space_ubo_only_count,
        .need_buffer_space_both_count = perf->need_buffer_space_both_count,
    };
    memcpy(frame->finish, perf->finish, sizeof(frame->finish));
    memcpy(frame->single_time, perf->single_time, sizeof(frame->single_time));
    memcpy(frame->cpu_regions, perf->cpu_regions, sizeof(frame->cpu_regions));
}

static void write_perf_frame(FILE *file, const PGRAPHVkPerfFrame *frame)
{
    uint64_t submit_count = 0;
    for (size_t i = 0; i < ARRAY_SIZE(frame->finish); i++) {
        submit_count += frame->finish[i].submit_count;
    }
    for (size_t i = 0; i < ARRAY_SIZE(frame->single_time); i++) {
        submit_count += frame->single_time[i].submit_count;
    }
    double staged_bytes_per_submit = submit_count ?
        (double)frame->staged_bytes / submit_count : 0.0;
    double submit_infos_per_submit = submit_count ?
        (double)frame->submit_info_count / submit_count : 0.0;
    double command_buffers_per_submit = submit_count ?
        (double)frame->command_buffer_count / submit_count : 0.0;

    fprintf(file,
            "{\"type\":\"frame\",\"schema_version\":6"
            ",\"timestamp_us\":%" PRId64 ",\"guest_frame\":%" PRIu64,
            frame->timestamp_us, frame->guest_frame);
    write_stat_array(file, "finish_count_per_guest_frame", frame->finish,
                     ARRAY_SIZE(frame->finish),
                     offsetof(PGRAPHVkWaitStats, call_count));
    write_stat_array(file, "finish_submit_count_per_guest_frame",
                     frame->finish, ARRAY_SIZE(frame->finish),
                     offsetof(PGRAPHVkWaitStats, submit_count));
    write_stat_array(file, "finish_timed_submit_count_per_guest_frame",
                     frame->finish, ARRAY_SIZE(frame->finish),
                     offsetof(PGRAPHVkWaitStats, timed_submit_count));
    write_stat_array(file, "finish_sampled_submit_cpu_us_per_guest_frame",
                     frame->finish, ARRAY_SIZE(frame->finish),
                     offsetof(PGRAPHVkWaitStats, submit_cpu_us));
    write_stat_array(file, "fence_wait_count_per_guest_frame", frame->finish,
                     ARRAY_SIZE(frame->finish),
                     offsetof(PGRAPHVkWaitStats, wait_count));
    write_stat_array(file, "finish_sampled_wait_us_per_guest_frame",
                     frame->finish, ARRAY_SIZE(frame->finish),
                     offsetof(PGRAPHVkWaitStats, wait_us));
    write_stat_array(file, "single_time_submit_count_per_guest_frame",
                     frame->single_time, ARRAY_SIZE(frame->single_time),
                     offsetof(PGRAPHVkWaitStats, submit_count));
    write_stat_array(file, "single_time_timed_submit_count_per_guest_frame",
                     frame->single_time, ARRAY_SIZE(frame->single_time),
                     offsetof(PGRAPHVkWaitStats, timed_submit_count));
    write_stat_array(file,
                     "single_time_sampled_submit_cpu_us_per_guest_frame",
                     frame->single_time, ARRAY_SIZE(frame->single_time),
                     offsetof(PGRAPHVkWaitStats, submit_cpu_us));
    write_stat_array(file, "queue_wait_idle_count_per_guest_frame",
                     frame->single_time, ARRAY_SIZE(frame->single_time),
                     offsetof(PGRAPHVkWaitStats, wait_count));
    write_stat_array(file, "single_time_sampled_wait_us_per_guest_frame",
                     frame->single_time, ARRAY_SIZE(frame->single_time),
                     offsetof(PGRAPHVkWaitStats, wait_us));
    write_cpu_stat_array(file, "cpu_region_calls_per_guest_frame",
                         frame->cpu_regions,
                         offsetof(PGRAPHVkCpuStats, call_count));
    write_cpu_stat_array(file, "cpu_region_us_per_guest_frame",
                         frame->cpu_regions,
                         offsetof(PGRAPHVkCpuStats, cpu_us));
    fprintf(file,
            ",\"vk_queue_submit_calls_per_guest_frame\":%" PRIu64
            ",\"vk_submit_infos_per_guest_frame\":%" PRIu64
            ",\"command_buffers_per_guest_frame\":%" PRIu64
            ",\"staged_bytes_per_guest_frame\":%" PRIu64
            ",\"vertex_staged_bytes_per_guest_frame\":%" PRIu64
            ",\"vertex_staging_copies_per_guest_frame\":%" PRIu64
            ",\"vertex_staging_capacity_bytes\":%zu"
            ",\"vertex_staging_capacity_growths_per_guest_frame\":%" PRIu64
            ",\"vertex_staging_fallback_finishes_per_guest_frame\":%" PRIu64
            ",\"native_bc_uploads_per_guest_frame\":%" PRIu64
            ",\"native_bc_source_bytes_per_guest_frame\":%" PRIu64
            ",\"native_bc_staged_bytes_per_guest_frame\":%" PRIu64
            ",\"native_bc_prepare_cpu_us_per_guest_frame\":%" PRIu64
            ",\"decoded_bc_uploads_per_guest_frame\":%" PRIu64
            ",\"decoded_bc_source_bytes_per_guest_frame\":%" PRIu64
            ",\"decoded_bc_staged_bytes_per_guest_frame\":%" PRIu64
            ",\"decoded_bc_prepare_cpu_us_per_guest_frame\":%" PRIu64
            ",\"staged_bytes_per_submit\":%.3f"
            ",\"submit_infos_per_submit\":%.3f"
            ",\"command_buffers_per_submit\":%.3f"
            ",\"in_flight_submission_count\":%" PRIu64
            ",\"peak_in_flight_submission_count\":%" PRIu64
            ",\"oldest_in_flight_serial\":%" PRIu64
            ",\"newest_submitted_serial\":%" PRIu64
            ",\"retirement_queue_objects\":%" PRIu64
            ",\"retirement_queue_bytes\":%" PRIu64
            ",\"need_buffer_space_capacity_triggers_per_guest_frame\":{\"descriptor_only\":%" PRIu64
            ",\"ubo_staging_only\":%" PRIu64
            ",\"both\":%" PRIu64 "}}\n",
            submit_count, frame->submit_info_count, frame->command_buffer_count,
            frame->staged_bytes, frame->vertex_staged_bytes,
            frame->vertex_staging_copy_count, frame->vertex_staging_capacity,
            frame->vertex_staging_capacity_growth_count,
            frame->vertex_staging_fallback_finish_count,
            frame->native_bc_upload_count, frame->native_bc_source_bytes,
            frame->native_bc_staged_bytes, frame->native_bc_prepare_cpu_us,
            frame->decoded_bc_upload_count, frame->decoded_bc_source_bytes,
            frame->decoded_bc_staged_bytes, frame->decoded_bc_prepare_cpu_us,
            staged_bytes_per_submit, submit_infos_per_submit,
            command_buffers_per_submit, frame->in_flight_submission_count,
            frame->peak_in_flight_submission_count,
            frame->oldest_in_flight_serial, frame->newest_submitted_serial,
            frame->retirement_queue_objects, frame->retirement_queue_bytes,
            frame->need_buffer_space_descriptor_only_count,
            frame->need_buffer_space_ubo_only_count,
            frame->need_buffer_space_both_count);
}

static void reset_perf_frame(PGRAPHVkPerfTelemetry *perf)
{
    memset(perf->finish, 0, sizeof(perf->finish));
    memset(perf->single_time, 0, sizeof(perf->single_time));
    memset(perf->cpu_regions, 0, sizeof(perf->cpu_regions));
    perf->submit_info_count = 0;
    perf->command_buffer_count = 0;
    perf->staged_bytes = 0;
    perf->vertex_staged_bytes = 0;
    perf->vertex_staging_copy_count = 0;
    perf->vertex_staging_capacity_growth_count = 0;
    perf->vertex_staging_fallback_finish_count = 0;
    perf->native_bc_upload_count = 0;
    perf->native_bc_source_bytes = 0;
    perf->native_bc_staged_bytes = 0;
    perf->native_bc_prepare_cpu_us = 0;
    perf->decoded_bc_upload_count = 0;
    perf->decoded_bc_source_bytes = 0;
    perf->decoded_bc_staged_bytes = 0;
    perf->decoded_bc_prepare_cpu_us = 0;
    perf->peak_in_flight_submission_count = perf->in_flight_submission_count;
    perf->oldest_in_flight_serial = 0;
    perf->retirement_queue_objects = 0;
    perf->retirement_queue_bytes = 0;
    perf->need_buffer_space_descriptor_only_count = 0;
    perf->need_buffer_space_ubo_only_count = 0;
    perf->need_buffer_space_both_count = 0;
}

void pgraph_vk_perf_frame(PGRAPHVkState *r)
{
    PGRAPHVkPerfTelemetry *perf = &r->perf;
    PGRAPHVkPerfFrame frame;

    if (!perf->enabled) {
        return;
    }

    capture_perf_frame(r, &frame);
    if (perf->deferred) {
        if (perf->deferred_frame_count < VK_PERF_DEFERRED_FRAME_CAPACITY &&
            perf->deferred_frames != NULL) {
            perf->deferred_frames[perf->deferred_frame_count++] = frame;
        } else {
            perf->deferred_frame_dropped++;
        }
    } else {
        write_perf_frame(perf->file, &frame);
        if (frame.timestamp_us - perf->last_flush_us >= G_USEC_PER_SEC) {
            fflush(perf->file);
            perf->last_flush_us = frame.timestamp_us;
        }
    }
    reset_perf_frame(perf);
}

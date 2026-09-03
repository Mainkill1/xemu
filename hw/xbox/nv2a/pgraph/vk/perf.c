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
    [VK_FINISH_REASON_NEED_BUFFER_SPACE_PIPELINE_CACHE] =
        "need_buffer_space_pipeline_cache",
    [VK_FINISH_REASON_NEED_BUFFER_SPACE_FRAMEBUFFER_SLOTS] =
        "need_buffer_space_framebuffer_slots",
    [VK_FINISH_REASON_NEED_BUFFER_SPACE_STORAGE_CAPACITY] =
        "need_buffer_space_storage_capacity",
    [VK_FINISH_REASON_NEED_BUFFER_SPACE_STORAGE_RECREATE] =
        "need_buffer_space_storage_recreate",
    [VK_FINISH_REASON_NEED_BUFFER_SPACE_UNIFORM_OR_DESCRIPTOR] =
        "need_buffer_space_uniform_or_descriptor",
    [VK_FINISH_REASON_NEED_BUFFER_SPACE_BUFFER_RESIZE] =
        "need_buffer_space_buffer_resize",
    [VK_FINISH_REASON_NEED_BUFFER_SPACE_SURFACE_COMPUTE] =
        "need_buffer_space_surface_compute",
    [VK_FINISH_REASON_NEED_BUFFER_SPACE_TEXTURE_STAGING] =
        "need_buffer_space_texture_staging",
    [VK_FINISH_REASON_NEED_BUFFER_SPACE_TEXTURE_COMPUTE] =
        "need_buffer_space_texture_compute",
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

void pgraph_vk_perf_init(PGRAPHVkState *r)
{
    const char *reuse_index_payloads =
        g_getenv("XEMU_VK_REUSE_IDENTICAL_INDEX_PAYLOADS");
    r->perf.reuse_identical_index_payloads =
        reuse_index_payloads != NULL && reuse_index_payloads[0] != '\0' &&
        strcmp(reuse_index_payloads, "0") != 0;

    const char *skip_push_constants =
        g_getenv("XEMU_VK_SKIP_IDENTICAL_PUSH_CONSTANTS");
    r->perf.skip_identical_push_constants =
        skip_push_constants != NULL && skip_push_constants[0] != '\0' &&
        strcmp(skip_push_constants, "0") != 0;

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
    fprintf(r->perf.file,
            "{\"type\":\"schema\",\"schema_version\":9"
            ",\"duration_sampling\":{\"initial_per_reason_per_frame\":%u"
            ",\"hot_stride\":%u}"
            ",\"tiny_draw_attribution_version\":1",
            VK_PERF_INITIAL_TIMED_SUBMITS, VK_PERF_HOT_SAMPLE_STRIDE);
    write_names(r->perf.file, "finish_reasons", finish_reason_names,
                ARRAY_SIZE(finish_reason_names));
    write_names(r->perf.file, "single_time_callers", single_time_reason_names,
                ARRAY_SIZE(single_time_reason_names));
    write_names(r->perf.file, "cpu_regions", cpu_region_names,
                ARRAY_SIZE(cpu_region_names));
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

bool pgraph_vk_perf_should_time_finish(PGRAPHVkState *r, FinishReason reason)
{
    if (!r->perf.enabled) {
        return false;
    }
    assert(reason < VK_FINISH_REASON_COUNT);
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

void pgraph_vk_perf_record_vertex_dirty_check(PGRAPHVkState *r, hwaddr addr,
                                               hwaddr size, bool dirty)
{
    PGRAPHVkPerfTelemetry *perf = &r->perf;
    if (!perf->enabled) {
        return;
    }

    bool repeated = false;
    for (uint8_t i = 0; i < perf->vertex_dirty_recent_range_count; i++) {
        MemorySyncRequirement *recent = &perf->vertex_dirty_recent_ranges[i];
        if (recent->addr == addr && recent->size == size) {
            repeated = true;
            break;
        }
    }

    perf->vertex_dirty_check_count++;
    perf->vertex_dirty_pages_checked += size / TARGET_PAGE_SIZE;
    if (dirty) {
        perf->vertex_dirty_hit_count++;
        perf->vertex_dirty_hit_pages += size / TARGET_PAGE_SIZE;
    }
    if (repeated) {
        perf->vertex_dirty_repeated_range_count++;
        perf->vertex_dirty_repeated_range_hit_count += dirty;
    }

    perf->vertex_dirty_recent_ranges[perf->vertex_dirty_recent_range_next] =
        (MemorySyncRequirement){ .addr = addr, .size = size };
    perf->vertex_dirty_recent_range_next =
        (perf->vertex_dirty_recent_range_next + 1) %
        ARRAY_SIZE(perf->vertex_dirty_recent_ranges);
    perf->vertex_dirty_recent_range_count = MIN(
        perf->vertex_dirty_recent_range_count + 1,
        ARRAY_SIZE(perf->vertex_dirty_recent_ranges));
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

bool pgraph_vk_perf_try_reuse_index_payload(PGRAPHVkState *r,
                                            const void *data,
                                            VkDeviceSize size,
                                            VkDeviceSize *staging_offset)
{
    PGRAPHVkPerfTelemetry *perf = &r->perf;
    if (!(perf->enabled || perf->reuse_identical_index_payloads)) {
        return false;
    }

    StorageBuffer *staging = &r->storage_buffers[BUFFER_INDEX_STAGING];
    bool duplicate = perf->last_index_payload_valid &&
                     perf->last_index_payload_size == size &&
                     memcmp(staging->mapped + perf->last_index_payload_offset,
                            data, size) == 0;

    if (perf->enabled) {
        perf->index_payload_count++;
        perf->index_payload_bytes += size;
        if (duplicate) {
            perf->consecutive_duplicate_index_payload_count++;
            perf->consecutive_duplicate_index_payload_bytes += size;
            if (perf->reuse_identical_index_payloads) {
                perf->reused_index_payload_count++;
                perf->reused_index_payload_bytes += size;
            }
        }
    }

    if (perf->reuse_identical_index_payloads && duplicate) {
        *staging_offset = perf->last_index_payload_offset;
        return true;
    }
    return false;
}

void pgraph_vk_perf_commit_index_payload(PGRAPHVkState *r,
                                         VkDeviceSize size,
                                         VkDeviceSize staging_offset)
{
    PGRAPHVkPerfTelemetry *perf = &r->perf;
    if (!(perf->enabled || perf->reuse_identical_index_payloads)) {
        return;
    }
    perf->last_index_payload_valid = true;
    perf->last_index_payload_offset = staging_offset;
    perf->last_index_payload_size = size;
}

bool pgraph_vk_perf_should_emit_push_constants(PGRAPHVkState *r,
                                               VkPipelineLayout layout,
                                               const float *values,
                                               size_t size)
{
    PGRAPHVkPerfTelemetry *perf = &r->perf;
    if (!(perf->enabled || perf->skip_identical_push_constants)) {
        return true;
    }

    bool identical = perf->last_push_constant_valid &&
                     perf->last_push_constant_layout == layout &&
                     perf->last_push_constant_size == size &&
                     memcmp(perf->last_push_constant_values, values, size) == 0;
    if (perf->enabled) {
        perf->push_constant_count++;
        perf->push_constant_bytes += size;
        if (identical) {
            perf->identical_push_constant_count++;
            perf->identical_push_constant_bytes += size;
            if (perf->skip_identical_push_constants) {
                perf->skipped_push_constant_count++;
            }
        }
    }

    assert(size <= sizeof(perf->last_push_constant_values));
    memcpy(perf->last_push_constant_values, values, size);
    perf->last_push_constant_valid = true;
    perf->last_push_constant_layout = layout;
    perf->last_push_constant_size = size;
    return !(perf->skip_identical_push_constants && identical);
}

void pgraph_vk_perf_begin_command_buffer(PGRAPHVkState *r)
{
    if (r->perf.enabled || r->perf.reuse_identical_index_payloads) {
        r->perf.last_index_payload_valid = false;
    }
    if (r->perf.enabled || r->perf.skip_identical_push_constants) {
        r->perf.last_push_constant_valid = false;
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
            "{\"type\":\"frame\",\"schema_version\":9"
            ",\"timestamp_us\":%" PRId64 ",\"guest_frame\":%" PRIu64,
            now, ++perf->frame);
    write_stat_array(perf->file, "finish_count_per_guest_frame", perf->finish,
                     ARRAY_SIZE(perf->finish),
                     offsetof(PGRAPHVkWaitStats, call_count));
    write_stat_array(perf->file, "finish_submit_count_per_guest_frame",
                     perf->finish, ARRAY_SIZE(perf->finish),
                     offsetof(PGRAPHVkWaitStats, submit_count));
    write_stat_array(perf->file, "finish_timed_submit_count_per_guest_frame",
                     perf->finish, ARRAY_SIZE(perf->finish),
                     offsetof(PGRAPHVkWaitStats, timed_submit_count));
    write_stat_array(perf->file,
                     "finish_sampled_submit_cpu_us_per_guest_frame",
                     perf->finish, ARRAY_SIZE(perf->finish),
                     offsetof(PGRAPHVkWaitStats, submit_cpu_us));
    write_stat_array(perf->file, "fence_wait_count_per_guest_frame",
                     perf->finish, ARRAY_SIZE(perf->finish),
                     offsetof(PGRAPHVkWaitStats, wait_count));
    write_stat_array(perf->file, "finish_sampled_wait_us_per_guest_frame",
                     perf->finish,
                     ARRAY_SIZE(perf->finish),
                     offsetof(PGRAPHVkWaitStats, wait_us));
    write_stat_array(perf->file, "single_time_submit_count_per_guest_frame",
                     perf->single_time, ARRAY_SIZE(perf->single_time),
                     offsetof(PGRAPHVkWaitStats, submit_count));
    write_stat_array(perf->file,
                     "single_time_timed_submit_count_per_guest_frame",
                     perf->single_time, ARRAY_SIZE(perf->single_time),
                     offsetof(PGRAPHVkWaitStats, timed_submit_count));
    write_stat_array(perf->file,
                     "single_time_sampled_submit_cpu_us_per_guest_frame",
                     perf->single_time, ARRAY_SIZE(perf->single_time),
                     offsetof(PGRAPHVkWaitStats, submit_cpu_us));
    write_stat_array(perf->file, "queue_wait_idle_count_per_guest_frame",
                     perf->single_time, ARRAY_SIZE(perf->single_time),
                     offsetof(PGRAPHVkWaitStats, wait_count));
    write_stat_array(perf->file,
                     "single_time_sampled_wait_us_per_guest_frame",
                     perf->single_time, ARRAY_SIZE(perf->single_time),
                     offsetof(PGRAPHVkWaitStats, wait_us));
    write_cpu_stat_array(perf->file, "cpu_region_calls_per_guest_frame",
                         perf->cpu_regions,
                         offsetof(PGRAPHVkCpuStats, call_count));
    write_cpu_stat_array(perf->file, "cpu_region_us_per_guest_frame",
                         perf->cpu_regions,
                         offsetof(PGRAPHVkCpuStats, cpu_us));
    fprintf(perf->file,
            ",\"vk_queue_submit_calls_per_guest_frame\":%" PRIu64
            ",\"vk_submit_infos_per_guest_frame\":%" PRIu64
            ",\"command_buffers_per_guest_frame\":%" PRIu64
            ",\"staged_bytes_per_guest_frame\":%" PRIu64
            ",\"vertex_staged_bytes_per_guest_frame\":%" PRIu64
            ",\"vertex_staging_copies_per_guest_frame\":%" PRIu64
            ",\"vertex_staging_capacity_bytes\":%zu"
            ",\"vertex_staging_capacity_growths_per_guest_frame\":%" PRIu64
            ",\"vertex_staging_fallback_finishes_per_guest_frame\":%" PRIu64
            ",\"vertex_dirty_checks_per_guest_frame\":%" PRIu64
            ",\"vertex_dirty_pages_checked_per_guest_frame\":%" PRIu64
            ",\"vertex_dirty_hits_per_guest_frame\":%" PRIu64
            ",\"vertex_dirty_hit_pages_per_guest_frame\":%" PRIu64
            ",\"vertex_dirty_repeated_ranges_per_guest_frame\":%" PRIu64
            ",\"vertex_dirty_repeated_range_hits_per_guest_frame\":%" PRIu64
            ",\"native_bc_uploads_per_guest_frame\":%" PRIu64
            ",\"native_bc_source_bytes_per_guest_frame\":%" PRIu64
            ",\"native_bc_staged_bytes_per_guest_frame\":%" PRIu64
            ",\"native_bc_prepare_cpu_us_per_guest_frame\":%" PRIu64
            ",\"decoded_bc_uploads_per_guest_frame\":%" PRIu64
            ",\"decoded_bc_source_bytes_per_guest_frame\":%" PRIu64
            ",\"decoded_bc_staged_bytes_per_guest_frame\":%" PRIu64
            ",\"decoded_bc_prepare_cpu_us_per_guest_frame\":%" PRIu64
            ",\"descriptor_set_capacity\":%u"
            ",\"descriptor_set_highwater_per_guest_frame\":%" PRIu64
            ",\"index_payloads_per_guest_frame\":%" PRIu64
            ",\"index_payload_bytes_per_guest_frame\":%" PRIu64
            ",\"consecutive_duplicate_index_payloads_per_guest_frame\":%" PRIu64
            ",\"consecutive_duplicate_index_payload_bytes_per_guest_frame\":%" PRIu64
            ",\"reused_index_payloads_per_guest_frame\":%" PRIu64
            ",\"reused_index_payload_bytes_per_guest_frame\":%" PRIu64
            ",\"push_constant_emits_per_guest_frame\":%" PRIu64
            ",\"push_constant_bytes_per_guest_frame\":%" PRIu64
            ",\"identical_push_constant_emits_per_guest_frame\":%" PRIu64
            ",\"identical_push_constant_bytes_per_guest_frame\":%" PRIu64
            ",\"skipped_push_constant_emits_per_guest_frame\":%" PRIu64
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
            perf->staged_bytes, perf->vertex_staged_bytes,
            perf->vertex_staging_copy_count,
            r->storage_buffers[BUFFER_VERTEX_RAM_STAGING].buffer_size,
            perf->vertex_staging_capacity_growth_count,
            perf->vertex_staging_fallback_finish_count,
            perf->vertex_dirty_check_count,
            perf->vertex_dirty_pages_checked,
            perf->vertex_dirty_hit_count,
            perf->vertex_dirty_hit_pages,
            perf->vertex_dirty_repeated_range_count,
            perf->vertex_dirty_repeated_range_hit_count,
            perf->native_bc_upload_count, perf->native_bc_source_bytes,
            perf->native_bc_staged_bytes, perf->native_bc_prepare_cpu_us,
            perf->decoded_bc_upload_count, perf->decoded_bc_source_bytes,
            perf->decoded_bc_staged_bytes, perf->decoded_bc_prepare_cpu_us,
            r->descriptor_set_capacity, perf->descriptor_set_highwater,
            perf->index_payload_count, perf->index_payload_bytes,
            perf->consecutive_duplicate_index_payload_count,
            perf->consecutive_duplicate_index_payload_bytes,
            perf->reused_index_payload_count,
            perf->reused_index_payload_bytes,
            perf->push_constant_count, perf->push_constant_bytes,
            perf->identical_push_constant_count,
            perf->identical_push_constant_bytes,
            perf->skipped_push_constant_count,
            staged_bytes_per_submit,
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
    memset(perf->cpu_regions, 0, sizeof(perf->cpu_regions));
    perf->submit_info_count = 0;
    perf->command_buffer_count = 0;
    perf->staged_bytes = 0;
    perf->vertex_staged_bytes = 0;
    perf->vertex_staging_copy_count = 0;
    perf->vertex_staging_capacity_growth_count = 0;
    perf->vertex_staging_fallback_finish_count = 0;
    perf->vertex_dirty_check_count = 0;
    perf->vertex_dirty_pages_checked = 0;
    perf->vertex_dirty_hit_count = 0;
    perf->vertex_dirty_hit_pages = 0;
    perf->vertex_dirty_repeated_range_count = 0;
    perf->vertex_dirty_repeated_range_hit_count = 0;
    perf->vertex_dirty_recent_range_count = 0;
    perf->vertex_dirty_recent_range_next = 0;
    perf->native_bc_upload_count = 0;
    perf->native_bc_source_bytes = 0;
    perf->native_bc_staged_bytes = 0;
    perf->native_bc_prepare_cpu_us = 0;
    perf->decoded_bc_upload_count = 0;
    perf->decoded_bc_source_bytes = 0;
    perf->decoded_bc_staged_bytes = 0;
    perf->decoded_bc_prepare_cpu_us = 0;
    perf->descriptor_set_highwater = 0;
    perf->index_payload_count = 0;
    perf->index_payload_bytes = 0;
    perf->consecutive_duplicate_index_payload_count = 0;
    perf->consecutive_duplicate_index_payload_bytes = 0;
    perf->reused_index_payload_count = 0;
    perf->reused_index_payload_bytes = 0;
    perf->push_constant_count = 0;
    perf->push_constant_bytes = 0;
    perf->identical_push_constant_count = 0;
    perf->identical_push_constant_bytes = 0;
    perf->skipped_push_constant_count = 0;
    perf->peak_in_flight_submission_count =
        perf->in_flight_submission_count;
    perf->oldest_in_flight_serial = 0;
    perf->retirement_queue_objects = 0;
    perf->retirement_queue_bytes = 0;
}

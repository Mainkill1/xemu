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

static const char *pipeline_stat_names[VK_PERF_PIPELINE_STAT_COUNT] = {
    "input_assembly_vertices",
    "input_assembly_primitives",
    "vertex_shader_invocations",
    "clipping_invocations",
    "clipping_primitives",
    "fragment_shader_invocations",
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

static void init_gpu_timestamps(PGRAPHVkState *r)
{
    QueueFamilyIndices indices =
        pgraph_vk_find_queue_families(r->physical_device);
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(r->physical_device, &count, NULL);
    g_autofree VkQueueFamilyProperties *props =
        g_new(VkQueueFamilyProperties, count);
    vkGetPhysicalDeviceQueueFamilyProperties(r->physical_device, &count, props);

    assert(indices.queue_family >= 0);
    assert((uint32_t)indices.queue_family < count);
    r->perf.timestamp_valid_bits =
        props[indices.queue_family].timestampValidBits;
    r->perf.timestamp_period_ns = r->device_props.limits.timestampPeriod;
    if (r->perf.timestamp_valid_bits == 0) {
        return;
    }

    VkQueryPoolCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_TIMESTAMP,
        .queryCount = 4,
    };
    VK_CHECK(vkCreateQueryPool(r->device, &create_info, NULL,
                               &r->perf.timestamp_query_pool));
}

static void init_pipeline_stats(PGRAPHVkState *r)
{
    if (!r->enabled_physical_device_features.pipelineStatisticsQuery) {
        return;
    }

    const char *shader_stats = g_getenv("XEMU_VK_PERF_SHADER_STATS");
    bool per_shader = shader_stats != NULL && shader_stats[0] != '\0';
    VkQueryPoolCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS,
        .queryCount = per_shader ? VK_PERF_MAX_SHADER_QUERIES : 1,
        .pipelineStatistics =
            VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT |
            VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT |
            VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT |
            VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT |
            VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT |
            VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT,
    };
    VkQueryPool *query_pool = per_shader ? &r->perf.shader_stats_query_pool :
                                          &r->perf.pipeline_stats_query_pool;
    VK_CHECK(vkCreateQueryPool(r->device, &create_info, NULL, query_pool));
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
    init_gpu_timestamps(r);
    init_pipeline_stats(r);
    if (r->perf.shader_stats_query_pool != VK_NULL_HANDLE) {
        r->perf.shader_dump_dir = g_strdup_printf("%s.shaders", path);
        if (g_mkdir_with_parents(r->perf.shader_dump_dir, 0755) != 0) {
            fprintf(stderr, "nv2a: failed to create shader dump directory '%s'\n",
                    r->perf.shader_dump_dir);
            g_clear_pointer(&r->perf.shader_dump_dir, g_free);
        }
    }
    r->perf.last_flush_us = qemu_clock_get_us(QEMU_CLOCK_REALTIME);
    fprintf(r->perf.file,
            "{\"type\":\"schema\",\"schema_version\":7"
            ",\"duration_sampling\":{\"initial_per_reason_per_frame\":%u"
            ",\"hot_stride\":%u}"
            ",\"gpu_batch_timestamps\":{\"supported\":%s"
            ",\"timestamp_valid_bits\":%u"
            ",\"timestamp_period_ns\":%.9g}",
            VK_PERF_INITIAL_TIMED_SUBMITS, VK_PERF_HOT_SAMPLE_STRIDE,
            r->perf.timestamp_query_pool != VK_NULL_HANDLE ? "true" : "false",
            r->perf.timestamp_valid_bits, r->perf.timestamp_period_ns);
    fprintf(r->perf.file,
            ",\"pipeline_statistics\":{\"supported\":%s"
            ",\"mode\":\"%s\",\"max_shader_queries_per_submit\":%u}",
            (r->perf.pipeline_stats_query_pool != VK_NULL_HANDLE ||
             r->perf.shader_stats_query_pool != VK_NULL_HANDLE) ? "true" :
                                                                  "false",
            r->perf.shader_stats_query_pool != VK_NULL_HANDLE ?
                "per_shader_draw" : "per_finish",
            VK_PERF_MAX_SHADER_QUERIES);
    fprintf(r->perf.file,
            ",\"shader_state_classification\":{"
            "\"clip_region0_covers_scissor\":true}");
    write_names(r->perf.file, "finish_reasons", finish_reason_names,
                ARRAY_SIZE(finish_reason_names));
    write_names(r->perf.file, "single_time_callers", single_time_reason_names,
                ARRAY_SIZE(single_time_reason_names));
    write_names(r->perf.file, "pipeline_statistics_names", pipeline_stat_names,
                ARRAY_SIZE(pipeline_stat_names));
    fprintf(r->perf.file, "}\n");
}

void pgraph_vk_perf_finalize(PGRAPHVkState *r)
{
    if (r->perf.shader_stats_query_pool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(r->device, r->perf.shader_stats_query_pool, NULL);
        r->perf.shader_stats_query_pool = VK_NULL_HANDLE;
    }
    if (r->perf.pipeline_stats_query_pool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(r->device, r->perf.pipeline_stats_query_pool, NULL);
        r->perf.pipeline_stats_query_pool = VK_NULL_HANDLE;
    }
    if (r->perf.timestamp_query_pool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(r->device, r->perf.timestamp_query_pool, NULL);
        r->perf.timestamp_query_pool = VK_NULL_HANDLE;
    }
    if (r->perf.file != NULL) {
        fflush(r->perf.file);
        fclose(r->perf.file);
    }
    r->perf.file = NULL;
    g_clear_pointer(&r->perf.shader_dump_dir, g_free);
    r->perf.enabled = false;
}

void pgraph_vk_perf_dump_shader(PGRAPHVkState *r,
                                VkShaderStageFlagBits stage,
                                ShaderModuleInfo *info)
{
    if (r->perf.shader_dump_dir == NULL) {
        return;
    }

    const char *stage_name;
    switch (stage) {
    case VK_SHADER_STAGE_VERTEX_BIT:
        stage_name = "vertex";
        break;
    case VK_SHADER_STAGE_GEOMETRY_BIT:
        stage_name = "geometry";
        break;
    case VK_SHADER_STAGE_FRAGMENT_BIT:
        stage_name = "fragment";
        break;
    case VK_SHADER_STAGE_COMPUTE_BIT:
        stage_name = "compute";
        break;
    default:
        stage_name = "unknown";
        break;
    }

    g_autofree char *glsl_name = g_strdup_printf(
        "%s-%016" PRIx64 ".glsl", stage_name, info->spirv_hash);
    g_autofree char *spv_name = g_strdup_printf(
        "%s-%016" PRIx64 ".spv", stage_name, info->spirv_hash);
    g_autofree char *glsl_path =
        g_build_filename(r->perf.shader_dump_dir, glsl_name, NULL);
    g_autofree char *spv_path =
        g_build_filename(r->perf.shader_dump_dir, spv_name, NULL);

    if (!g_file_test(glsl_path, G_FILE_TEST_EXISTS)) {
        FILE *file = qemu_fopen(glsl_path, "w");
        if (file != NULL) {
            fwrite(info->glsl, 1, strlen(info->glsl), file);
            fclose(file);
        }
    }
    if (!g_file_test(spv_path, G_FILE_TEST_EXISTS)) {
        FILE *file = qemu_fopen(spv_path, "wb");
        if (file != NULL) {
            fwrite(info->spirv->data, 1, info->spirv->len, file);
            fclose(file);
        }
    }
}

void pgraph_vk_perf_begin_shader_query(PGRAPHState *pg,
                                        ShaderModuleInfo *fragment_shader)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    assert(!r->perf.shader_query_active);
    if (r->perf.shader_stats_query_pool == VK_NULL_HANDLE) {
        return;
    }
    if (r->perf.shader_query_count >= VK_PERF_MAX_SHADER_QUERIES) {
        r->perf.shader_query_overflow_count++;
        return;
    }

    uint32_t query = r->perf.shader_query_count++;
    r->perf.shader_query_hashes[query] = fragment_shader->spirv_hash;
    r->perf.shader_query_clip_region0_covers_scissor[query] =
        pgraph_vk_window_clip_is_redundant(pg);
    vkCmdBeginQuery(r->command_buffer, r->perf.shader_stats_query_pool,
                    query, 0);
    r->perf.shader_query_active = true;
}

void pgraph_vk_perf_end_shader_query(PGRAPHVkState *r)
{
    if (!r->perf.shader_query_active) {
        return;
    }
    vkCmdEndQuery(r->command_buffer, r->perf.shader_stats_query_pool,
                  r->perf.shader_query_count - 1);
    r->perf.shader_query_active = false;
}

static PGRAPHVkShaderStats *find_shader_stats(PGRAPHVkPerfTelemetry *perf,
                                               uint64_t spirv_hash)
{
    for (uint32_t i = 0; i < perf->shader_stats_count; i++) {
        if (perf->shader_stats[i].spirv_hash == spirv_hash) {
            return &perf->shader_stats[i];
        }
    }
    if (perf->shader_stats_count >= VK_PERF_MAX_SHADER_STATS) {
        perf->shader_stats_overflow_count++;
        return NULL;
    }
    PGRAPHVkShaderStats *stats =
        &perf->shader_stats[perf->shader_stats_count++];
    stats->spirv_hash = spirv_hash;
    return stats;
}

void pgraph_vk_perf_collect_shader_queries(PGRAPHVkState *r)
{
    PGRAPHVkPerfTelemetry *perf = &r->perf;
    if (perf->shader_stats_query_pool == VK_NULL_HANDLE ||
        perf->shader_query_count == 0) {
        return;
    }

    size_t value_count =
        perf->shader_query_count * VK_PERF_PIPELINE_STAT_COUNT;
    g_autofree uint64_t *values = g_new(uint64_t, value_count);
    VkResult result = vkGetQueryPoolResults(
        r->device, perf->shader_stats_query_pool, 0,
        perf->shader_query_count, value_count * sizeof(*values), values,
        VK_PERF_PIPELINE_STAT_COUNT * sizeof(*values),
        VK_QUERY_RESULT_64_BIT);
    VK_CHECK(result);

    for (uint32_t query = 0; query < perf->shader_query_count; query++) {
        PGRAPHVkShaderStats *stats = find_shader_stats(
            perf, perf->shader_query_hashes[query]);
        if (stats == NULL) {
            continue;
        }
        stats->draw_count++;
        for (size_t i = 0; i < VK_PERF_PIPELINE_STAT_COUNT; i++) {
            stats->pipeline_stats[i] +=
                values[query * VK_PERF_PIPELINE_STAT_COUNT + i];
        }
        if (perf->shader_query_clip_region0_covers_scissor[query]) {
            stats->clip_region0_covers_scissor_draw_count++;
            for (size_t i = 0; i < VK_PERF_PIPELINE_STAT_COUNT; i++) {
                stats->clip_region0_covers_scissor_pipeline_stats[i] +=
                    values[query * VK_PERF_PIPELINE_STAT_COUNT + i];
            }
        }
    }
    perf->shader_query_count = 0;
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
                                         bool gpu_timed,
                                         uint64_t gpu_batch_ns,
                                         uint64_t gpu_aux_ns,
                                         uint64_t gpu_handoff_ns,
                                         uint64_t gpu_main_ns,
                                         bool pipeline_stats_valid,
                                         const uint64_t pipeline_stats[
                                             VK_PERF_PIPELINE_STAT_COUNT],
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
    if (gpu_timed) {
        stats->gpu_timed_submit_count++;
        stats->gpu_batch_ns += gpu_batch_ns;
        stats->gpu_aux_ns += gpu_aux_ns;
        stats->gpu_handoff_ns += gpu_handoff_ns;
        stats->gpu_main_ns += gpu_main_ns;
    }
    if (pipeline_stats_valid) {
        for (size_t i = 0; i < VK_PERF_PIPELINE_STAT_COUNT; i++) {
            stats->pipeline_stats[i] += pipeline_stats[i];
        }
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
            "{\"type\":\"frame\",\"schema_version\":7"
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
    write_stat_array(perf->file,
                     "finish_gpu_timed_submit_count_per_guest_frame",
                     perf->finish, ARRAY_SIZE(perf->finish),
                     offsetof(PGRAPHVkWaitStats, gpu_timed_submit_count));
    write_stat_array(perf->file,
                     "finish_gpu_batch_ns_per_guest_frame",
                     perf->finish, ARRAY_SIZE(perf->finish),
                     offsetof(PGRAPHVkWaitStats, gpu_batch_ns));
    write_stat_array(perf->file,
                     "finish_gpu_aux_ns_per_guest_frame",
                     perf->finish, ARRAY_SIZE(perf->finish),
                     offsetof(PGRAPHVkWaitStats, gpu_aux_ns));
    write_stat_array(perf->file,
                     "finish_gpu_handoff_ns_per_guest_frame",
                     perf->finish, ARRAY_SIZE(perf->finish),
                     offsetof(PGRAPHVkWaitStats, gpu_handoff_ns));
    write_stat_array(perf->file,
                     "finish_gpu_main_ns_per_guest_frame",
                     perf->finish, ARRAY_SIZE(perf->finish),
                     offsetof(PGRAPHVkWaitStats, gpu_main_ns));
    for (size_t i = 0; i < VK_PERF_PIPELINE_STAT_COUNT; i++) {
        char key[96];
        snprintf(key, sizeof(key), "finish_%s_per_guest_frame",
                 pipeline_stat_names[i]);
        write_stat_array(perf->file, key, perf->finish,
                         ARRAY_SIZE(perf->finish),
                         offsetof(PGRAPHVkWaitStats, pipeline_stats) +
                             i * sizeof(uint64_t));
    }
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
            ",\"staged_bytes_per_submit\":%.3f"
            ",\"submit_infos_per_submit\":%.3f"
            ",\"command_buffers_per_submit\":%.3f"
            ",\"in_flight_submission_count\":%" PRIu64
            ",\"peak_in_flight_submission_count\":%" PRIu64
            ",\"oldest_in_flight_serial\":%" PRIu64
            ",\"newest_submitted_serial\":%" PRIu64
            ",\"retirement_queue_objects\":%" PRIu64
            ",\"retirement_queue_bytes\":%" PRIu64
            ",\"shader_query_overflows_per_guest_frame\":%" PRIu64
            ",\"shader_stats_overflows_per_guest_frame\":%" PRIu64,
            submit_count, perf->submit_info_count, perf->command_buffer_count,
            perf->staged_bytes, perf->vertex_staged_bytes,
            perf->vertex_staging_copy_count,
            r->storage_buffers[BUFFER_VERTEX_RAM_STAGING].buffer_size,
            perf->vertex_staging_capacity_growth_count,
            perf->vertex_staging_fallback_finish_count,
            staged_bytes_per_submit,
            submit_infos_per_submit, command_buffers_per_submit,
            perf->in_flight_submission_count,
            perf->peak_in_flight_submission_count,
            perf->oldest_in_flight_serial, perf->newest_submitted_serial,
            perf->retirement_queue_objects, perf->retirement_queue_bytes,
            perf->shader_query_overflow_count,
            perf->shader_stats_overflow_count);
    fprintf(perf->file, ",\"fragment_shader_stats_per_guest_frame\":[");
    for (uint32_t shader = 0; shader < perf->shader_stats_count; shader++) {
        PGRAPHVkShaderStats *stats = &perf->shader_stats[shader];
        fprintf(perf->file,
                "%s{\"spirv_hash\":\"%016" PRIx64 "\""
                ",\"draw_count\":%" PRIu64
                ",\"clip_region0_covers_scissor_draw_count\":%" PRIu64,
                shader ? "," : "", stats->spirv_hash, stats->draw_count,
                stats->clip_region0_covers_scissor_draw_count);
        for (size_t stat = 0; stat < VK_PERF_PIPELINE_STAT_COUNT; stat++) {
            fprintf(perf->file, ",\"%s\":%" PRIu64,
                    pipeline_stat_names[stat], stats->pipeline_stats[stat]);
            fprintf(perf->file,
                    ",\"clip_region0_covers_scissor_%s\":%" PRIu64,
                    pipeline_stat_names[stat],
                    stats->clip_region0_covers_scissor_pipeline_stats[stat]);
        }
        fputc('}', perf->file);
    }
    fprintf(perf->file, "]}\n");

    if (now - perf->last_flush_us >= G_USEC_PER_SEC) {
        fflush(perf->file);
        perf->last_flush_us = now;
    }

    memset(perf->finish, 0, sizeof(perf->finish));
    memset(perf->single_time, 0, sizeof(perf->single_time));
    memset(perf->shader_stats, 0, sizeof(perf->shader_stats));
    perf->shader_stats_count = 0;
    perf->shader_query_overflow_count = 0;
    perf->shader_stats_overflow_count = 0;
    perf->submit_info_count = 0;
    perf->command_buffer_count = 0;
    perf->staged_bytes = 0;
    perf->vertex_staged_bytes = 0;
    perf->vertex_staging_copy_count = 0;
    perf->vertex_staging_capacity_growth_count = 0;
    perf->vertex_staging_fallback_finish_count = 0;
    perf->peak_in_flight_submission_count =
        perf->in_flight_submission_count;
    perf->oldest_in_flight_serial = 0;
    perf->retirement_queue_objects = 0;
    perf->retirement_queue_bytes = 0;
}

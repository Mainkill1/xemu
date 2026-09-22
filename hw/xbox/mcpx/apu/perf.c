/*
 * MCPX APU opt-in performance telemetry
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/xbox/mcpx/apu/perf.h"
#include "hw/xbox/mcpx/apu/apu_regs.h"

#define APU_PERF_SCHEMA_VERSION 1
#define APU_PERF_EMIT_INTERVAL_US G_USEC_PER_SEC
#define APU_PERF_FRAME_BUDGET_US (EP_FRAME_US / 8)

static void write_array(FILE *file, const char *key, const uint64_t *values,
                        int count)
{
    fprintf(file, ",\"%s\":[", key);
    for (int i = 0; i < count; i++) {
        fprintf(file, "%s%" PRIu64, i ? "," : "", values[i]);
    }
    fputc(']', file);
}

static void emit_sample_locked(McpxApuPerfTelemetry *perf, int64_t now_us)
{
    McpxApuPerfTotals *t = &perf->totals;

    fprintf(perf->file,
            "{\"type\":\"sample\",\"schema_version\":%d"
            ",\"timestamp_us\":%" PRId64
            ",\"frames\":%" PRIu64
            ",\"frame_work_us\":%" PRIu64
            ",\"frame_work_max_us\":%" PRIu64
            ",\"frame_budget_overruns\":%" PRIu64
            ",\"dispatches\":%" PRIu64
            ",\"queued_voices\":%" PRIu64
            ",\"queued_voices_max\":%" PRIu64
            ",\"scheduled_workers\":%" PRIu64
            ",\"scheduled_workers_max\":%" PRIu64
            ",\"voice_lock_wait_us\":%" PRIu64
            ",\"schedule_us\":%" PRIu64
            ",\"completion_wait_us\":%" PRIu64
            ",\"dispatch_us\":%" PRIu64
            ",\"dispatch_max_us\":%" PRIu64
            ",\"worker_wakeups\":%" PRIu64
            ",\"useful_worker_wakeups\":%" PRIu64
            ",\"empty_worker_wakeups\":%" PRIu64
            ",\"audio_queue_samples\":%" PRIu64
            ",\"audio_queue_bytes\":%" PRIu64
            ",\"audio_queue_min_bytes\":%" PRIu64
            ",\"audio_queue_max_bytes\":%" PRIu64
            ",\"audio_low_watermark_samples\":%" PRIu64
            ",\"audio_high_watermark_samples\":%" PRIu64,
            APU_PERF_SCHEMA_VERSION, now_us, t->frames, t->frame_work_us,
            t->frame_work_max_us, t->frame_budget_overruns, t->dispatches,
            t->queued_voices, t->queued_voices_max, t->scheduled_workers,
            t->scheduled_workers_max, t->voice_lock_wait_us, t->schedule_us,
            t->completion_wait_us, t->dispatch_us, t->dispatch_max_us,
            t->worker_wakeups, t->useful_worker_wakeups,
            t->empty_worker_wakeups, t->audio_queue_samples,
            t->audio_queue_bytes, t->audio_queue_min_bytes,
            t->audio_queue_max_bytes, t->audio_low_watermark_samples,
            t->audio_high_watermark_samples);
    write_array(perf->file, "assigned_voices", t->assigned_voices,
                perf->num_workers);
    write_array(perf->file, "worker_processing_us", t->worker_processing_us,
                perf->num_workers);
    write_array(perf->file, "worker_reduction_us", t->worker_reduction_us,
                perf->num_workers);
    write_array(perf->file, "worker_total_us", t->worker_total_us,
                perf->num_workers);
    write_array(perf->file, "worker_max_us", t->worker_max_us,
                perf->num_workers);
    fprintf(perf->file, "}\n");
    perf->last_emit_us = now_us;
}

bool mcpx_apu_perf_init(McpxApuPerfTelemetry *perf, const char *path,
                        int num_workers, int64_t now_us)
{
    memset(perf, 0, sizeof(*perf));
    perf->num_workers = MIN(MAX(num_workers, 1), MAX_VOICE_WORKERS);
    perf->last_emit_us = now_us;

    if (path == NULL || path[0] == '\0') {
        return false;
    }

    perf->file = qemu_fopen(path, "w");
    if (perf->file == NULL) {
        fprintf(stderr, "mcpx-apu: failed to open performance log '%s'\n",
                path);
        return false;
    }

    perf->enabled = true;
    fprintf(perf->file,
            "{\"type\":\"schema\",\"schema_version\":%d"
            ",\"num_workers\":%d,\"frame_budget_us\":%d"
            ",\"counters\":\"cumulative_totals\"}\n",
            APU_PERF_SCHEMA_VERSION, perf->num_workers,
            APU_PERF_FRAME_BUDGET_US);
    return true;
}

void mcpx_apu_perf_finalize(McpxApuPerfTelemetry *perf, int64_t now_us)
{
    if (perf->enabled) {
        emit_sample_locked(perf, now_us);
        fflush(perf->file);
        fclose(perf->file);
        perf->file = NULL;
        perf->enabled = false;
    }
}

void mcpx_apu_perf_record_worker(McpxApuPerfTelemetry *perf, int worker_id,
                                 int assigned_voices,
                                 uint64_t processing_us,
                                 uint64_t reduction_us,
                                 uint64_t total_us)
{
    if (!perf->enabled) {
        return;
    }
    assert(worker_id >= 0 && worker_id < perf->num_workers);
    assert(assigned_voices >= 0);

    McpxApuPerfTotals *t = &perf->totals;
    t->worker_wakeups++;
    if (assigned_voices) {
        t->useful_worker_wakeups++;
    } else {
        t->empty_worker_wakeups++;
    }
    t->assigned_voices[worker_id] += assigned_voices;
    t->worker_processing_us[worker_id] += processing_us;
    t->worker_reduction_us[worker_id] += reduction_us;
    t->worker_total_us[worker_id] += total_us;
    t->worker_max_us[worker_id] = MAX(t->worker_max_us[worker_id], total_us);
}

void mcpx_apu_perf_record_dispatch(McpxApuPerfTelemetry *perf,
                                   int queued_voices, int scheduled_workers,
                                   uint64_t voice_lock_wait_us,
                                   uint64_t schedule_us,
                                   uint64_t completion_wait_us,
                                   uint64_t dispatch_us)
{
    if (!perf->enabled) {
        return;
    }
    assert(queued_voices >= 0);
    assert(scheduled_workers >= 0 &&
           scheduled_workers <= perf->num_workers);

    McpxApuPerfTotals *t = &perf->totals;
    t->dispatches++;
    t->queued_voices += queued_voices;
    t->queued_voices_max = MAX(t->queued_voices_max, queued_voices);
    t->scheduled_workers += scheduled_workers;
    t->scheduled_workers_max = MAX(t->scheduled_workers_max,
                                   scheduled_workers);
    t->voice_lock_wait_us += voice_lock_wait_us;
    t->schedule_us += schedule_us;
    t->completion_wait_us += completion_wait_us;
    t->dispatch_us += dispatch_us;
    t->dispatch_max_us = MAX(t->dispatch_max_us, dispatch_us);
}

void mcpx_apu_perf_record_audio_queue(McpxApuPerfTelemetry *perf,
                                      int queued_bytes, int low_watermark,
                                      int high_watermark)
{
    if (!perf->enabled || queued_bytes < 0) {
        return;
    }

    McpxApuPerfTotals *t = &perf->totals;
    if (t->audio_queue_samples == 0) {
        t->audio_queue_min_bytes = queued_bytes;
        t->audio_queue_max_bytes = queued_bytes;
    } else {
        t->audio_queue_min_bytes = MIN(t->audio_queue_min_bytes,
                                       queued_bytes);
        t->audio_queue_max_bytes = MAX(t->audio_queue_max_bytes,
                                       queued_bytes);
    }
    t->audio_queue_samples++;
    t->audio_queue_bytes += queued_bytes;
    t->audio_low_watermark_samples += queued_bytes <= low_watermark;
    t->audio_high_watermark_samples += queued_bytes >= high_watermark;
}

void mcpx_apu_perf_record_frame(McpxApuPerfTelemetry *perf,
                                uint64_t frame_work_us, int64_t now_us)
{
    if (!perf->enabled) {
        return;
    }

    McpxApuPerfTotals *t = &perf->totals;
    t->frames++;
    t->frame_work_us += frame_work_us;
    t->frame_work_max_us = MAX(t->frame_work_max_us, frame_work_us);
    t->frame_budget_overruns += frame_work_us > APU_PERF_FRAME_BUDGET_US;
    if (now_us - perf->last_emit_us >= APU_PERF_EMIT_INTERVAL_US) {
        emit_sample_locked(perf, now_us);
        fflush(perf->file);
    }
}

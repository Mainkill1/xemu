/*
 * MCPX APU opt-in performance telemetry
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_XBOX_MCPX_APU_PERF_H
#define HW_XBOX_MCPX_APU_PERF_H

#include "hw/xbox/mcpx/apu/apu_debug.h"

typedef struct McpxApuPerfTotals {
    uint64_t frames;
    uint64_t frame_work_us;
    uint64_t frame_work_max_us;
    uint64_t frame_budget_overruns;

    uint64_t dispatches;
    uint64_t queued_voices;
    uint64_t queued_voices_max;
    uint64_t resampled_mono_voices;
    uint64_t resampled_stereo_voices;
    uint64_t multipass_voices;
    uint64_t scheduled_workers;
    uint64_t scheduled_workers_max;
    uint64_t voice_lock_wait_us;
    uint64_t schedule_us;
    uint64_t completion_wait_us;
    uint64_t dispatch_us;
    uint64_t dispatch_max_us;

    uint64_t worker_wakeups;
    uint64_t useful_worker_wakeups;
    uint64_t empty_worker_wakeups;
    uint64_t assigned_voices[MAX_VOICE_WORKERS];
    uint64_t worker_processing_us[MAX_VOICE_WORKERS];
    uint64_t worker_reduction_us[MAX_VOICE_WORKERS];
    uint64_t worker_total_us[MAX_VOICE_WORKERS];
    uint64_t worker_max_us[MAX_VOICE_WORKERS];

    uint64_t audio_queue_samples;
    uint64_t audio_queue_bytes;
    uint64_t audio_queue_min_bytes;
    uint64_t audio_queue_max_bytes;
    uint64_t audio_low_watermark_samples;
    uint64_t audio_high_watermark_samples;
} McpxApuPerfTotals;

typedef struct McpxApuPerfTelemetry {
    bool enabled;
    FILE *file;
    int num_workers;
    int64_t last_emit_us;
    McpxApuPerfTotals totals;
} McpxApuPerfTelemetry;

/* Record calls are serialized by VoiceWorkDispatch::lock in production. */
bool mcpx_apu_perf_init(McpxApuPerfTelemetry *perf, const char *path,
                        int num_workers, int64_t now_us);
void mcpx_apu_perf_finalize(McpxApuPerfTelemetry *perf, int64_t now_us);
void mcpx_apu_perf_record_worker(McpxApuPerfTelemetry *perf, int worker_id,
                                 int assigned_voices,
                                 uint64_t processing_us,
                                 uint64_t reduction_us,
                                 uint64_t total_us);
void mcpx_apu_perf_record_dispatch(McpxApuPerfTelemetry *perf,
                                   int queued_voices, int scheduled_workers,
                                   int resampled_mono_voices,
                                   int resampled_stereo_voices,
                                   int multipass_voices,
                                   uint64_t voice_lock_wait_us,
                                   uint64_t schedule_us,
                                   uint64_t completion_wait_us,
                                   uint64_t dispatch_us);
void mcpx_apu_perf_record_audio_queue(McpxApuPerfTelemetry *perf,
                                      int queued_bytes, int low_watermark,
                                      int high_watermark);
void mcpx_apu_perf_record_frame(McpxApuPerfTelemetry *perf,
                                uint64_t frame_work_us, int64_t now_us);

#endif

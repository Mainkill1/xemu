/*
 * Opt-in, slow-frame-only Vulkan hybrid attribution.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"

#include "hw/xbox/nv2a/pgraph/vk/hybrid-trace.h"

#define HYBRID_TRACE_CAPACITY 16384
#define HYBRID_TRACE_SLOW_FRAME_THRESHOLD_US 50000

typedef struct PGRAPHVkHybridTraceRecord {
    uint64_t timestamp_us;
    uint64_t frame;
    uint64_t draw;
    uint64_t pipeline_hash;
    uint64_t shader_hash;
    uint64_t ticket;
    uint64_t data[4];
    uint32_t type;
    uint32_t route;
} PGRAPHVkHybridTraceRecord;

struct PGRAPHVkHybridTrace {
    FILE *file;
    PGRAPHVkHybridTraceRecord *records;
    uint64_t frame;
    uint64_t draw;
    int64_t frame_start_us;
    uint64_t threshold_us;
    size_t head;
    size_t count;
    uint64_t dropped;
};

uint64_t pgraph_vk_hybrid_trace_threshold_us(const char *trace_all_frames)
{
    return trace_all_frames && strcmp(trace_all_frames, "1") == 0 ?
           0 : HYBRID_TRACE_SLOW_FRAME_THRESHOLD_US;
}

PGRAPHVkHybridTrace *pgraph_vk_hybrid_trace_open(const char *path,
                                                 uint64_t threshold_us)
{
    if (!path || !path[0]) {
        return NULL;
    }
    FILE *file = qemu_fopen(path, "w");
    if (!file) {
        return NULL;
    }
    PGRAPHVkHybridTrace *trace = g_new0(PGRAPHVkHybridTrace, 1);
    trace->records = g_new(PGRAPHVkHybridTraceRecord,
                            HYBRID_TRACE_CAPACITY);
    trace->file = file;
    trace->frame_start_us = g_get_monotonic_time();
    trace->threshold_us = threshold_us;
    fprintf(file, "# hybrid-trace-v1; times are monotonic us; "
            "type,frame,draw,time_us,route,pipeline_hash,shader_hash,"
            "ticket,a,b,c,d\n");
    fprintf(file, "# event-types: source-generation=%u,source-compile=%u,"
            "recipe-capture=%u,job-promotion=%u,pipeline-promotion=%u,"
            "completion=%u\n",
            VK_HYBRID_TRACE_SOURCE_GENERATION,
            VK_HYBRID_TRACE_SOURCE_COMPILE,
            VK_HYBRID_TRACE_RECIPE_CAPTURE,
            VK_HYBRID_TRACE_JOB_PROMOTION,
            VK_HYBRID_TRACE_PIPELINE_PROMOTION,
            VK_HYBRID_TRACE_COMPLETION);
    fprintf(file, "# source-generation/source-compile job timing: "
            "a=submitted_us,b=started_us,c=finished_us,d=success\n");
    fprintf(file, "# recipe capture: "
            "a=started_us,b=finished_us,c=submit_result,d=job_kind\n");
    fprintf(file, "# promotion: "
            "a=old_urgency,b=new_urgency,c=epoch,d=job_kind\n");
    fprintf(file, "# pipeline promotion: "
            "a=old_urgency,b=new_urgency,c=generation,d=0\n");
    fprintf(file, "# completion: "
            "a=renderer_started_us,b=renderer_finished_us,"
            "c=published,d=matching_work\n");
    return trace;
}

void pgraph_vk_hybrid_trace_close(PGRAPHVkHybridTrace *trace)
{
    if (!trace) {
        return;
    }
    fclose(trace->file);
    g_free(trace->records);
    g_free(trace);
}

void pgraph_vk_hybrid_trace_draw(PGRAPHVkHybridTrace *trace)
{
    if (trace) {
        trace->draw++;
    }
}

void pgraph_vk_hybrid_trace_record(PGRAPHVkHybridTrace *trace,
                                   PGRAPHVkHybridTraceType type,
                                   uint32_t route, uint64_t pipeline_hash,
                                   uint64_t shader_hash, uint64_t ticket,
                                   uint64_t a, uint64_t b, uint64_t c,
                                   uint64_t d)
{
    if (!trace) {
        return;
    }
    size_t slot = (trace->head + trace->count) % HYBRID_TRACE_CAPACITY;
    if (trace->count == HYBRID_TRACE_CAPACITY) {
        trace->head = (trace->head + 1) % HYBRID_TRACE_CAPACITY;
        trace->dropped++;
    } else {
        trace->count++;
    }
    trace->records[slot] = (PGRAPHVkHybridTraceRecord) {
        .timestamp_us = g_get_monotonic_time(),
        .frame = trace->frame,
        .draw = trace->draw,
        .pipeline_hash = pipeline_hash,
        .shader_hash = shader_hash,
        .ticket = ticket,
        .data = { a, b, c, d },
        .type = type,
        .route = route,
    };
}

void pgraph_vk_hybrid_trace_frame(PGRAPHVkHybridTrace *trace)
{
    if (!trace) {
        return;
    }
    int64_t now_us = g_get_monotonic_time();
    uint64_t duration_us = MAX(now_us - trace->frame_start_us, 0);
    if (duration_us >= trace->threshold_us) {
        fprintf(trace->file, "frame,%" PRIu64 ",%" PRIu64 ",%zu,%" PRIu64
                "\n", trace->frame, duration_us, trace->count,
                trace->dropped);
        for (size_t i = 0; i < trace->count; i++) {
            const PGRAPHVkHybridTraceRecord *record =
                &trace->records[(trace->head + i) % HYBRID_TRACE_CAPACITY];
            fprintf(trace->file,
                    "%u,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%u,"
                    "%016" PRIx64 ",%016" PRIx64 ",%" PRIu64 ","
                    "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
                    "\n", record->type, record->frame, record->draw,
                    record->timestamp_us, record->route,
                    record->pipeline_hash, record->shader_hash,
                    record->ticket, record->data[0], record->data[1],
                    record->data[2], record->data[3]);
        }
    }
    trace->frame++;
    trace->draw = 0;
    trace->frame_start_us = now_us;
    trace->head = 0;
    trace->count = 0;
    trace->dropped = 0;
}

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/atomic.h"
#include "voice-write-trace.h"

typedef struct VoiceWriteCounts {
    uint64_t calls, changed, unchanged, ram_range;
    uint64_t changed_samples, changed_ns, unchanged_samples, unchanged_ns;
} VoiceWriteCounts;

struct McpxApuVoiceWriteTrace {
    FILE *file;
    VoiceWriteCounts fields[33][17];
    uint64_t frames, active_voice_sum, active_voice_max;
};

static VoiceWriteCounts *counts_for(McpxApuVoiceWriteTrace *trace,
                                    unsigned offset, unsigned phase)
{
    unsigned slot = offset < 128 && !(offset & 3) ? offset / 4 : 32;
    return &trace->fields[slot][MIN(phase, MCPX_VOICE_WRITE_OTHER_PHASE)];
}

McpxApuVoiceWriteTrace *mcpx_apu_voice_write_trace_open(const char *path)
{
    if (!path || !*path) {
        return NULL;
    }
    FILE *file = fopen(path, "w");
    if (!file) {
        return NULL;
    }
    McpxApuVoiceWriteTrace *trace = g_new0(McpxApuVoiceWriteTrace, 1);
    trace->file = file;
    fprintf(file,
            "voice-write-trace-v1\n"
            "summary,frames,active_voice_sum,active_voice_max,store_bytes\n"
            "field,offset,phase,calls,changed,unchanged,ram_range,"
            "changed_samples,changed_ns,unchanged_samples,unchanged_ns\n");
    return trace;
}

bool mcpx_apu_voice_write_trace_record(McpxApuVoiceWriteTrace *trace,
                                       unsigned offset, unsigned phase,
                                       uint32_t old, uint32_t value,
                                       bool ram_range)
{
    if (!trace) {
        return false;
    }
    VoiceWriteCounts *counts = counts_for(trace, offset, phase);
    uint64_t call = qatomic_fetch_inc(&counts->calls);
    if (old == value) {
        qatomic_inc(&counts->unchanged);
    } else {
        qatomic_inc(&counts->changed);
    }
    if (ram_range) {
        qatomic_inc(&counts->ram_range);
    }
    /* Mix the sequence so periodic voice/class schedules do not alias a
     * fixed stride. SplitMix64's finalizer gives reproducible sparse samples;
     * separate fields and phases use different sequences.
     */
    uint64_t sample =
        call + UINT64_C(0x9e3779b97f4a7c15) + ((uint64_t)offset << 32) + phase;
    sample = (sample ^ (sample >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    sample = (sample ^ (sample >> 27)) * UINT64_C(0x94d049bb133111eb);
    sample ^= sample >> 31;
    return (sample & 1023) == 0;
}

void mcpx_apu_voice_write_trace_sample(McpxApuVoiceWriteTrace *trace,
                                       unsigned offset, unsigned phase,
                                       bool changed, uint64_t elapsed_ns)
{
    if (trace) {
        VoiceWriteCounts *counts = counts_for(trace, offset, phase);
        if (changed) {
            qatomic_inc(&counts->changed_samples);
            qatomic_add(&counts->changed_ns, elapsed_ns);
        } else {
            qatomic_inc(&counts->unchanged_samples);
            qatomic_add(&counts->unchanged_ns, elapsed_ns);
        }
    }
}

void mcpx_apu_voice_write_trace_frame(McpxApuVoiceWriteTrace *trace,
                                      unsigned active_voices)
{
    if (trace) {
        trace->frames++;
        trace->active_voice_sum += active_voices;
        trace->active_voice_max = MAX(trace->active_voice_max, active_voices);
    }
}

void mcpx_apu_voice_write_trace_close(McpxApuVoiceWriteTrace *trace)
{
    if (trace) {
        uint64_t calls = 0;
        for (unsigned slot = 0; slot < 33; slot++) {
            for (unsigned phase = 0; phase < 17; phase++) {
                VoiceWriteCounts *c = &trace->fields[slot][phase];
                if (!c->calls) {
                    continue;
                }
                calls += c->calls;
                fprintf(trace->file,
                        "field,%u,%u,%" PRIu64 ",%" PRIu64 ",%" PRIu64
                        ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
                        ",%" PRIu64 "\n",
                        slot * 4, phase, c->calls, c->changed, c->unchanged,
                        c->ram_range, c->changed_samples, c->changed_ns,
                        c->unchanged_samples, c->unchanged_ns);
            }
        }
        fprintf(trace->file,
                "summary,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n",
                trace->frames, trace->active_voice_sum, trace->active_voice_max,
                calls * 4);
        bool failed = ferror(trace->file);
        failed |= fclose(trace->file) != 0;
        if (failed) {
            fprintf(stderr, "APU voice-write trace output failed\n");
        }
        g_free(trace);
    }
}

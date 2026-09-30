/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef MCPX_APU_VOICE_WRITE_TRACE_H
#define MCPX_APU_VOICE_WRITE_TRACE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct McpxApuVoiceWriteTrace McpxApuVoiceWriteTrace;

/* EF phases 0..7, EA phases 8..15, other writes 16. */
#define MCPX_VOICE_WRITE_OTHER_PHASE 16

McpxApuVoiceWriteTrace *mcpx_apu_voice_write_trace_open(const char *path);
bool mcpx_apu_voice_write_trace_record(McpxApuVoiceWriteTrace *trace,
                                       unsigned offset, unsigned phase,
                                       uint32_t old, uint32_t value,
                                       bool ram_range);
void mcpx_apu_voice_write_trace_sample(McpxApuVoiceWriteTrace *trace,
                                       unsigned offset, unsigned phase,
                                       bool changed, uint64_t elapsed_ns);
void mcpx_apu_voice_write_trace_frame(McpxApuVoiceWriteTrace *trace,
                                      unsigned active_voices);
void mcpx_apu_voice_write_trace_close(McpxApuVoiceWriteTrace *trace);

#endif

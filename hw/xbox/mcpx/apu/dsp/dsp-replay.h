/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef MCPX_DSP_REPLAY_H
#define MCPX_DSP_REPLAY_H

#include "qapi/error.h"
#include "dsp-jit-state.h"

#define DSP_REPLAY_MAX_IO 4096
#define DSP_REPLAY_MAX_BYTES (1024 * 1024)
#define DSP_REPLAY_MAX_CALLS 65536
#define DSP_REPLAY_MAX_FRAMES 64

typedef struct DSPReplayDMA {
    uint32_t configuration, control, start_block, next_block, dma_read_count;
    bool error, eol;
} DSPReplayDMA;

typedef struct DSPReplaySnapshot {
    DSPJitState jit;
    DSPReplayDMA dma;
    uint32_t interrupts;
    int32_t save_cycles;
} DSPReplaySnapshot;

typedef struct DSPReplayIO {
    bool fifo, dir;
    uint32_t address;
    GBytes *bytes;
} DSPReplayIO;

typedef struct DSPReplayCase {
    DSPReplaySnapshot before, after;
    uint32_t calls;
    bool realtime, is_gp;
    GPtrArray *io;
} DSPReplayCase;

DSPReplayCase *dsp_replay_case_new(void);
void dsp_replay_case_free(DSPReplayCase *data);
bool dsp_replay_save_case(const char *path, DSPReplayCase *data, Error **errp);
DSPReplayCase *dsp_replay_load_case(const char *path, Error **errp);
bool dsp_replay_verify_case(DSPReplayCase *data, Error **errp);
typedef enum DSPReplayNativeResult {
    DSP_REPLAY_NATIVE_SKIPPED,
    DSP_REPLAY_NATIVE_DONE,
    DSP_REPLAY_NATIVE_FAILED,
} DSPReplayNativeResult;
/* Offline-only helper: caller owns the quiesced DSP and verifies the program.
 */
DSPReplayNativeResult dsp_replay_native_dma_start(DSPState *dsp,
                                                  const DSPJitState *before,
                                                  DSPJitState *scratch,
                                                  bool preserve_cache,
                                                  Error **errp);
DSPReplayNativeResult dsp_replay_native_dma_sequence(DSPState *dsp,
                                                     const DSPJitState *before,
                                                     DSPJitState *scratch,
                                                     bool preserve_cache,
                                                     Error **errp);
DSPReplayNativeResult
dsp_replay_native_dma_stopfreeze(DSPState *dsp, const DSPJitState *before,
                                 DSPJitState *scratch, bool preserve_cache,
                                 Error **errp);
typedef struct DSPGPReplayStats {
    uint64_t feedback_calls, mix2_calls, add_calls, matrix_calls, meter_calls;
    uint64_t filter_calls, filter_final_calls, filter_nonzero_calls, mac_calls,
        delay_calls;
    uint64_t decimate_calls, gain_calls, dma_start_calls,
        dma_start_transactions;
    uint64_t dma_sequence_calls, dma_sequence_transactions,
        dma_stopfreeze_calls;
    uint64_t dsp_steps, native_cycles;
} DSPGPReplayStats;
/* Offline stepped replay; excludes run-loop cycle budget from comparison. */
bool dsp_replay_verify_gp_hle(DSPReplayCase *data, bool enable_native,
                              bool preserve_cache, DSPGPReplayStats *stats,
                              Error **errp);
/* Offline comparison only; this does not authorize serving native output. */
bool dsp_replay_verify_native_ep(DSPReplayCase *data, Error **errp);
bool dsp_replay_take_snapshot(DSPState *dsp, DSPReplaySnapshot *snapshot);
bool dsp_replay_snapshot_equal(const DSPReplaySnapshot *a,
                               const DSPReplaySnapshot *b,
                               const char **different_field);

/* All recorder operations run on the owning APU thread under its lock. */
bool dsp_replay_configure(DSPState *dsp, const char *path, uint32_t delay_ms,
                          Error **errp);
bool dsp_replay_configure_sequence(DSPState *dsp, const char *path,
                                   uint32_t delay_ms, uint32_t count,
                                   Error **errp);
void dsp_replay_init(DSPState *dsp);
void dsp_replay_cleanup(DSPState *dsp);
DSPReplay *dsp_replay_begin(DSPState *dsp, bool realtime);
bool dsp_replay_end(DSPReplay *capture, uint64_t calls, Error **errp);

#endif

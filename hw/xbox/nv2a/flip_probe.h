/*
 * Experimental NV2A relative-flip diagnostic.
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef NV2A_FLIP_PROBE_H
#define NV2A_FLIP_PROBE_H

#include "flip_probe_core.h"

typedef struct NV2AFlipProbeCounters {
    uint64_t guest_read3d;
    uint64_t completed_flip_stall;
    uint64_t vblank;
    uint64_t host_present;
    int64_t timestamp_us;
} NV2AFlipProbeCounters;

typedef struct NV2AFlipProbe {
    bool enabled;
    NV2AFlipProbeGate gate;
    uint64_t epoch;
    NV2AFlipProbeCounters counters;
    NV2AFlipProbeCounters arm;
    NV2AFlipProbeCounters quiesce;
    NV2AFlipProbeCounters pause;
    bool has_arm;
    bool has_quiesce;
    bool has_pause;
    const char *cancel_reason;
    QEMUBH *stop_bh;
    uint64_t pending_generation;
    uint64_t paused_generation;
    uint64_t presented_generation;
} NV2AFlipProbe;

void nv2a_flip_probe_init(NV2AState *d);
void nv2a_flip_probe_destroy(NV2AState *d);
void nv2a_flip_probe_read3d(NV2AState *d);
void nv2a_flip_probe_vblank(NV2AState *d);
bool nv2a_flip_probe_complete_stall(NV2AState *d);
void nv2a_flip_probe_cancel_locked(NV2AState *d, const char *reason,
                                   bool new_epoch);
void nv2a_flip_probe_resume_locked(NV2AState *d);

#endif

/*
 * Experimental NV2A relative-flip diagnostic.
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef NV2A_FLIP_PROBE_CORE_H
#define NV2A_FLIP_PROBE_CORE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum NV2AFlipProbePhase {
    NV2A_FLIP_PROBE_IDLE,
    NV2A_FLIP_PROBE_ARMED,
    NV2A_FLIP_PROBE_HELD,
    NV2A_FLIP_PROBE_PAUSED,
    NV2A_FLIP_PROBE_RELEASED,
    NV2A_FLIP_PROBE_CANCELLED,
} NV2AFlipProbePhase;

/* All operations on a gate are serialized by the owning PFIFO lock. */
typedef struct NV2AFlipProbeGate {
    NV2AFlipProbePhase phase;
    uint64_t generation;
    uint64_t start_read3d;
    uint64_t start_completed;
    uint64_t target;
    uint64_t held_read3d;
} NV2AFlipProbeGate;

bool nv2a_flip_probe_arm(NV2AFlipProbeGate *gate, uint64_t read3d,
                         uint64_t completed, uint64_t delta);
bool nv2a_flip_probe_complete(NV2AFlipProbeGate *gate, uint64_t read3d,
                              uint64_t completed);
bool nv2a_flip_probe_pause(NV2AFlipProbeGate *gate, uint64_t generation);
bool nv2a_flip_probe_held(const NV2AFlipProbeGate *gate);
void nv2a_flip_probe_cancel(NV2AFlipProbeGate *gate);
void nv2a_flip_probe_release(NV2AFlipProbeGate *gate);

#endif

/*
 * Experimental NV2A relative-flip diagnostic.
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "flip_probe_core.h"

bool nv2a_flip_probe_held(const NV2AFlipProbeGate *gate)
{
    return gate->phase == NV2A_FLIP_PROBE_HELD ||
           gate->phase == NV2A_FLIP_PROBE_PAUSED;
}

bool nv2a_flip_probe_arm(NV2AFlipProbeGate *gate, uint64_t read3d,
                         uint64_t completed, uint64_t delta)
{
    if (!delta || delta > 100000 || read3d > UINT64_MAX - delta ||
        gate->generation == UINT64_MAX ||
        gate->phase == NV2A_FLIP_PROBE_ARMED || nv2a_flip_probe_held(gate)) {
        return false;
    }
    gate->generation++;
    gate->phase = NV2A_FLIP_PROBE_ARMED;
    gate->start_read3d = read3d;
    gate->start_completed = completed;
    gate->target = read3d + delta;
    gate->held_read3d = 0;
    return true;
}

bool nv2a_flip_probe_complete(NV2AFlipProbeGate *gate, uint64_t read3d,
                              uint64_t completed)
{
    if (gate->phase != NV2A_FLIP_PROBE_ARMED || read3d < gate->target ||
        completed <= gate->start_completed) {
        return false;
    }
    gate->held_read3d = read3d;
    gate->phase = NV2A_FLIP_PROBE_HELD;
    return true;
}

bool nv2a_flip_probe_pause(NV2AFlipProbeGate *gate, uint64_t generation)
{
    if (gate->phase != NV2A_FLIP_PROBE_HELD || generation != gate->generation) {
        return false;
    }
    gate->phase = NV2A_FLIP_PROBE_PAUSED;
    return true;
}

void nv2a_flip_probe_cancel(NV2AFlipProbeGate *gate)
{
    gate->phase = NV2A_FLIP_PROBE_CANCELLED;
}

void nv2a_flip_probe_release(NV2AFlipProbeGate *gate)
{
    if (nv2a_flip_probe_held(gate)) {
        gate->phase = NV2A_FLIP_PROBE_RELEASED;
    }
}

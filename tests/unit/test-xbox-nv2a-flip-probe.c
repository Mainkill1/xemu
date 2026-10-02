/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/flip_probe_core.h"

int main(void)
{
    NV2AFlipProbeGate gate = { 0 };
    uint64_t first;

    g_assert_false(nv2a_flip_probe_arm(&gate, 20, 10, 0));
    g_assert_false(nv2a_flip_probe_arm(&gate, 20, 10, 100001));
    g_assert_false(nv2a_flip_probe_arm(&gate, UINT64_MAX - 2, 10, 3));
    g_assert_true(nv2a_flip_probe_arm(&gate, 20, 10, 3));
    first = gate.generation;
    g_assert_cmpuint(gate.target, ==, 23);
    g_assert_false(nv2a_flip_probe_arm(&gate, 22, 11, 3));
    g_assert_false(nv2a_flip_probe_complete(&gate, 22, 11));
    g_assert_false(nv2a_flip_probe_complete(&gate, 23, 10));
    g_assert_true(nv2a_flip_probe_complete(&gate, 24, 11));
    g_assert_cmpuint(gate.held_read3d, ==, 24);
    g_assert_false(nv2a_flip_probe_pause(&gate, first + 1));
    nv2a_flip_probe_cancel(&gate);
    g_assert_false(nv2a_flip_probe_pause(&gate, first));
    g_assert_true(nv2a_flip_probe_arm(&gate, 30, 15, 2));
    g_assert_false(nv2a_flip_probe_pause(&gate, first));
    g_assert_true(nv2a_flip_probe_complete(&gate, 32, 16));
    g_assert_true(nv2a_flip_probe_pause(&gate, gate.generation));
    g_assert_true(nv2a_flip_probe_held(&gate));
    nv2a_flip_probe_release(&gate);
    g_assert_false(nv2a_flip_probe_held(&gate));
    g_assert_false(nv2a_flip_probe_complete(&gate, 33, 17));
    g_assert_true(nv2a_flip_probe_arm(&gate, 33, 17, 1));
    nv2a_flip_probe_cancel(&gate);
    g_assert_false(nv2a_flip_probe_complete(&gate, 34, 18));
    gate.generation = UINT64_MAX;
    g_assert_false(nv2a_flip_probe_arm(&gate, 34, 18, 1));
    return 0;
}

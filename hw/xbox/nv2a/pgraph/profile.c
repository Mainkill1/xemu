/*
 * QEMU Geforce NV2A profiling helpers
 *
 * Copyright (c) 2020-2024 Matt Borgerson
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw/xbox/nv2a/nv2a_int.h"

NV2AStats g_nv2a_stats;

/* Temporary test-only probe. Count the guest's NV_PGRAPH_INCREMENT READ_3D
 * writes from process start and stop the vCPU at the requested occurrence. */
static uint64_t poc_flip_count;
static uint64_t poc_stop_flip;
static bool poc_stop_initialized;
static bool poc_stop_requested;
static unsigned int poc_overshoot_reports;

static void nv2a_profile_poc_stop_at_flip(int64_t now)
{
    if (!poc_stop_initialized) {
        const char *value = g_getenv("XEMU_POC_STOP_AFTER_NV2A_FLIPS");
        char *end = NULL;

        poc_stop_initialized = true;
        if (value && value[0]) {
            uint64_t parsed = g_ascii_strtoull(value, &end, 10);
            if (parsed && end != value && *end == '\0') {
                poc_stop_flip = parsed;
            } else {
                fprintf(stderr, "nv2a flip POC: invalid stop count '%s'\n",
                        value);
            }
        }
    }

    poc_flip_count++;
    if (poc_stop_requested && poc_overshoot_reports < 8) {
        poc_overshoot_reports++;
        fprintf(stderr, "nv2a flip POC: overshoot at flip=%" PRIu64 "\n",
                poc_flip_count);
    } else if (poc_stop_flip && poc_flip_count == poc_stop_flip) {
        poc_stop_requested = true;
        fprintf(stderr, "nv2a flip POC: stop requested at flip=%" PRIu64
                        " host_us=%" PRId64 "\n", poc_flip_count, now);
        vm_stop(RUN_STATE_PAUSED);
    }
}

void nv2a_profile_increment(void)
{
    int64_t now = qemu_clock_get_us(QEMU_CLOCK_REALTIME);
    nv2a_profile_poc_stop_at_flip(now);
    const int64_t fps_update_interval = 250000;
    g_nv2a_stats.last_flip_time = now;

    static int64_t frame_count = 0;
    frame_count++;

    static int64_t ts = 0;
    int64_t delta = now - ts;
    if (delta >= fps_update_interval) {
        g_nv2a_stats.increment_fps = frame_count * 1000000 / delta;
        ts = now;
        frame_count = 0;
    }
}

void nv2a_profile_flip_stall(void)
{
    int64_t now = qemu_clock_get_us(QEMU_CLOCK_REALTIME);
    int64_t render_time = (now-g_nv2a_stats.last_flip_time)/1000;

    g_nv2a_stats.frame_working.mspf = render_time;
    g_nv2a_stats.frame_history[g_nv2a_stats.frame_ptr] =
        g_nv2a_stats.frame_working;
    g_nv2a_stats.frame_ptr =
        (g_nv2a_stats.frame_ptr + 1) % NV2A_PROF_NUM_FRAMES;
    g_nv2a_stats.frame_count++;
    memset(&g_nv2a_stats.frame_working, 0, sizeof(g_nv2a_stats.frame_working));
}

const char *nv2a_profile_get_counter_name(unsigned int cnt)
{
    const char *default_names[NV2A_PROF__COUNT] = {
        #define _X(x) stringify(x),
        NV2A_PROF_COUNTERS_XMAC
        #undef _X
    };

    assert(cnt < NV2A_PROF__COUNT);
    return default_names[cnt] + 10; /* 'NV2A_PROF_' */
}

int nv2a_profile_get_counter_value(unsigned int cnt)
{
    assert(cnt < NV2A_PROF__COUNT);
    unsigned int idx = (g_nv2a_stats.frame_ptr + NV2A_PROF_NUM_FRAMES - 1) %
                       NV2A_PROF_NUM_FRAMES;
    return g_nv2a_stats.frame_history[idx].counters[cnt];
}

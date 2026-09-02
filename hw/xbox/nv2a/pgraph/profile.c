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

static void nv2a_profile_write_frame_log(int64_t now)
{
    static FILE *file;
    static bool initialized;
    static int64_t previous_frame;
    static int64_t last_flush;
    static uint64_t frame;

    if (!initialized) {
        const char *path;

        initialized = true;
        path = g_getenv("XEMU_FRAME_LOG");
        if (path == NULL || path[0] == '\0') {
            return;
        }
        file = qemu_fopen(path, "w");
        if (file == NULL) {
            fprintf(stderr, "nv2a: failed to open frame log '%s'\n", path);
            return;
        }
        previous_frame = now;
        last_flush = now;
    }

    if (file == NULL) {
        return;
    }

    frame++;
    fprintf(file, "timestamp_us=%" PRId64 " frame=%" PRIu64
                  " delta_us=%" PRId64 "\n",
            now, frame, now - previous_frame);
    previous_frame = now;

    /* Keep live stall detection within one second without forcing a disk
     * flush on every emulated frame. */
    if (now - last_flush >= G_USEC_PER_SEC) {
        fflush(file);
        last_flush = now;
    }
}

void nv2a_profile_log_event_once(const char *event)
{
    enum {
        EVENT_SHADER_COMPILE = 1 << 0,
        EVENT_GPU_SUBMIT = 1 << 1,
        EVENT_READBACK = 1 << 2,
    };
    static FILE *file;
    static GMutex lock;
    static bool initialized;
    static unsigned int written;
    unsigned int event_bit = 0;

    if (strcmp(event, "shader_compile") == 0) {
        event_bit = EVENT_SHADER_COMPILE;
    } else if (strcmp(event, "gpu_submit") == 0) {
        event_bit = EVENT_GPU_SUBMIT;
    } else if (strcmp(event, "readback") == 0) {
        event_bit = EVENT_READBACK;
    } else {
        return;
    }

    g_mutex_lock(&lock);
    if (!initialized) {
        const char *path;

        initialized = true;
        path = g_getenv("XEMU_PERF_EVENT_LOG");
        if (path != NULL && path[0] != '\0') {
            file = qemu_fopen(path, "w");
            if (file == NULL) {
                fprintf(stderr, "nv2a: failed to open event log '%s'\n", path);
            }
        }
    }

    if (file != NULL && !(written & event_bit)) {
        written |= event_bit;
        fprintf(file, "{\"timestamp_us\":%" PRId64
                      ",\"event\":\"%s\"}\n",
                qemu_clock_get_us(QEMU_CLOCK_REALTIME), event);
        fflush(file);
    }
    g_mutex_unlock(&lock);
}

static void nv2a_profile_write_flip_log(int64_t now)
{
    static FILE *file;
    static bool initialized;
    static int64_t window_start;
    static uint64_t window_frames;

    if (!initialized) {
        const char *path;

        initialized = true;
        path = g_getenv("XEMU_FLIP_LOG");
        if (path == NULL || path[0] == '\0') {
            return;
        }
        file = qemu_fopen(path, "w");
        if (file == NULL) {
            fprintf(stderr, "nv2a: failed to open flip log '%s'\n", path);
            return;
        }
        window_start = now;
    }

    if (file == NULL) {
        return;
    }

    window_frames++;
    int64_t elapsed_us = now - window_start;
    if (elapsed_us < 5 * G_USEC_PER_SEC) {
        return;
    }

    fprintf(file, "elapsed_us=%" PRId64 " frames=%" PRIu64
                  " fps=%.3f\n",
            elapsed_us, window_frames,
            window_frames * (double)G_USEC_PER_SEC / elapsed_us);
    fflush(file);
    window_start = now;
    window_frames = 0;
}

void nv2a_profile_increment(void)
{
    int64_t now = qemu_clock_get_us(QEMU_CLOCK_REALTIME);
    const int64_t fps_update_interval = 250000;
    g_nv2a_stats.last_flip_time = now;

    static int64_t frame_count = 0;
    frame_count++;
    nv2a_profile_write_flip_log(now);
    nv2a_profile_write_frame_log(now);

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

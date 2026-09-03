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

static int64_t last_flip_stall_us;
static int64_t last_vblank_us;
static uint64_t flip_stall_serial;
static uint64_t vblank_serial;
uint32_t g_nv2a_profile_timing_enabled;

typedef struct NV2AProfilePfifoAtomicStats {
    uint64_t region_calls[NV2A_PROFILE_PFIFO_REGION_COUNT];
    uint64_t region_us[NV2A_PROFILE_PFIFO_REGION_COUNT];
    uint64_t idle_wait_count;
    uint64_t idle_wait_us;
    uint64_t loop_count;
    uint64_t kick_skip_wait_count;
} NV2AProfilePfifoAtomicStats;

static NV2AProfilePfifoAtomicStats pfifo_stats;

static NV2AProfilePfifoAtomicStats nv2a_profile_take_pfifo_stats(void)
{
    NV2AProfilePfifoAtomicStats result = { 0 };

    for (unsigned int i = 0; i < NV2A_PROFILE_PFIFO_REGION_COUNT; i++) {
        result.region_calls[i] = qatomic_xchg(&pfifo_stats.region_calls[i], 0);
        result.region_us[i] = qatomic_xchg(&pfifo_stats.region_us[i], 0);
    }
    result.idle_wait_count = qatomic_xchg(&pfifo_stats.idle_wait_count, 0);
    result.idle_wait_us = qatomic_xchg(&pfifo_stats.idle_wait_us, 0);
    result.loop_count = qatomic_xchg(&pfifo_stats.loop_count, 0);
    result.kick_skip_wait_count =
        qatomic_xchg(&pfifo_stats.kick_skip_wait_count, 0);
    return result;
}

static void nv2a_profile_write_frame_log(
    int64_t now, int64_t work_to_flip_stall_us,
    int64_t flip_stall_to_increment_us, uint64_t flip_stalls,
    uint64_t vblanks, int64_t increment_after_vblank_us,
    const NV2AProfilePfifoAtomicStats *pfifo)
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
        qatomic_set(&g_nv2a_profile_timing_enabled, 1);
        previous_frame = now;
        last_flush = now;
    }

    if (file == NULL) {
        return;
    }

    frame++;
    fprintf(file, "timestamp_us=%" PRId64 " frame=%" PRIu64
                  " delta_us=%" PRId64
                  " work_to_flip_stall_us=%" PRId64
                  " flip_stall_to_increment_us=%" PRId64
                  " flip_stalls_since_previous_frame=%" PRIu64
                  " vblanks_since_previous_frame=%" PRIu64
                  " increment_after_vblank_us=%" PRId64
                  " pfifo_loop_count=%" PRIu64
                  " pfifo_kick_skip_wait_count=%" PRIu64
                  " pfifo_pending_calls=%" PRIu64
                  " pfifo_pending_us=%" PRIu64
                  " pfifo_pusher_calls=%" PRIu64
                  " pfifo_pusher_us=%" PRIu64
                  " pfifo_reports_calls=%" PRIu64
                  " pfifo_reports_us=%" PRIu64
                  " pfifo_idle_wait_count=%" PRIu64
                  " pfifo_idle_wait_us=%" PRIu64 "\n",
            now, frame, now - previous_frame, work_to_flip_stall_us,
            flip_stall_to_increment_us, flip_stalls, vblanks,
            increment_after_vblank_us, pfifo->loop_count,
            pfifo->kick_skip_wait_count,
            pfifo->region_calls[NV2A_PROFILE_PFIFO_PENDING],
            pfifo->region_us[NV2A_PROFILE_PFIFO_PENDING],
            pfifo->region_calls[NV2A_PROFILE_PFIFO_PUSHER],
            pfifo->region_us[NV2A_PROFILE_PFIFO_PUSHER],
            pfifo->region_calls[NV2A_PROFILE_PFIFO_REPORTS],
            pfifo->region_us[NV2A_PROFILE_PFIFO_REPORTS],
            pfifo->idle_wait_count, pfifo->idle_wait_us);
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
    int64_t previous_flip_us = qatomic_read(&g_nv2a_stats.last_flip_time);
    bool timing_enabled = qatomic_read(&g_nv2a_profile_timing_enabled);
    int64_t flip_stall_us = timing_enabled ?
        qatomic_read(&last_flip_stall_us) : 0;
    int64_t vblank_us = timing_enabled ?
        qatomic_read(&last_vblank_us) : 0;
    static uint64_t previous_flip_stall_serial;
    static uint64_t previous_vblank_serial;
    uint64_t current_flip_stall_serial = timing_enabled ?
        qatomic_read(&flip_stall_serial) : 0;
    uint64_t current_vblank_serial = timing_enabled ?
        qatomic_read(&vblank_serial) : 0;
    uint64_t flip_stalls =
        current_flip_stall_serial - previous_flip_stall_serial;
    uint64_t vblanks = current_vblank_serial - previous_vblank_serial;
    int64_t work_to_flip_stall_us =
        flip_stall_us >= previous_flip_us && flip_stall_us <= now ?
        flip_stall_us - previous_flip_us : 0;
    int64_t flip_stall_to_increment_us =
        flip_stall_us >= previous_flip_us && flip_stall_us <= now ?
        now - flip_stall_us : 0;
    int64_t increment_after_vblank_us =
        vblank_us > 0 && vblank_us <= now ? now - vblank_us : 0;
    NV2AProfilePfifoAtomicStats current_pfifo_stats = { 0 };

    if (timing_enabled) {
        current_pfifo_stats = nv2a_profile_take_pfifo_stats();
    }

    previous_flip_stall_serial = current_flip_stall_serial;
    previous_vblank_serial = current_vblank_serial;
    qatomic_set(&g_nv2a_stats.last_flip_time, now);

    static int64_t frame_count = 0;
    frame_count++;
    nv2a_profile_write_flip_log(now);
    nv2a_profile_write_frame_log(
        now, work_to_flip_stall_us, flip_stall_to_increment_us, flip_stalls,
        vblanks, increment_after_vblank_us, &current_pfifo_stats);

    static int64_t ts = 0;
    int64_t delta = now - ts;
    if (delta >= fps_update_interval) {
        g_nv2a_stats.increment_fps = frame_count * 1000000 / delta;
        ts = now;
        frame_count = 0;
    }
}

void nv2a_profile_pfifo_record_region(NV2AProfilePfifoRegion region,
                                       uint64_t elapsed_us)
{
    assert(region < NV2A_PROFILE_PFIFO_REGION_COUNT);
    qatomic_fetch_add(&pfifo_stats.region_calls[region], 1);
    qatomic_fetch_add(&pfifo_stats.region_us[region], elapsed_us);
}

void nv2a_profile_pfifo_record_idle_wait(uint64_t elapsed_us)
{
    qatomic_fetch_add(&pfifo_stats.idle_wait_count, 1);
    qatomic_fetch_add(&pfifo_stats.idle_wait_us, elapsed_us);
}

void nv2a_profile_pfifo_record_loop(bool skipped_wait_for_kick)
{
    qatomic_fetch_add(&pfifo_stats.loop_count, 1);
    if (skipped_wait_for_kick) {
        qatomic_fetch_add(&pfifo_stats.kick_skip_wait_count, 1);
    }
}

void nv2a_profile_flip_stall(void)
{
    int64_t now = qemu_clock_get_us(QEMU_CLOCK_REALTIME);
    int64_t render_time =
        (now - qatomic_read(&g_nv2a_stats.last_flip_time)) / 1000;

    if (qatomic_read(&g_nv2a_profile_timing_enabled)) {
        qatomic_set(&last_flip_stall_us, now);
        qatomic_inc(&flip_stall_serial);
    }

    g_nv2a_stats.frame_working.mspf = render_time;
    g_nv2a_stats.frame_history[g_nv2a_stats.frame_ptr] =
        g_nv2a_stats.frame_working;
    g_nv2a_stats.frame_ptr =
        (g_nv2a_stats.frame_ptr + 1) % NV2A_PROF_NUM_FRAMES;
    g_nv2a_stats.frame_count++;
    memset(&g_nv2a_stats.frame_working, 0, sizeof(g_nv2a_stats.frame_working));
}

void nv2a_profile_vblank(void)
{
    if (!qatomic_read(&g_nv2a_profile_timing_enabled)) {
        return;
    }
    qatomic_set(&last_vblank_us, qemu_clock_get_us(QEMU_CLOCK_REALTIME));
    qatomic_inc(&vblank_serial);
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

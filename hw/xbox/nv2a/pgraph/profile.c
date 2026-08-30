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
bool g_nv2a_perf_telemetry_enabled;

typedef struct NV2APerfScopeStats {
    uint64_t count;
    uint64_t total_ns;
    uint64_t max_ns;
} NV2APerfScopeStats;

#define NV2A_PERF_MAX_MEASUREMENT_WINDOWS 1024U

typedef struct NV2APerfMeasurementWindow {
    uint64_t index;
    uint64_t start_ns;
    uint64_t end_ns;
    uint64_t wall_ns;
    uint32_t frame_count;
    uint64_t gpu_timestamp_count;
    uint64_t gpu_timestamp_total_ns;
    uint64_t gpu_timestamp_max_ns;
    uint64_t counters[NV2A_PROF__COUNT];
    uint64_t logical_bytes[NV2A_PROF__COUNT];
    uint64_t transferred_bytes[NV2A_PROF__COUNT];
    NV2APerfScopeStats scopes[NV2A_PROF_SCOPE__COUNT];
} NV2APerfMeasurementWindow;

typedef struct NV2APerfTelemetry {
    bool timing_enabled;
    bool measurement_seen;
    bool measurement_active;
    bool final_window_valid;
    bool measurement_windows_overflowed;
    char *output_path;
    uint32_t measurement_start_frame;
    char vulkan_memory_mode[32];
    uint32_t vulkan_memory_property_flags;
    uint32_t measurement_window_count;
    uint64_t measurement_windows_completed;
    uint64_t measurement_windows_dropped;
    uint64_t measurement_protocol_errors;
    NV2APerfMeasurementWindow live_window;
    NV2APerfMeasurementWindow final_window;
    NV2APerfMeasurementWindow *measurement_windows;
} NV2APerfTelemetry;

static NV2APerfTelemetry telemetry;

static const char *nv2a_profile_get_scope_name(unsigned int scope)
{
    static const char *names[NV2A_PROF_SCOPE__COUNT] = {
        #define _X(x, kind) stringify(x),
        NV2A_PROF_SCOPES_XMAC
        #undef _X
    };

    assert(scope < NV2A_PROF_SCOPE__COUNT);
    return names[scope] + 16;
}

static const char *nv2a_profile_get_scope_kind(unsigned int scope)
{
    static const char *kinds[NV2A_PROF_SCOPE__COUNT] = {
        #define _X(x, kind) kind,
        NV2A_PROF_SCOPES_XMAC
        #undef _X
    };

    assert(scope < NV2A_PROF_SCOPE__COUNT);
    return kinds[scope];
}

void nv2a_profile_init(void)
{
    const char *output_path = g_getenv("XEMU_PERF_TELEMETRY_PATH");
    const char *level = g_getenv("XEMU_PERF_TELEMETRY_LEVEL");

    memset(&telemetry, 0, sizeof(telemetry));
    g_nv2a_perf_telemetry_enabled = output_path && output_path[0];
    if (!g_nv2a_perf_telemetry_enabled) {
        return;
    }

    telemetry.output_path = g_strdup(output_path);
    telemetry.timing_enabled = g_strcmp0(level, "timed") == 0;
    telemetry.measurement_windows =
        g_new0(NV2APerfMeasurementWindow, NV2A_PERF_MAX_MEASUREMENT_WINDOWS);
    atexit(nv2a_profile_finalize);
    fprintf(stderr, "xemu_perf: NV2A telemetry enabled (%s): %s\n",
            telemetry.timing_enabled ? "timed" : "counters",
            telemetry.output_path);
}

void nv2a_profile_record_counter(enum NV2A_PROF_COUNTERS_ENUM cnt)
{
    nv2a_profile_add_counter(cnt, 1);
}

void nv2a_profile_add_counter(enum NV2A_PROF_COUNTERS_ENUM cnt,
                              uint64_t amount)
{
    assert(cnt < NV2A_PROF__COUNT);
    if (telemetry.measurement_seen && !telemetry.measurement_active) {
        return;
    }
    telemetry.live_window.counters[cnt] += amount;
}

void nv2a_profile_add_bytes(enum NV2A_PROF_COUNTERS_ENUM cnt,
                            uint64_t logical_bytes,
                            uint64_t transferred_bytes)
{
    if (!g_nv2a_perf_telemetry_enabled ||
        (telemetry.measurement_seen && !telemetry.measurement_active)) {
        return;
    }

    assert(cnt < NV2A_PROF__COUNT);
    telemetry.live_window.logical_bytes[cnt] += logical_bytes;
    telemetry.live_window.transferred_bytes[cnt] += transferred_bytes;
}

int64_t nv2a_profile_scope_begin(void)
{
    if (!g_nv2a_perf_telemetry_enabled || !telemetry.timing_enabled ||
        (telemetry.measurement_seen && !telemetry.measurement_active)) {
        return -1;
    }
    return qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
}

void nv2a_profile_scope_end(enum NV2A_PROF_SCOPES_ENUM scope,
                            int64_t start_ns)
{
    if (start_ns < 0) {
        return;
    }

    assert(scope < NV2A_PROF_SCOPE__COUNT);
    int64_t elapsed_ns = qemu_clock_get_ns(QEMU_CLOCK_REALTIME) - start_ns;
    NV2APerfScopeStats *stats = &telemetry.live_window.scopes[scope];
    stats->count++;
    stats->total_ns += MAX(elapsed_ns, 0);
    stats->max_ns = MAX(stats->max_ns, (uint64_t)MAX(elapsed_ns, 0));
}

void nv2a_profile_marker(uint8_t marker)
{
    if (!g_nv2a_perf_telemetry_enabled) {
        return;
    }

    if (marker == XEMU_PERF_MARKER_MEASURE_BEGIN) {
        if (telemetry.measurement_active) {
            telemetry.measurement_protocol_errors++;
        }
        memset(&telemetry.live_window, 0, sizeof(telemetry.live_window));
        telemetry.measurement_seen = true;
        telemetry.measurement_active = true;
        telemetry.live_window.start_ns = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
        telemetry.measurement_start_frame = g_nv2a_stats.frame_count;
    } else if (marker == XEMU_PERF_MARKER_MEASURE_END) {
        if (!telemetry.measurement_active) {
            telemetry.measurement_protocol_errors++;
            return;
        }

        NV2APerfMeasurementWindow *window = &telemetry.live_window;
        window->index = telemetry.measurement_windows_completed++;
        window->end_ns = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
        window->wall_ns = window->end_ns >= window->start_ns ?
                          window->end_ns - window->start_ns : 0;
        window->frame_count = g_nv2a_stats.frame_count -
                              telemetry.measurement_start_frame;
        telemetry.final_window = *window;
        telemetry.final_window_valid = true;
        if (telemetry.measurement_window_count <
            NV2A_PERF_MAX_MEASUREMENT_WINDOWS) {
            telemetry.measurement_windows[telemetry.measurement_window_count++] =
                *window;
        } else {
            telemetry.measurement_windows_overflowed = true;
            telemetry.measurement_windows_dropped++;
        }
        telemetry.measurement_active = false;
    }
}

void nv2a_profile_set_vulkan_memory(const char *mode,
                                    uint32_t memory_property_flags)
{
    g_strlcpy(telemetry.vulkan_memory_mode, mode,
              sizeof(telemetry.vulkan_memory_mode));
    telemetry.vulkan_memory_property_flags = memory_property_flags;
}

void nv2a_profile_record_gpu_duration(uint64_t duration_ns)
{
    if (!g_nv2a_perf_telemetry_enabled ||
        (telemetry.measurement_seen && !telemetry.measurement_active)) {
        return;
    }
    telemetry.live_window.gpu_timestamp_count++;
    telemetry.live_window.gpu_timestamp_total_ns += duration_ns;
    telemetry.live_window.gpu_timestamp_max_ns =
        MAX(telemetry.live_window.gpu_timestamp_max_ns, duration_ns);
}

static void nv2a_profile_append_window_metrics(
    GString *json, const NV2APerfMeasurementWindow *window,
    int indent, bool trailing_comma)
{
    g_string_append_printf(json, "%*s\"measurement_start_ns\": %" PRIu64 ",\n",
                           indent, "", window->start_ns);
    g_string_append_printf(json, "%*s\"measurement_end_ns\": %" PRIu64 ",\n",
                           indent, "", window->end_ns);
    g_string_append_printf(json, "%*s\"measurement_wall_ns\": %" PRIu64 ",\n",
                           indent, "", window->wall_ns);
    g_string_append_printf(json, "%*s\"measurement_frame_count\": %u,\n",
                           indent, "", window->frame_count);
    g_string_append_printf(json, "%*s\"gpu_timestamp\": {\"count\": %" PRIu64
                           ", \"total_ns\": %" PRIu64 ", \"max_ns\": %" PRIu64 "},\n",
                           indent, "", window->gpu_timestamp_count,
                           window->gpu_timestamp_total_ns,
                           window->gpu_timestamp_max_ns);
    g_string_append_printf(json, "%*s\"counter_totals\": {\n", indent, "");
    for (unsigned int i = 0; i < NV2A_PROF__COUNT; i++) {
        g_string_append_printf(json, "%*s\"%s\": %" PRIu64 "%s\n",
                               indent + 2, "", nv2a_profile_get_counter_name(i),
                               window->counters[i],
                               i + 1 == NV2A_PROF__COUNT ? "" : ",");
    }
    g_string_append_printf(json, "%*s},\n%*s\"byte_totals\": {\n",
                           indent, "", indent, "");
    for (unsigned int i = 0; i < NV2A_PROF__COUNT; i++) {
        g_string_append_printf(json, "%*s\"%s\": {\"logical\": %" PRIu64
                               ", \"transferred\": %" PRIu64 "}%s\n",
                               indent + 2, "", nv2a_profile_get_counter_name(i),
                               window->logical_bytes[i], window->transferred_bytes[i],
                               i + 1 == NV2A_PROF__COUNT ? "" : ",");
    }
    g_string_append_printf(json, "%*s},\n%*s\"scopes\": {\n",
                           indent, "", indent, "");
    for (unsigned int i = 0; i < NV2A_PROF_SCOPE__COUNT; i++) {
        const NV2APerfScopeStats *stats = &window->scopes[i];
        g_string_append_printf(json, "%*s\"%s\": {\"kind\": \"%s\", \"count\": %" PRIu64
                               ", \"total_ns\": %" PRIu64 ", \"max_ns\": %" PRIu64 "}%s\n",
                               indent + 2, "", nv2a_profile_get_scope_name(i),
                               nv2a_profile_get_scope_kind(i), stats->count,
                               stats->total_ns, stats->max_ns,
                               i + 1 == NV2A_PROF_SCOPE__COUNT ? "" : ",");
    }
    g_string_append_printf(json, "%*s}%s\n", indent, "",
                           trailing_comma ? "," : "");
}

void nv2a_profile_finalize(void)
{
    if (!g_nv2a_perf_telemetry_enabled) {
        return;
    }

    static const NV2APerfMeasurementWindow empty_window;
    const NV2APerfMeasurementWindow *top_window =
        telemetry.final_window_valid ? &telemetry.final_window : &empty_window;
    const char *status = telemetry.measurement_protocol_errors ? "protocol_error" :
                         telemetry.measurement_windows_overflowed ? "overflow" :
                         telemetry.measurement_active ||
                         !telemetry.measurement_windows_completed ? "incomplete" :
                         "ok";
    GString *json = g_string_new("{\n");
    g_string_append(json, "  \"schema_version\": 2,\n");
    g_string_append_printf(json, "  \"timing_enabled\": %s,\n",
                           telemetry.timing_enabled ? "true" : "false");
    g_string_append_printf(json, "  \"measurement_seen\": %s,\n",
                           telemetry.measurement_seen ? "true" : "false");
    g_string_append_printf(json, "  \"measurement_active\": %s,\n",
                           telemetry.measurement_active ? "true" : "false");
    g_string_append_printf(json, "  \"frame_count\": %u,\n", g_nv2a_stats.frame_count);
    g_string_append_printf(json, "  \"measurement_windows_capacity\": %u,\n",
                           NV2A_PERF_MAX_MEASUREMENT_WINDOWS);
    g_string_append_printf(json, "  \"measurement_windows_count\": %u,\n",
                           telemetry.measurement_window_count);
    g_string_append_printf(json, "  \"measurement_windows_completed\": %" PRIu64 ",\n",
                           telemetry.measurement_windows_completed);
    g_string_append_printf(json, "  \"measurement_windows_dropped\": %" PRIu64 ",\n",
                           telemetry.measurement_windows_dropped);
    g_string_append_printf(json, "  \"measurement_windows_overflowed\": %s,\n",
                           telemetry.measurement_windows_overflowed ? "true" : "false");
    g_string_append_printf(json, "  \"measurement_protocol_errors\": %" PRIu64 ",\n",
                           telemetry.measurement_protocol_errors);
    g_string_append_printf(json, "  \"measurement_windows_status\": \"%s\",\n", status);
    g_string_append_printf(json, "  \"vulkan_vertex_ram\": {\"mode\": \"%s\", \"memory_property_flags\": %u},\n",
                           telemetry.vulkan_memory_mode[0] ?
                           telemetry.vulkan_memory_mode : "not-applicable",
                           telemetry.vulkan_memory_property_flags);
    nv2a_profile_append_window_metrics(json, top_window, 2, true);
    g_string_append(json, "  \"measurement_windows\": [\n");
    for (uint32_t i = 0; i < telemetry.measurement_window_count; i++) {
        g_string_append(json, "    {\n");
        g_string_append_printf(json, "      \"index\": %" PRIu64 ",\n",
                               telemetry.measurement_windows[i].index);
        nv2a_profile_append_window_metrics(json, &telemetry.measurement_windows[i],
                                           6, false);
        g_string_append_printf(json, "    }%s\n",
                               i + 1 == telemetry.measurement_window_count ? "" : ",");
    }
    g_string_append(json, "  ]\n}\n");

    GError *error = NULL;
    if (!g_file_set_contents(telemetry.output_path, json->str, json->len, &error)) {
        fprintf(stderr, "xemu_perf: failed to write telemetry: %s\n", error->message);
        g_error_free(error);
    }
    g_string_free(json, true);
    g_clear_pointer(&telemetry.measurement_windows, g_free);
    g_clear_pointer(&telemetry.output_path, g_free);
    g_nv2a_perf_telemetry_enabled = false;
}

void nv2a_profile_increment(void)
{
    int64_t now = qemu_clock_get_us(QEMU_CLOCK_REALTIME);
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

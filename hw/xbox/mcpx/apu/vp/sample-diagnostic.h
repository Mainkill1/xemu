/* SPDX-License-Identifier: LGPL-2.0-or-later */
/* Diagnostic branch only. Counters are single-writer; report with workers idle. */
#ifndef MCPX_SAMPLE_DIAGNOSTIC_H
#define MCPX_SAMPLE_DIAGNOSTIC_H
#include "qemu/timer.h"
#define DIAG_FIELDS(X) \
    X(callbacks) X(pcm_callbacks) X(adpcm_callbacks) X(stream_callbacks) \
    X(paused_callbacks) X(requested_samples) X(returned_samples) \
    X(descriptor_reads) X(descriptor_hits) X(descriptor_misses) \
    X(descriptor_phys) X(descriptor_init) X(descriptor_destroy) \
    X(descriptor_flatview) X(descriptor_remap) \
    X(payload_reads) X(payload_hits) X(payload_misses) X(payload_init) \
    X(payload_destroy) X(payload_phys) X(payload_flatview) X(payload_remap) \
    X(payload_range_miss) X(payload_non_direct) X(payload_mapped_bytes) \
    X(adpcm_blocks) X(stream_blocks) X(adpcm_decodes) X(adpcm_decode_hits) \
    X(pcm_u8) X(pcm_s16) X(pcm_s24) X(pcm_s32) \
    X(voice_processes) X(resample_calls) X(worker_batches) X(worker_voices) X(dispatches) X(vp_frames)
#define DIAG_TIMERS(X) \
    X(callback) X(descriptor) X(descriptor_refill) X(payload) \
    X(payload_refill) X(cleanup) X(pcm_read) X(decode) \
    X(worker_process) X(worker_lock) X(dispatch_wait) X(vp_frame) \
    X(voice_process) X(resample) X(hrtf) X(descriptor_cleanup)
#define DIAG_ENUM(n) DIAG_##n,
enum { DIAG_TIMERS(DIAG_ENUM) DIAG_TIMER_COUNT };
#undef DIAG_ENUM
#define DIAG_NAME(n) #n,
static const char *const diag_timer_names[] = { DIAG_TIMERS(DIAG_NAME) };
#undef DIAG_NAME

typedef struct MCPXDiagTiming {
    uint64_t count, ns, max_ns, histogram[32];
} MCPXDiagTiming;
typedef struct MCPXDiag {
#define DIAG_MEMBER(n) uint64_t n;
    DIAG_FIELDS(DIAG_MEMBER)
#undef DIAG_MEMBER
    MCPXDiagTiming timing[DIAG_TIMER_COUNT];
    bool sampling;
    uint32_t rng;
} QEMU_ALIGNED(64) MCPXDiag;
static MCPXDiag diag_workers[17];
static __thread MCPXDiag *diag_current = &diag_workers[16];
static int diag_reader_mode = 2;
/* 0: outer scopes, 1: all inclusive probes, 2+timer: one selected scope. */
static int diag_timer_mode;
static unsigned diag_sample_mask = 127;
static bool diag_enabled;
static int64_t diag_last_report;
static uint64_t diag_reports, diag_report_ns;
#define DIAG_COUNT(n) (diag_current->n++)
#define DIAG_ADD(n, v) (diag_current->n += (v))

static inline bool diag_should_sample(void)
{
    uint32_t x = diag_current->rng ?: UINT32_C(0x9e3779b9);
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    diag_current->rng = x;
    return !(x & diag_sample_mask);
}
static inline bool diag_timer_enabled(int timer)
{
    return diag_timer_mode == 1 || diag_timer_mode == timer + 2 ||
           (diag_timer_mode == 0 && (timer == DIAG_callback ||
            (timer >= DIAG_worker_process && timer <= DIAG_hrtf)));
}
static inline int64_t diag_start(int timer)
{
    return diag_current->sampling && diag_timer_enabled(timer) ? get_clock() : 0;
}
static inline void diag_stop(int timer, int64_t start)
{
    if (!start) {
        return;
    }
    uint64_t elapsed = MAX(INT64_C(0), get_clock() - start);
    MCPXDiagTiming *t = &diag_current->timing[timer];
    unsigned bucket = 0;
    for (uint64_t n = elapsed; n > 1 && bucket < 31; n >>= 1) {
        bucket++;
    }
    t->count++;
    t->ns += elapsed;
    t->max_ns = MAX(t->max_ns, elapsed);
    t->histogram[bucket]++;
}
typedef struct MCPXDiagScope {
    int64_t start;
    int timer;
    bool previous_sampling;
} MCPXDiagScope;
static inline MCPXDiagScope diag_scope_begin(int timer, bool sample)
{
    MCPXDiagScope scope = { .timer = timer,
        .previous_sampling = diag_current->sampling };
    diag_current->sampling = sample;
    scope.start = diag_start(timer);
    return scope;
}
static inline void diag_scope_end(MCPXDiagScope *scope)
{
    diag_stop(scope->timer, scope->start);
    diag_current->sampling = scope->previous_sampling;
}
G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(MCPXDiagScope, diag_scope_end)

static void G_GNUC_UNUSED diag_init(void)
{
    const char *mode = getenv("XEMU_APU_DIAG");
    diag_enabled = mode && *mode;
    if (diag_enabled) {
        if (!strcmp(mode, "baseline")) {
            diag_reader_mode = 0;
        } else if (!strcmp(mode, "descriptor")) {
            diag_reader_mode = 1;
        } else if (!strcmp(mode, "payload")) {
            diag_reader_mode = 2;
        } else {
            fprintf(stderr, "Invalid XEMU_APU_DIAG reader: %s\n", mode);
            abort();
        }
    }
    const char *timers = getenv("XEMU_APU_DIAG_TIMERS");
    diag_timer_mode = 0;
    if (timers && strcmp(timers, "outer")) {
        diag_timer_mode = -1;
        if (!strcmp(timers, "all")) { diag_timer_mode = 1; }
        for (int i = 0; i < DIAG_TIMER_COUNT; i++) {
            if (!strcmp(timers, diag_timer_names[i])) { diag_timer_mode = i + 2; }
        }
        assert(diag_timer_mode >= 0);
    }
    const char *sampling = getenv("XEMU_APU_DIAG_SAMPLE_LOG2");
    if (sampling) {
        char *end;
        unsigned long n = strtoul(sampling, &end, 10);
        assert(*sampling && !*end && n <= 16);
        diag_sample_mask = (1U << n) - 1;
    }
    memset(diag_workers, 0, sizeof(diag_workers));
    diag_current = &diag_workers[16];
    diag_last_report = get_clock();
    diag_reports = diag_report_ns = 0;
    if (diag_enabled) {
        int64_t sum = 0;
        for (int i = 0; i < 4096; i++) {
            int64_t start = get_clock();
            sum += get_clock() - start;
        }
        fprintf(stderr, "APU_DIAG_META {\"clock_pairs\":4096,"
                "\"clock_pair_ns\":%" PRId64 ",\"worker_stride\":%zu}\n",
                sum, sizeof(MCPXDiag));
    }
}
/* All worker updates precede pending-bit clear under the dispatch lock. */
static void G_GNUC_UNUSED diag_report(bool force)
{
    if (!diag_enabled) {
        return;
    }
    int64_t now = get_clock();
    if (!force && now - diag_last_report < INT64_C(1000000000)) {
        return;
    }
    MCPXDiag total = { 0 };
    for (unsigned i = 0; i < ARRAY_SIZE(diag_workers); i++) {
        MCPXDiag *w = &diag_workers[i];
#define DIAG_SUM(n) total.n += w->n;
        DIAG_FIELDS(DIAG_SUM)
#undef DIAG_SUM
        for (int j = 0; j < DIAG_TIMER_COUNT; j++) {
            total.timing[j].count += w->timing[j].count;
            total.timing[j].ns += w->timing[j].ns;
            total.timing[j].max_ns = MAX(total.timing[j].max_ns,
                                         w->timing[j].max_ns);
            for (int b = 0; b < 32; b++) {
                total.timing[j].histogram[b] += w->timing[j].histogram[b];
            }
        }
    }
    GString *s = g_string_new("APU_DIAG {\"version\":1");
    g_string_append_printf(s, ",\"mode\":%d,\"timerMode\":%d,\"sampleEvery\":%u,"
                           "\"monotonic_ns\":%" PRId64 ",\"utc_us\":%" PRId64
                           ",\"reports\":%" PRIu64 ",\"report_ns\":%" PRIu64,
                           diag_reader_mode, diag_timer_mode, diag_sample_mask + 1, now,
                           (int64_t)g_get_real_time(), diag_reports,
                           diag_report_ns);
#define DIAG_PRINT(n) g_string_append_printf(s, ",\"" #n "\":%" PRIu64, total.n);
    DIAG_FIELDS(DIAG_PRINT)
#undef DIAG_PRINT
    g_string_append(s, ",\"timers\":{");
    for (int j = 0; j < DIAG_TIMER_COUNT; j++) {
        MCPXDiagTiming *t = &total.timing[j];
        g_string_append_printf(s, "%s\"%s\":{\"count\":%" PRIu64
            ",\"ns\":%" PRIu64 ",\"max_ns\":%" PRIu64 ",\"histogram\":[",
            j ? "," : "", diag_timer_names[j], t->count, t->ns, t->max_ns);
        for (int b = 0; b < 32; b++) {
            g_string_append_printf(s, "%s%" PRIu64, b ? "," : "", t->histogram[b]);
        }
        g_string_append(s, "]}");
    }
    g_string_append(s, "},\"workers\":[");
    for (int i = 0; i < 16; i++) {
        MCPXDiag *w = &diag_workers[i];
        g_string_append_printf(s, "%s{\"id\":%d,\"batches\":%" PRIu64
            ",\"voices\":%" PRIu64 ",\"callbacks\":%" PRIu64
            ",\"work_ns\":%" PRIu64 ",\"lock_ns\":%" PRIu64
            ",\"work_count\":%" PRIu64 ",\"lock_count\":%" PRIu64
            ",\"work_max_ns\":%" PRIu64 ",\"lock_max_ns\":%" PRIu64 "}",
            i ? "," : "", i, w->worker_batches, w->worker_voices,
            w->callbacks, w->timing[DIAG_worker_process].ns,
            w->timing[DIAG_worker_lock].ns,
            w->timing[DIAG_worker_process].count,
            w->timing[DIAG_worker_lock].count,
            w->timing[DIAG_worker_process].max_ns,
            w->timing[DIAG_worker_lock].max_ns);
    }
    g_string_append(s, "]}\n");
    fputs(s->str, stderr);
    g_string_free(s, true);
    diag_last_report = now;
    diag_reports++;
    diag_report_ns += get_clock() - now;
}
#endif

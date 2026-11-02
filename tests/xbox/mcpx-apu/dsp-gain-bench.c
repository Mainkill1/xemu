/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Linux fixed-work public gain projection benchmark, never live dispatch. */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "hw/xbox/mcpx/apu/dsp/dsp_capture.h"
#include "hw/xbox/mcpx/apu/dsp/gain_shadow.h"
#include "ui/xemu-settings.h"
#include <sys/resource.h>
#include <time.h>

typedef struct GainBench {
    DSPState *dsp;
    uint32_t input[64], expected[192], output[192];
    uint8_t scratch[8192];
    uint64_t exports, exported_bytes;
    bool valid;
} GainBench;

static void scratch(void *opaque, uint8_t *data, uint32_t address, size_t size,
                    bool write)
{
    GainBench *b = opaque;
    if (!write || address != 0x1800 || size != 768) {
        b->valid = false;
        return;
    }
    memcpy(b->scratch + address, data, size);
    b->exports++;
    b->exported_bytes += size;
}

static void fifo(void *opaque, uint8_t *data, unsigned index, size_t size,
                 bool write)
{
    ((GainBench *)opaque)->valid = false;
}

static uint64_t clock_ns(clockid_t clock)
{
    struct timespec time;
    g_assert(clock_gettime(clock, &time) == 0);
    return (uint64_t)time.tv_sec * 1000000000 + time.tv_nsec;
}

static uint64_t memory_kib(const char *field)
{
    FILE *file = fopen("/proc/self/status", "r");
    char line[256], key[64];
    uint64_t value = 0;
    if (!file) {
        return 0;
    }
    while (fgets(line, sizeof(line), file)) {
        unsigned long measured;
        if (sscanf(line, "%63s %lu", key, &measured) == 2 &&
            !strcmp(key, field)) {
            value = measured;
            break;
        }
    }
    fclose(file);
    return value;
}

static bool load_words(const char *path, uint32_t *words, size_t count)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return false;
    }
    bool ok = true;
    for (size_t i = 0; i < count; i++) {
        uint8_t bytes[4];
        if (fread(bytes, 1, 4, file) != 4) {
            ok = false;
            break;
        }
        words[i] = ldl_le_p(bytes);
        if (words[i] > 0xffffff) {
            ok = false;
            break;
        }
    }
    ok = ok && fgetc(file) == EOF && !ferror(file);
    fclose(file);
    return ok;
}

static bool verify(GainBench *b, uint64_t old_exports)
{
    if (!b->valid || b->exports != old_exports + 1 ||
        memcmp(b->output, b->expected, sizeof(b->output))) {
        return false;
    }
    for (unsigned i = 0; i < 192; i++) {
        if (ldl_le_p(b->scratch + 0x1800 + 4 * i) != b->expected[i]) {
            return false;
        }
    }
    return true;
}

static bool block(GainBench *b, bool native)
{
    uint64_t old_exports = b->exports;
    /* Include output materialization and verification in both timed paths. */
    memset(b->output, 0x55, sizeof(b->output));
    memset(b->scratch + 0x1800, 0x55, 768);
    if (native) {
        uint32_t input[64];
        uint8_t bytes[768];
        memcpy(input, b->input, sizeof(input));
        for (unsigned route = 0; route < 3; route++) {
            dsp_gain_compute_route(route, input, b->output + 64 * route);
        }
        for (unsigned i = 0; i < 192; i++) {
            stl_le_p(bytes + 4 * i, b->output[i]);
        }
        scratch(b, bytes, 0x1800, sizeof(bytes), true);
    } else {
        for (unsigned i = 0; i < 64; i++) {
            dsp_write_memory(b->dsp, 'X', 0x100 + i, b->input[i]);
        }
        dsp_write_memory(b->dsp, 'X', 0x3f0, 0);
        dsp_set_halt_requested(b->dsp, false);
        uint32_t cycles_before = dsp_get_cycle_count(b->dsp);
        dsp_run(b->dsp, 10000);
        if (!dsp_get_halt_requested(b->dsp) ||
            dsp_read_memory(b->dsp, 'X', 0x3f0) != 0x51d00d) {
            return false;
        }
        uint32_t consumed = dsp_get_cycle_count(b->dsp) - cycles_before;
        if (consumed > 20000) {
            return false;
        }
        /* run() adds to its signed budget. Drain the remainder while halted
         * so repeated batches cannot overflow it or accumulate free credit. */
        dsp_run(b->dsp, (int)consumed - 10000);
        for (unsigned i = 0; i < 192; i++) {
            b->output[i] = dsp_read_memory(b->dsp, 'X', 0x200 + i);
        }
        for (unsigned i = 0; i < 64; i++) {
            if (dsp_read_memory(b->dsp, 'X', 0x100 + i) != b->input[i]) {
                return false;
            }
        }
    }
    return verify(b, old_exports);
}

static bool initialize_dsp(GainBench *b, const uint32_t program[4096])
{
    g_config.audio.use_dsp_jit = true;
    b->dsp = dsp_init(b, scratch, fifo, true);
    DspJitCapture *initial = g_new0(DspJitCapture, 1);
    bool ok = dsp_jit_capture(b->dsp, initial);
    memcpy(initial->pram, program, sizeof(initial->pram));
    memset(initial->xram, 0, sizeof(initial->xram));
    memset(initial->yram, 0, sizeof(initial->yram));
    const uint32_t node[] = { 0x4000, 0x19e2, 192, 0x200, 0x1800, 0, 0 };
    memcpy(initial->xram + 0x20, node, sizeof(node));
    initial->state.pc = 0;
    initial->state.halt_requested = false;
    ok = ok && dsp_jit_restore(b->dsp, initial);
    g_free(initial);
    return ok;
}

static bool trace(GainBench *b, uint64_t *steps, uint64_t routes[3])
{
    DspJitCapture *state = g_new0(DspJitCapture, 1);
    uint64_t exports = b->exports;
    dsp_set_halt_requested(b->dsp, false);
    *steps = 0;
    while (*steps < 4096 && !dsp_get_halt_requested(b->dsp)) {
        if (!dsp_jit_capture(b->dsp, state)) {
            g_free(state);
            return false;
        }
        uint32_t pc = state->state.pc;
        for (unsigned i = 0; i < 3; i++) {
            if (pc >= 261 + 322 * i && pc < 261 + 322 * (i + 1)) {
                routes[i]++;
            }
        }
        dsp_step(b->dsp);
        (*steps)++;
    }
    g_free(state);
    for (unsigned i = 0; i < 192; i++) {
        b->output[i] = dsp_read_memory(b->dsp, 'X', 0x200 + i);
    }
    return dsp_get_halt_requested(b->dsp) && verify(b, exports);
}

static bool parse_count(const char *text, unsigned long *value,
                        unsigned long min, unsigned long max)
{
    char *end;
    errno = 0;
    *value = strtoul(text, &end, 10);
    return !errno && end != text && !*end && *value >= min && *value <= max;
}

int __wrap_main(int argc, char **argv);
int __wrap_main(int argc, char **argv)
{
    unsigned long count, warmup;
    if (argc != 7 || (strcmp(argv[1], "dsp") && strcmp(argv[1], "native")) ||
        !parse_count(argv[5], &count, 1, 1000000) ||
        !parse_count(argv[6], &warmup, 1, 10000)) {
        puts("{\"status\":\"INVALID\"}");
        return 2;
    }
    bool native = !strcmp(argv[1], "native");
    GainBench *b = g_new0(GainBench, 1);
    uint32_t program[4096];
    b->valid = true;
    if (!load_words(argv[2], program, 4096) ||
        !load_words(argv[3], b->input, 64) ||
        !load_words(argv[4], b->expected, 192)) {
        puts("{\"status\":\"INVALID\"}");
        return 2;
    }
    g_autofree gchar *image = NULL;
    gsize length;
    if (!g_file_get_contents(argv[2], &image, &length, NULL)) {
        return 2;
    }
    g_autofree char *identity = g_compute_checksum_for_data(
        G_CHECKSUM_SHA256, (const guchar *)image, length);
    if (strcmp(identity, "41a14ad0eff5ce00e45fb73c81f497d1d1056bf1ffc6301b91b1d"
                         "78d5663778a")) {
        puts("{\"status\":\"UNSUPPORTED\"}");
        return 2;
    }
    uint64_t first_start = clock_ns(CLOCK_MONOTONIC);
    if ((!native && !initialize_dsp(b, program)) || !block(b, native)) {
        puts("{\"status\":\"MISMATCH\"}");
        return 1;
    }
    uint64_t first_ns = clock_ns(CLOCK_MONOTONIC) - first_start;
    uint64_t instructions = 0, route_steps[3] = { 0 };
    if (!native) {
        /* Only the post-halt idle backedge changes; original work just passed.
         * Retain the cached arithmetic program across repeated blocks. */
        if (program[1237] != 0x0af080 || program[1238] != 1235) {
            puts("{\"status\":\"UNSUPPORTED\"}");
            return 2;
        }
        dsp_write_memory(b->dsp, 'P', 1238, 0);
        if (!trace(b, &instructions, route_steps)) {
            puts("{\"status\":\"MISMATCH\"}");
            return 1;
        }
    }
    for (unsigned long i = 0; i < warmup; i++) {
        if (!block(b, native)) {
            puts("{\"status\":\"MISMATCH\"}");
            return 1;
        }
    }
    uint64_t exports = b->exports, bytes = b->exported_bytes;
    uint64_t rss_before = memory_kib("VmRSS:");
    struct rusage before, after;
    getrusage(RUSAGE_SELF, &before);
    uint64_t cpu_start = clock_ns(CLOCK_PROCESS_CPUTIME_ID);
    uint64_t wall_start = clock_ns(CLOCK_MONOTONIC);
    for (unsigned long i = 0; i < count; i++) {
        if (!block(b, native)) {
            puts("{\"status\":\"MISMATCH\"}");
            return 1;
        }
    }
    uint64_t wall = clock_ns(CLOCK_MONOTONIC) - wall_start;
    uint64_t cpu = clock_ns(CLOCK_PROCESS_CPUTIME_ID) - cpu_start;
    getrusage(RUSAGE_SELF, &after);
    printf("{\"status\":\"MATCH\",\"mode\":\"%s\",\"blocks\":%lu,"
           "\"warmup\":%lu,\"wall_ns\":%" PRIu64 ",\"cpu_ns\":%" PRIu64 ","
           "\"rss_before_kib\":%" PRIu64 ",\"rss_after_kib\":%" PRIu64 ","
           "\"peak_rss_kib\":%" PRIu64 ",\"rusage_peak_rss_kib\":%ld,"
           "\"memory_source\":\"proc-status-VmRSS-VmHWM\","
           "\"cold_setup_first_block_ns\":%" PRIu64 ","
           "\"output_words_checked\":%lu,\"dma_words_checked\":%lu,"
           "\"semantic_gain_results\":%lu,\"exports\":%" PRIu64 ","
           "\"exported_bytes\":%" PRIu64
           ",\"dsp_instructions_per_block\":%" PRIu64 ","
           "\"dsp_route_instructions\":[%" PRIu64 ",%" PRIu64 ",%" PRIu64
           "]}\n",
           argv[1], count, warmup, wall, cpu, rss_before, memory_kib("VmRSS:"),
           memory_kib("VmHWM:"), after.ru_maxrss, first_ns, count * 192,
           count * 192, count * 192, b->exports - exports,
           b->exported_bytes - bytes, instructions, route_steps[0],
           route_steps[1], route_steps[2]);
    if (b->dsp) {
        dsp_destroy(b->dsp);
    }
    g_free(b);
    return 0;
}

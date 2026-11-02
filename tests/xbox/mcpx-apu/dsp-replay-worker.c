/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Private worker: invoke through dsp-replay.py for a wall-clock timeout. */
#include "qemu/osdep.h"
#include "dsp-capture-compare.h"
#include "hw/xbox/mcpx/apu/dsp/capture_wire.h"
#include "dsp-replay-core.h"
#include "dsp-replay-dual.h"
#include "dsp-replay-frame.h"
#include "dsp-replay-transcript.h"
#include "dsp-replay-library-identity.h"
#include "hw/xbox/mcpx/apu/apu_int.h"
#include "hw/boards.h"
#include "qemu/module.h"
#include "exec/cpu-common.h"
#include "system/address-spaces.h"
#include "system/cpus.h"
#include <float.h>

#define LIBRARY_JSON                                       \
    "\"library\":{\"status\":\"" DSP_REPLAY_LIBRARY_STATUS \
    "\",\"version\":\"" DSP_REPLAY_LIBRARY_VERSION         \
    "\",\"sha256\":\"" DSP_REPLAY_LIBRARY_SHA256           \
    "\",\"api_revision\":\"UNVERIFIED\"}"

static void print_status(const char *status)
{
    printf("{\"status\":\"%s\"," LIBRARY_JSON "}\n", status);
}

/* The only shared address space is private to this short-lived worker.
 * The comparator swaps two independent RAM images between engine steps. */
static int run_dual(char **argv)
{
    DspDualFixture fixture;
    if (!strcmp(argv[2], "scratch")) {
        fixture = DSP_DUAL_SCRATCH;
    } else if (!strcmp(argv[2], "fifo")) {
        fixture = DSP_DUAL_FIFO;
    } else if (!strcmp(argv[2], "ep-sink")) {
        fixture = DSP_DUAL_EP_SINK;
    } else {
        print_status("UNSUPPORTED");
        return 2;
    }
    const char *signals[] = { "impulse",   "silence",   "alternating",
                              "ramp",      "left",      "right",
                              "extremes",  "near-zero", "deterministic-noise",
                              "swept-sine" };
    unsigned signal;
    for (signal = 0; signal < G_N_ELEMENTS(signals); signal++) {
        if (!strcmp(argv[21], signals[signal])) {
            break;
        }
    }
    if (signal == G_N_ELEMENTS(signals)) {
        print_status("INVALID");
        return 2;
    }
    if (fixture == DSP_DUAL_EP_SINK && signal != DSP_SIGNAL_IMPULSE) {
        printf("{\"status\":\"INVALID\",\"reason\":\"unsupported fixture or "
               "signal\"," LIBRARY_JSON "}\n");
        return 2;
    }
    char *end;
    errno = 0;
    unsigned long frames = strtoul(argv[3], &end, 10);
    if (errno || end == argv[3] || *end || !frames || frames > 1024) {
        print_status("INVALID");
        return 2;
    }
    errno = 0;
    unsigned long value = strtoul(argv[4], &end, 10);
    if (errno || end == argv[4] || *end || value > 0xffffff ||
        (strcmp(argv[5], "0") && strcmp(argv[5], "1")) ||
        (strcmp(argv[6], "jit") && strcmp(argv[6], "interpreter")) ||
        (strcmp(argv[7], "jit") && strcmp(argv[7], "interpreter")) ||
        (strcmp(argv[8], "0") && strcmp(argv[8], "1")) ||
        (strcmp(argv[9], "0") && strcmp(argv[9], "1")) ||
        (strcmp(argv[10], "0") && strcmp(argv[10], "1")) ||
        (strcmp(argv[11], "0") && strcmp(argv[11], "1")) ||
        (strcmp(argv[12], "0") && strcmp(argv[12], "1")) ||
        (strcmp(argv[13], "0") && strcmp(argv[13], "1")) ||
        (strcmp(argv[14], "0") && strcmp(argv[14], "1")) ||
        (strcmp(argv[15], "0") && strcmp(argv[15], "1")) ||
        (strcmp(argv[16], "0") && strcmp(argv[16], "1")) ||
        (strcmp(argv[17], "0") && strcmp(argv[17], "1")) ||
        (strcmp(argv[18], "0") && strcmp(argv[18], "16") &&
         strcmp(argv[18], "24") && strcmp(argv[18], "32") &&
         strcmp(argv[18], "57")) ||
        (strcmp(argv[19], "0") && strcmp(argv[19], "1") &&
         strcmp(argv[19], "2") && strcmp(argv[19], "3") &&
         strcmp(argv[19], "4")) ||
        (strcmp(argv[20], "0") && strcmp(argv[20], "1") &&
         strcmp(argv[20], "2")) ||
        (fixture != DSP_DUAL_SCRATCH && argv[9][0] == '1')) {
        print_status("INVALID");
        return 2;
    }
    module_call_init(MODULE_INIT_QOM);
    Object *machine = object_new(TYPE_CONTAINER);
    object_property_add_child(object_get_root(), "machine", machine);
    object_property_add_child(machine, "unattached",
                              object_new(TYPE_CONTAINER));
    qemu_init_cpu_loop();
    bql_lock();
    cpu_exec_init_all();
    current_machine = g_new0(MachineState, 1);
    current_machine->dump_guest_core = true;
    MemoryRegion *ram = g_new0(MemoryRegion, 1);
    uint8_t *backing = g_malloc0(0x10000);
    memory_region_init_ram_ptr(ram, NULL, "dsp-dual-ram", 0x10000, backing);
    memory_region_add_subregion(get_system_memory(), 0, ram);
    MCPXAPUState *owner = g_new0(MCPXAPUState, 1);
    owner->ram = ram;
    owner->ram_ptr = backing;
    DspDualResult result = { 0 };
    DspReplayStatus status = dsp_dual_synthetic(
        owner, fixture, signal, frames, value, argv[5][0] == '1',
        argv[8][0] == '1', argv[9][0] == '1', argv[10][0] == '1',
        argv[11][0] == '1', argv[12][0] == '1', argv[13][0] == '1',
        argv[14][0] == '1', argv[15][0] == '1', argv[16][0] == '1',
        argv[17][0] == '1', strtoul(argv[18], NULL, 10),
        strtoul(argv[19], NULL, 10), strtoul(argv[20], NULL, 10),
        !strcmp(argv[6], "jit"), !strcmp(argv[7], "jit"), &result);
    if (status == DSP_REPLAY_STATE_MISMATCH) {
        if (!strcmp(result.field, "pcm") ||
            !strcmp(result.field, "fixture.pcm")) {
            printf("{\"status\":\"MISMATCH\",\"failure_frame\":%zu,"
                   "\"difference\":{"
                   "\"field\":\"%s\",\"sample\":%zu,\"channel\":%zu,"
                   "\"expected\":%d,\"actual\":%d}",
                   result.frame, result.field, result.index / 2,
                   result.index % 2, (int16_t)result.expected,
                   (int16_t)result.actual);
        } else if (!strcmp(result.field, "io_event")) {
            printf("{\"status\":\"MISMATCH\",\"failure_frame\":%zu,"
                   "\"difference\":{"
                   "\"field\":\"io_event\",\"index\":%zu,\"component\":\"%s\","
                   "\"sample\":%" PRIu64
                   ",\"byte_offset\":%zu,\"expected\":%" PRIu64
                   ",\"actual\":%" PRIu64 "}",
                   result.frame, result.index, result.component, result.sample,
                   result.byte_offset, result.expected, result.actual);
        } else if (result.component) {
            printf("{\"status\":\"MISMATCH\",\"failure_frame\":%zu,"
                   "\"difference\":{"
                   "\"field\":\"%s\",\"index\":%zu,\"component\":\"%s\","
                   "\"expected\":%" PRIu64 ",\"actual\":%" PRIu64 "}",
                   result.frame, result.field, result.index, result.component,
                   result.expected, result.actual);
        } else {
            printf("{\"status\":\"MISMATCH\",\"failure_frame\":%zu,"
                   "\"difference\":{"
                   "\"field\":\"%s\",\"index\":%zu,\"expected\":%" PRIu64
                   ",\"actual\":%" PRIu64 "}",
                   result.frame, result.field, result.index, result.expected,
                   result.actual);
        }
        if (result.post_subframe_pc_valid) {
            printf(",\"post_subframe_pc\":{\"reference_gp\":%u,\"reference_"
                   "ep\":%u,"
                   "\"candidate_gp\":%u,\"candidate_ep\":%u}",
                   result.post_subframe_pc[0], result.post_subframe_pc[1],
                   result.post_subframe_pc[2], result.post_subframe_pc[3]);
        }
        printf("," LIBRARY_JSON "}\n");
        return 1;
    }
    if (status != DSP_REPLAY_HALTED) {
        if (status == DSP_REPLAY_UNSUPPORTED && result.initial_state_mismatch) {
            printf("{\"status\":\"UNSUPPORTED\",\"reason\":\"INITIAL_STATE_"
                   "MISMATCH\","
                   "\"initial_difference\":{\"field\":\"%s\",\"index\":%zu,"
                   "\"expected\":%" PRIu64 ",\"actual\":%" PRIu64
                   "}," LIBRARY_JSON "}\n",
                   result.field, result.index, result.expected, result.actual);
            return 2;
        }
        printf("{\"status\":\"%s\",\"failure_frame\":%zu," LIBRARY_JSON "}\n",
               status == DSP_REPLAY_TIMEOUT     ? "TIMEOUT" :
               status == DSP_REPLAY_UNSUPPORTED ? "UNSUPPORTED" :
                                                  "INVALID",
               result.frame);
        return 2;
    }
    printf(
        "{\"status\":\"BASELINE_MATCH\",\"observations\":{"
        "\"io_events\":%zu,\"pcm_nonzero_comparisons\":%zu,"
        "\"dsp_memory_word_comparisons\":%zu,\"architectural_comparisons\":%zu,"
        "\"native_checkpoint_comparisons\":%zu,"
        "\"initial_subset_checked\":%s}," LIBRARY_JSON "}\n",
        result.io_events, result.pcm_nonzero_comparisons,
        result.dsp_memory_word_comparisons, result.architectural_comparisons,
        result.native_checkpoint_comparisons,
        result.initial_subset_checked ? "true" : "false");
    return 0;
}

#define MAX_SCRATCH (16 * 1024 * 1024)
#define MAX_MAPPED_RAM (128 * 1024 * 1024)
#define MAX_EVENTS 4096
#define MAX_EVENT_PAYLOAD (4 * 1024 * 1024)
#define MAX_TRANSCRIPT (MAX_EVENT_PAYLOAD + MAX_EVENTS * 24 + 8)

static uint8_t *load_blob(const char *path, size_t limit, size_t *length)
{
    FILE *file = fopen(path, "rb");
    struct stat statbuf;
    if (!file || fstat(fileno(file), &statbuf) || !S_ISREG(statbuf.st_mode) ||
        statbuf.st_size < 0 || (uint64_t)statbuf.st_size > limit) {
        if (file) {
            fclose(file);
        }
        return NULL;
    }
    size_t expected = statbuf.st_size;
    uint8_t *bytes = g_try_malloc(expected + 1);
    if (!bytes) {
        fclose(file);
        return NULL;
    }
    *length = fread(bytes, 1, expected + 1, file);
    bool ok = *length == expected && !ferror(file);
    fclose(file);
    if (!ok) {
        g_free(bytes);
        return NULL;
    }
    return bytes;
}

static bool load_state(const char *path, DspJitCapture *state)
{
    size_t size = dsp_jit_wire_size();
    uint8_t *bytes = g_try_malloc(size);
    FILE *file = fopen(path, "rb");
    bool ok = false;
    if (bytes && file && fread(bytes, 1, size, file) == size &&
        fgetc(file) == EOF && !ferror(file)) {
        ok = dsp_jit_wire_decode(bytes, size, state);
    }
    if (file) {
        fclose(file);
    }
    g_free(bytes);
    return ok;
}

static uint32_t frame_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
           (uint32_t)p[3] << 24;
}

static int16_t frame_s16(const uint8_t *p)
{
    unsigned value = (unsigned)p[0] | (unsigned)p[1] << 8;
    return value < 0x8000 ? (int16_t)value : (int16_t)((int)value - 0x10000);
}

typedef struct FrameCheckContext {
    uint8_t *wire[2];
    DspJitCapture *expected;
    DspCaptureDifference difference;
    unsigned unit;
    const uint8_t *pcm;
    bool pcm_mismatch;
    size_t pcm_index;
    int pcm_expected, pcm_actual;
} FrameCheckContext;

static bool check_frame_state(void *opaque, size_t frame,
                              const DspJitCapture *gp, const DspJitCapture *ep,
                              const int16_t pcm[256][2])
{
    FrameCheckContext *ctx = opaque;
    const DspJitCapture *actual[2] = { gp, ep };
    size_t size = dsp_jit_wire_size();
    for (unsigned i = 0; i < 2; i++) {
        /* Inputs were fully decoded and validated before the first frame. */
        if (!dsp_jit_wire_decode(ctx->wire[i] + frame * size, size,
                                 ctx->expected) ||
            !dsp_capture_equal(ctx->expected, actual[i], &ctx->difference)) {
            ctx->unit = i;
            return false;
        }
    }
    for (size_t i = 0; i < 512; i++) {
        int expected = frame_s16(ctx->pcm + frame * 1024 + i * 2);
        int actual_pcm = pcm[i / 2][i % 2];
        if (expected != actual_pcm) {
            ctx->pcm_mismatch = true;
            ctx->pcm_index = i;
            ctx->pcm_expected = expected;
            ctx->pcm_actual = actual_pcm;
            return false;
        }
    }
    return true;
}

/* Invoked only in a fresh worker process: production callbacks use the global
 * system address space and a private APU with no live guest or device threads.
 */
static int run_mapped_frame(char **argv, bool sequence, bool trace,
                            bool checkpoints)
{
    unsigned long count = 1;
    if (sequence) {
        char *end;
        errno = 0;
        count = strtoul(argv[14], &end, 10);
        if (errno || end == argv[14] || *end || !count || count > 1024) {
            print_status("INVALID");
            return 2;
        }
    }
    if (sizeof(float) != 4 || FLT_RADIX != 2 || FLT_MANT_DIG != 24 ||
        FLT_MAX_EXP != 128) {
        print_status("UNSUPPORTED");
        return 2;
    }
    DspJitCapture *states[4] = { 0 };
    uint8_t *blobs[8] = { 0 }, *result_ram = NULL;
    size_t sizes[8] = { 0 };
    const size_t limits[8] = { count * 4096,   1024,    count * 1024,
                               0x80000,        0x80000, MAX_MAPPED_RAM,
                               MAX_MAPPED_RAM, 16 };
    DspMappedFrame *initial = NULL, *result = NULL;
    DspIoEvent *events = NULL;
    uint8_t *event_bytes = NULL, *event_payload = NULL;
    DspIoLog io;
    FrameCheckContext check = { 0 };
    float (*mixbins)[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME] = NULL;
    int16_t (*sequence_pcm)[256][2] = NULL;
    MCPXAPUState *apu = NULL;
    int exit_code = 2;
    for (size_t i = 0; i < 4; i++) {
        states[i] = g_try_new0(DspJitCapture, 1);
        if (!states[i] || !load_state(argv[2 + i], states[i])) {
            print_status("INCOMPLETE");
            goto out;
        }
    }
    for (size_t i = 0; i < 8; i++) {
        blobs[i] = load_blob(argv[6 + i], limits[i], &sizes[i]);
        if (!blobs[i]) {
            print_status("INCOMPLETE");
            goto out;
        }
    }
    if (sizes[0] != limits[0] || sizes[1] != 1024 || sizes[2] != limits[2] ||
        sizes[3] != 0x80000 || sizes[4] != 0x80000 || !sizes[5] ||
        sizes[5] != sizes[6] || sizes[7] != 16 ||
        (sizes[5] % TARGET_PAGE_SIZE) || memcmp(blobs[7], "XMF1", 4) ||
        blobs[7][12] > MCPX_APU_DEBUG_MON_GP_OR_EP || blobs[7][13] > 7 ||
        blobs[7][14] > 1 || blobs[7][15] > 1) {
        print_status("INCOMPLETE");
        goto out;
    }
    initial = g_try_new0(DspMappedFrame, 1);
    result = g_try_new(DspMappedFrame, 1);
    result_ram = g_try_malloc(sizes[5]);
    mixbins = g_try_new(typeof(*mixbins), count);
    sequence_pcm = sequence ? g_try_new(typeof(*sequence_pcm), count) : NULL;
    if (!initial || !result || !result_ram || !mixbins ||
        (sequence && !sequence_pcm)) {
        print_status("INCOMPLETE");
        goto out;
    }
    if (trace) {
        size_t event_size;
        event_bytes = load_blob(argv[15], MAX_TRANSCRIPT, &event_size);
        event_payload = g_try_malloc(MAX_EVENT_PAYLOAD);
        events = g_try_new(DspIoEvent, MAX_EVENTS);
        if (!event_bytes || !event_payload || !events ||
            !dsp_io_decode(event_bytes, event_size, &io, events, MAX_EVENTS,
                           event_payload, MAX_EVENT_PAYLOAD)) {
            print_status("INCOMPLETE");
            goto out;
        }
    }
    if (checkpoints) {
        check.pcm = blobs[2];
        size_t size = dsp_jit_wire_size();
        check.expected = g_try_new(DspJitCapture, 1);
        for (unsigned i = 0; i < 2; i++) {
            size_t length;
            check.wire[i] = load_blob(argv[16 + i], count * size, &length);
            if (!check.expected || !check.wire[i] || length != count * size) {
                print_status("INCOMPLETE");
                goto out;
            }
            for (size_t frame = 0; frame < count; frame++) {
                if (!dsp_jit_wire_decode(check.wire[i] + frame * size, size,
                                         check.expected) ||
                    check.expected->is_gp != (i == 0)) {
                    print_status("INCOMPLETE");
                    goto out;
                }
            }
        }
    }
    initial->dsp.gp = *states[0];
    initial->dsp.ep = *states[1];
    for (size_t i = 0; i < G_N_ELEMENTS(initial->apu_regs); i++) {
        initial->apu_regs[i] = frame_u32(blobs[3] + i * 4);
    }
    initial->gp_reset = frame_u32(blobs[7] + 4);
    initial->ep_reset = frame_u32(blobs[7] + 8);
    initial->monitor_point = blobs[7][12];
    initial->phase = blobs[7][13];
    initial->gp_realtime = blobs[7][14];
    initial->ep_realtime = blobs[7][15];
    for (size_t frame = 0; frame < count; frame++) {
        for (size_t i = 0; i < NUM_MIXBINS; i++) {
            for (size_t j = 0; j < NUM_SAMPLES_PER_FRAME; j++) {
                uint32_t bits = frame_u32(blobs[0] + frame * 4096 +
                                          (i * NUM_SAMPLES_PER_FRAME + j) * 4);
                memcpy(&mixbins[frame][i][j], &bits, sizeof(bits));
            }
        }
    }
    for (size_t i = 0; i < 512; i++) {
        initial->dsp.pcm[i / 2][i % 2] = frame_s16(blobs[1] + 2 * i);
    }

    module_call_init(MODULE_INIT_QOM);
    Object *machine = object_new(TYPE_CONTAINER);
    object_property_add_child(object_get_root(), "machine", machine);
    object_property_add_child(machine, "unattached",
                              object_new(TYPE_CONTAINER));
    qemu_init_cpu_loop();
    bql_lock();
    cpu_exec_init_all();
    current_machine = g_new0(MachineState, 1);
    current_machine->dump_guest_core = true;
    MemoryRegion *ram = g_new0(MemoryRegion, 1);
    uint8_t *backing = g_malloc0(sizes[5]);
    memory_region_init_ram_ptr(ram, NULL, "dsp-replay-ram", sizes[5], backing);
    memory_region_add_subregion(get_system_memory(), 0, ram);
    apu = g_new0(MCPXAPUState, 1);
    apu->ram = ram;
    apu->ram_ptr = memory_region_get_ram_ptr(ram);
    g_config.audio.use_dsp = true;
    g_config.audio.use_dsp_jit = true;
    mcpx_apu_dsp_init(apu);
    size_t failed_frame = SIZE_MAX;
    DspReplayStatus status =
        checkpoints ? dsp_replay_mapped_trace_checked(
                          apu, initial, count, mixbins, blobs[5], sizes[5],
                          result, result_ram, sequence_pcm, &failed_frame, &io,
                          check_frame_state, &check) :
        trace ? dsp_replay_mapped_trace(apu, initial, count, mixbins, blobs[5],
                                        sizes[5], result, result_ram,
                                        sequence_pcm, &failed_frame, &io) :
        sequence ? dsp_replay_mapped_sequence(
                       apu, initial, count, mixbins, blobs[5], sizes[5], result,
                       result_ram, sequence_pcm, &failed_frame) :
                   dsp_replay_mapped_frame(apu, initial, mixbins[0], blobs[5],
                                           sizes[5], result, result_ram);
    if (status != DSP_REPLAY_HALTED) {
        if (status == DSP_REPLAY_STATE_MISMATCH) {
            if (check.pcm_mismatch) {
                printf("{\"status\":\"MISMATCH\",\"failure_frame\":%zu,"
                       "\"difference\":{"
                       "\"field\":\"pcm\",\"frame\":%zu,\"sample\":%zu,"
                       "\"channel\":%zu,"
                       "\"expected\":%d,\"actual\":%d}," LIBRARY_JSON "}\n",
                       failed_frame, failed_frame, check.pcm_index / 2,
                       check.pcm_index % 2, check.pcm_expected,
                       check.pcm_actual);
                exit_code = 1;
                goto out;
            }
            printf("{\"status\":\"MISMATCH\",\"failure_frame\":%zu,"
                   "\"difference\":{"
                   "\"field\":\"%s.%s\",\"index\":%zu,\"expected\":%" PRIu64
                   ",\"actual\":%" PRIu64 "}," LIBRARY_JSON "}\n",
                   failed_frame, check.unit ? "ep" : "gp",
                   check.difference.field, check.difference.index,
                   check.difference.expected, check.difference.actual);
            exit_code = 1;
            goto out;
        }
        if (status == DSP_REPLAY_IO_MISMATCH) {
            if (io.missing_context || !io.valid) {
                print_status("INCOMPLETE");
            } else if (failed_frame < count) {
                printf("{\"status\":\"MISMATCH\",\"failure_frame\":%zu,"
                       "\"difference\":{"
                       "\"field\":\"io_event\",\"index\":%zu}," LIBRARY_JSON
                       "}\n",
                       failed_frame, io.cursor);
                exit_code = 1;
            } else {
                printf("{\"status\":\"MISMATCH\",\"difference\":{"
                       "\"field\":\"io_event\",\"index\":%zu}," LIBRARY_JSON
                       "}\n",
                       io.cursor);
                exit_code = 1;
            }
            goto out;
        }
        const char *label =
            status == DSP_REPLAY_TIMEOUT ? "TIMEOUT" : "INVALID";
        if (sequence && failed_frame < count) {
            printf("{\"status\":\"%s\",\"failure_frame\":%zu," LIBRARY_JSON
                   "}\n",
                   label, failed_frame);
        } else {
            print_status(label);
        }
        goto out;
    }
    DspCaptureDifference difference;
    for (size_t i = 0; i < 2; i++) {
        const DspJitCapture *actual = i ? &result->dsp.ep : &result->dsp.gp;
        if (!dsp_capture_equal(states[i + 2], actual, &difference)) {
            printf("{\"status\":\"MISMATCH\",\"difference\":{"
                   "\"field\":\"%s.%s\",\"index\":%zu,"
                   "\"expected\":%" PRIu64 ",\"actual\":%" PRIu64
                   "}," LIBRARY_JSON "}\n",
                   i ? "ep" : "gp", difference.field, difference.index,
                   difference.expected, difference.actual);
            exit_code = 1;
            goto out;
        }
    }
    for (size_t i = 0; i < count * 512; i++) {
        int16_t expected = frame_s16(blobs[2] + i * 2);
        int16_t actual = sequence ?
                             sequence_pcm[i / 512][(i / 2) % 256][i % 2] :
                             result->dsp.pcm[i / 2][i % 2];
        if (expected != actual) {
            printf("{\"status\":\"MISMATCH\",\"difference\":{"
                   "\"field\":\"pcm\",\"frame\":%zu,\"sample\":%zu,\"channel\":"
                   "%zu,"
                   "\"expected\":%d,\"actual\":%d}," LIBRARY_JSON "}\n",
                   i / 512, (i / 2) % 256, i % 2, expected, actual);
            exit_code = 1;
            goto out;
        }
    }
    for (size_t i = 0; i < G_N_ELEMENTS(result->apu_regs); i++) {
        uint32_t expected = frame_u32(blobs[4] + i * 4);
        if (expected != result->apu_regs[i]) {
            printf("{\"status\":\"MISMATCH\",\"difference\":{"
                   "\"field\":\"apu_registers\",\"index\":%zu,"
                   "\"expected\":%u,\"actual\":%u}," LIBRARY_JSON "}\n",
                   i, expected, result->apu_regs[i]);
            exit_code = 1;
            goto out;
        }
    }
    for (size_t i = 0; i < sizes[5]; i++) {
        if (result_ram[i] != blobs[6][i]) {
            printf("{\"status\":\"MISMATCH\",\"difference\":{"
                   "\"field\":\"ram\",\"index\":%zu,"
                   "\"expected\":%u,\"actual\":%u}," LIBRARY_JSON "}\n",
                   i, blobs[6][i], result_ram[i]);
            exit_code = 1;
            goto out;
        }
    }
    print_status("BASELINE_MATCH");
    exit_code = 0;
out:
    if (apu) {
        dsp_destroy(apu->gp.dsp);
        dsp_destroy(apu->ep.dsp);
    }
    for (size_t i = 0; i < 4; i++) {
        g_free(states[i]);
    }
    for (size_t i = 0; i < 8; i++) {
        g_free(blobs[i]);
    }
    g_free(initial);
    g_free(result);
    g_free(result_ram);
    g_free(events);
    g_free(event_bytes);
    g_free(check.wire[0]);
    g_free(check.wire[1]);
    g_free(check.expected);
    g_free(event_payload);
    g_free(mixbins);
    g_free(sequence_pcm);
    return exit_code;
}

static int run_frame(char **argv, bool sequence)
{
    char *end;
    errno = 0;
    unsigned long phase = strtoul(argv[14], &end, 10);
    if (errno || end == argv[14] || *end || phase > 7) {
        print_status("INVALID");
        return 2;
    }
    unsigned long count = 1;
    if (sequence) {
        errno = 0;
        count = strtoul(argv[15], &end, 10);
        if (errno || end == argv[15] || *end || !count || count > 1024) {
            print_status("INVALID");
            return 2;
        }
    }
    if (sizeof(float) != 4 || FLT_RADIX != 2 || FLT_MANT_DIG != 24 ||
        FLT_MAX_EXP != 128) {
        print_status("UNSUPPORTED");
        return 2;
    }
    DspJitCapture *states[4] = { 0 };
    uint8_t *blobs[8] = { 0 }, *gp_output = NULL, *ep_output = NULL;
    const size_t limits[8] = { count * 4096,
                               1024,
                               sequence ? count * 128 : 1024,
                               MAX_SCRATCH,
                               MAX_SCRATCH,
                               MAX_SCRATCH,
                               MAX_SCRATCH,
                               MAX_TRANSCRIPT + (sequence ? 12 * count : 0) };
    size_t sizes[8] = { 0 };
    DspIoEvent *events = NULL;
    uint8_t *payload = NULL;
    DspFrameResult *result = NULL;
    float (*mixbins)[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME] = NULL;
    int16_t (*sequence_pcm)[NUM_SAMPLES_PER_FRAME][2] = NULL;
    DspIoLog *logs = NULL;
    DspIoEvent **event_arrays = NULL;
    uint8_t **payload_arrays = NULL;
    int exit_code = 2;
    for (size_t i = 0; i < 4; i++) {
        states[i] = g_try_new0(DspJitCapture, 1);
        if (!states[i] || !load_state(argv[2 + i], states[i])) {
            print_status("INCOMPLETE");
            goto out;
        }
    }
    for (size_t i = 0; i < 8; i++) {
        blobs[i] = load_blob(argv[6 + i], limits[i], &sizes[i]);
        if (!blobs[i]) {
            print_status("INCOMPLETE");
            goto out;
        }
    }
    if (sizes[0] != count * 4096 || sizes[1] != 1024 || sizes[2] != limits[2] ||
        sizes[3] != sizes[5] || sizes[4] != sizes[6]) {
        print_status("INCOMPLETE");
        goto out;
    }
    if (!sequence) {
        events = g_try_new(DspIoEvent, MAX_EVENTS);
        payload = g_try_malloc(MAX_EVENT_PAYLOAD);
    }
    gp_output = g_try_malloc(MAX(sizes[3], 1));
    ep_output = g_try_malloc(MAX(sizes[4], 1));
    result = g_try_new(DspFrameResult, 1);
    mixbins = g_try_new(typeof(*mixbins), count);
    DspIoLog io;
    if (!gp_output || !ep_output || !result || !mixbins) {
        print_status("INCOMPLETE");
        goto out;
    }
    if (!sequence) {
        if (!events || !payload ||
            !dsp_io_decode(blobs[7], sizes[7], &io, events, MAX_EVENTS, payload,
                           MAX_EVENT_PAYLOAD)) {
            print_status("INCOMPLETE");
            goto out;
        }
    } else {
        logs = g_try_new0(DspIoLog, count);
        event_arrays = g_try_new0(DspIoEvent *, count);
        payload_arrays = g_try_new0(uint8_t *, count);
        sequence_pcm = g_try_new(typeof(*sequence_pcm), count);
        if (!logs || !event_arrays || !payload_arrays || !sequence_pcm ||
            sizes[7] < 8 || memcmp(blobs[7], "XIS1", 4) ||
            frame_u32(blobs[7] + 4) != count) {
            print_status("INCOMPLETE");
            goto out;
        }
        size_t offset = 8, total_events = 0;
        for (size_t i = 0; i < count; i++) {
            if (sizes[7] - offset < 4) {
                print_status("INCOMPLETE");
                goto out;
            }
            size_t length = frame_u32(blobs[7] + offset);
            offset += 4;
            if (length < 8 || length > sizes[7] - offset) {
                print_status("INCOMPLETE");
                goto out;
            }
            size_t n = frame_u32(blobs[7] + offset + 4);
            if (n > MAX_EVENTS - total_events) {
                print_status("INCOMPLETE");
                goto out;
            }
            total_events += n;
            event_arrays[i] = g_try_new(DspIoEvent, MAX(n, 1));
            payload_arrays[i] = g_try_malloc(length);
            if (!event_arrays[i] || !payload_arrays[i] ||
                !dsp_io_decode(blobs[7] + offset, length, &logs[i],
                               event_arrays[i], n, payload_arrays[i], length)) {
                print_status("INCOMPLETE");
                goto out;
            }
            offset += length;
        }
        if (offset != sizes[7]) {
            print_status("INCOMPLETE");
            goto out;
        }
    }
    int16_t prior_pcm[256][2];
    for (size_t frame = 0; frame < count; frame++) {
        for (size_t i = 0; i < NUM_MIXBINS; i++) {
            for (size_t j = 0; j < NUM_SAMPLES_PER_FRAME; j++) {
                uint32_t bits = frame_u32(blobs[0] + frame * 4096 +
                                          (i * NUM_SAMPLES_PER_FRAME + j) * 4);
                memcpy(&mixbins[frame][i][j], &bits, sizeof(bits));
            }
        }
    }
    for (size_t i = 0; i < 256; i++) {
        for (size_t channel = 0; channel < 2; channel++) {
            prior_pcm[i][channel] = frame_s16(blobs[1] + (i * 2 + channel) * 2);
        }
    }
    DspFrameMemory memory = { .gp = blobs[3],
                              .ep = blobs[4],
                              .gp_size = sizes[3],
                              .ep_size = sizes[4],
                              .gp_result = gp_output,
                              .ep_result = ep_output };
    size_t failed_frame = SIZE_MAX;
    DspReplayStatus status =
        sequence ?
            dsp_replay_gp_ep_sequence(states[0], states[1], phase, count,
                                      mixbins, prior_pcm, &memory, logs, result,
                                      sequence_pcm, &failed_frame) :
            dsp_replay_gp_ep_frame(states[0], states[1], phase, mixbins[0],
                                   prior_pcm, &memory, &io, result);
    if (status != DSP_REPLAY_HALTED) {
        DspIoLog *failed_io =
            sequence && failed_frame < count ? &logs[failed_frame] : &io;
        if (status == DSP_REPLAY_IO_MISMATCH && !failed_io->missing_context) {
            printf("{\"status\":\"MISMATCH\",\"difference\":{"
                   "\"field\":\"io_event\",\"index\":%zu,\"frame\":%zu}"
                   "," LIBRARY_JSON "}\n",
                   failed_io->cursor, sequence ? failed_frame : 0);
            exit_code = 1;
        } else {
            print_status(status == DSP_REPLAY_IO_MISMATCH ? "INCOMPLETE" :
                         status == DSP_REPLAY_TIMEOUT     ? "TIMEOUT" :
                         status == DSP_REPLAY_UNSUPPORTED ? "UNSUPPORTED" :
                                                            "INVALID");
        }
        goto out;
    }
    DspCaptureDifference difference;
    for (size_t i = 0; i < 2; i++) {
        const DspJitCapture *actual = i ? &result->ep : &result->gp;
        if (!dsp_capture_equal(states[i + 2], actual, &difference)) {
            printf("{\"status\":\"MISMATCH\",\"difference\":{"
                   "\"field\":\"%s.%s\",\"index\":%zu,"
                   "\"expected\":%" PRIu64 ",\"actual\":%" PRIu64
                   "}," LIBRARY_JSON "}\n",
                   i ? "ep" : "gp", difference.field, difference.index,
                   difference.expected, difference.actual);
            exit_code = 1;
            goto out;
        }
    }
    size_t pcm_values = sequence ? count * NUM_SAMPLES_PER_FRAME * 2 : 512;
    for (size_t i = 0; i < pcm_values; i++) {
        int16_t expected = frame_s16(blobs[2] + i * 2);
        int16_t actual = sequence ? sequence_pcm[i / 64][(i / 2) % 32][i % 2] :
                                    result->pcm[i / 2][i % 2];
        if (expected != actual) {
            printf("{\"status\":\"MISMATCH\",\"difference\":{"
                   "\"field\":\"pcm\",\"index\":%zu,"
                   "\"sample\":%zu,\"channel\":%zu,"
                   "\"expected\":%d,\"actual\":%d}," LIBRARY_JSON "}\n",
                   i, i / 2, i % 2, expected, actual);
            exit_code = 1;
            goto out;
        }
    }
    for (size_t unit = 0; unit < 2; unit++) {
        const uint8_t *actual = unit ? ep_output : gp_output;
        const uint8_t *expected = blobs[unit + 5];
        size_t size = sizes[unit + 3];
        for (size_t i = 0; i < size; i++) {
            if (actual[i] != expected[i]) {
                printf("{\"status\":\"MISMATCH\",\"difference\":{"
                       "\"field\":\"%s_scratch\",\"index\":%zu,"
                       "\"expected\":%u,\"actual\":%u}," LIBRARY_JSON "}\n",
                       unit ? "ep" : "gp", i, expected[i], actual[i]);
                exit_code = 1;
                goto out;
            }
        }
    }
    print_status("BASELINE_MATCH");
    exit_code = 0;
out:
    for (size_t i = 0; i < 4; i++) {
        g_free(states[i]);
    }
    for (size_t i = 0; i < 8; i++) {
        g_free(blobs[i]);
    }
    g_free(events);
    g_free(payload);
    g_free(gp_output);
    g_free(ep_output);
    g_free(result);
    g_free(mixbins);
    g_free(sequence_pcm);
    if (event_arrays) {
        for (size_t i = 0; i < count; i++) {
            g_free(event_arrays[i]);
        }
    }
    if (payload_arrays) {
        for (size_t i = 0; i < count; i++) {
            g_free(payload_arrays[i]);
        }
    }
    g_free(event_arrays);
    g_free(payload_arrays);
    g_free(logs);
    return exit_code;
}

int __wrap_main(int argc, char **argv);

int __wrap_main(int argc, char **argv)
{
    if (argc == 22 && !strcmp(argv[1], "--engines")) {
        return run_dual(argv);
    }
    if (argc == 14 && !strcmp(argv[1], "--mapped-frame")) {
        return run_mapped_frame(argv, false, false, false);
    }
    if (argc == 15 && !strcmp(argv[1], "--mapped-sequence")) {
        return run_mapped_frame(argv, true, false, false);
    }
    if (argc == 16 && !strcmp(argv[1], "--mapped-trace")) {
        return run_mapped_frame(argv, true, true, false);
    }
    if (argc == 18 && !strcmp(argv[1], "--mapped-trace-checkpoints")) {
        return run_mapped_frame(argv, true, true, true);
    }
    if (argc == 15 && !strcmp(argv[1], "--frame")) {
        return run_frame(argv, false);
    }
    if (argc == 16 && !strcmp(argv[1], "--sequence")) {
        return run_frame(argv, true);
    }
    if (argc != 4 && argc != 7) {
        print_status("INVALID");
        return 2;
    }
    char *end;
    errno = 0;
    unsigned long steps = strtoul(argv[3], &end, 10);
    if (errno || end == argv[3] || *end || steps == 0 || steps > 1000000) {
        print_status("INVALID");
        return 2;
    }
    DspJitCapture *initial = g_new0(DspJitCapture, 1);
    DspJitCapture *expected = g_new0(DspJitCapture, 1);
    DspJitCapture *actual = g_new0(DspJitCapture, 1);
    uint8_t *memory = NULL, *expected_memory = NULL, *actual_memory = NULL;
    uint8_t *encoded_events = NULL, *payload = NULL;
    DspIoEvent *events = NULL;
    size_t memory_size = 0, expected_size = 0, encoded_size = 0;
    int exit_code = 2;
    if (!load_state(argv[1], initial) || !load_state(argv[2], expected)) {
        print_status("INCOMPLETE");
        goto out;
    }
    DspIoLog io;
    if (argc == 7) {
        memory = load_blob(argv[4], MAX_SCRATCH, &memory_size);
        expected_memory = load_blob(argv[5], MAX_SCRATCH, &expected_size);
        encoded_events = load_blob(argv[6], MAX_TRANSCRIPT, &encoded_size);
        events = g_try_new(DspIoEvent, MAX_EVENTS);
        payload = g_try_malloc(MAX_EVENT_PAYLOAD);
        actual_memory = g_try_malloc(MAX(memory_size, 1));
        if (!memory || !expected_memory || !encoded_events || !events ||
            !payload || !actual_memory || memory_size != expected_size ||
            !dsp_io_decode(encoded_events, encoded_size, &io, events,
                           MAX_EVENTS, payload, MAX_EVENT_PAYLOAD)) {
            print_status("INCOMPLETE");
            goto out;
        }
    } else {
        dsp_io_record_init(&io, NULL, 0, NULL, 0);
        dsp_io_replay_init(&io);
    }
    DspReplayStatus status = dsp_replay_execute(
        initial, memory, memory_size, &io, steps, actual, actual_memory);
    if (status == DSP_REPLAY_TIMEOUT) {
        print_status("TIMEOUT");
    } else if (status == DSP_REPLAY_UNSUPPORTED) {
        print_status("UNSUPPORTED");
    } else if (status == DSP_REPLAY_IO_MISMATCH) {
        if (io.missing_context) {
            print_status("INCOMPLETE");
        } else {
            printf("{\"status\":\"MISMATCH\",\"difference\":{"
                   "\"field\":\"io_event\",\"index\":%zu}," LIBRARY_JSON "}\n",
                   io.cursor);
            exit_code = 1;
        }
    } else if (status != DSP_REPLAY_HALTED) {
        print_status("INVALID");
    } else {
        DspCaptureDifference difference;
        if (!dsp_capture_equal(expected, actual, &difference)) {
            printf("{\"status\":\"MISMATCH\",\"difference\":{"
                   "\"field\":\"%s\",\"index\":%zu,"
                   "\"expected\":%" PRIu64 ",\"actual\":%" PRIu64
                   "}," LIBRARY_JSON "}\n",
                   difference.field, difference.index, difference.expected,
                   difference.actual);
            exit_code = 1;
        } else if (memory_size &&
                   memcmp(expected_memory, actual_memory, memory_size)) {
            size_t index = 0;
            while (expected_memory[index] == actual_memory[index]) {
                index++;
            }
            printf("{\"status\":\"MISMATCH\",\"difference\":{"
                   "\"field\":\"scratch_memory\",\"index\":%zu,"
                   "\"expected\":%u,\"actual\":%u}," LIBRARY_JSON "}\n",
                   index, expected_memory[index], actual_memory[index]);
            exit_code = 1;
        } else {
            print_status("BASELINE_MATCH");
            exit_code = 0;
        }
    }
out:
    g_free(memory);
    g_free(expected_memory);
    g_free(actual_memory);
    g_free(encoded_events);
    g_free(payload);
    g_free(events);
    g_free(initial);
    g_free(expected);
    g_free(actual);
    return exit_code;
}

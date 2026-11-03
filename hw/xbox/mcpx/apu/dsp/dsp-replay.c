/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "io/channel-buffer.h"
#include "io/channel-file.h"
#include "migration/qemu-file.h"
#include "migration/vmstate.h"
#include "dsp_internal.h"
#include "dsp_dma_regs.h"
#include "dsp-replay.h"
#include "ep-mix-hle.h"
#include "dsp_internal.h"
#include "dsp_dma_regs.h"
#include "gp-feedback-project.h"
#include "gp-mix2-project.h"
#include "gp-add-project.h"
#include "gp-matrix-project.h"
#include "gp-meter-project.h"
#include "gp-mac-project.h"
#include "gp-delay-project.h"
#include "gp-decimate-project.h"
#include "gp-gain-project.h"
#include "gp-filter-hle.h"
#include "ep-mix-identity.h"
#include "ep-native-input.h"
#include "ep-native-project.h"

#define REPLAY_MAGIC_GP UINT64_C(0x58475046524d3031) /* XGPFRM01 */
#define REPLAY_MAGIC_EP UINT64_C(0x58455046524d3031) /* XEPFRM01 */
#define REPLAY_VERSION 1
#define REPLAY_PROVIDER 0x103 /* dsp56300 0.1.3 */
#define REPLAY_END 0x4750454e
#define REPLAY_MAX_FILE (4 * 1024 * 1024)
#define REPLAY_MAX_DELAY_MS 86400000

/*
 * Flat fields avoid nested subsection lookahead into another native field.
 * Each snapshot is followed by a zero byte before the next format element.
 */
static const VMStateDescription snapshot_vmstate = {
    .name = "xemu-gp-replay-native",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields =
        (const VMStateField[]){
            VMSTATE_UINT32(jit.cpu.pc, DSPReplaySnapshot),
            VMSTATE_UINT32(jit.cpu.pc_advance, DSPReplaySnapshot),
            VMSTATE_BOOL(jit.cpu.pc_on_rep, DSPReplaySnapshot),
            VMSTATE_UINT32(jit.cpu.cycle_count, DSPReplaySnapshot),
            VMSTATE_INT32(jit.cpu.cycle_budget, DSPReplaySnapshot),
            VMSTATE_UINT64_ARRAY(jit.cpu.interrupts.pending_bits,
                                 DSPReplaySnapshot, 2),
            VMSTATE_UINT8(jit.cpu.interrupts.pipeline_stage, DSPReplaySnapshot),
            VMSTATE_UINT32(jit.cpu.interrupts.vector_addr, DSPReplaySnapshot),
            VMSTATE_UINT32(jit.cpu.interrupts.saved_pc, DSPReplaySnapshot),
            VMSTATE_UINT8(jit.cpu.interrupts.state, DSPReplaySnapshot),
            VMSTATE_ARRAY(jit.cpu.interrupts.ipl, DSPReplaySnapshot, 128, 0,
                          vmstate_info_int8, int8_t),
            VMSTATE_UINT8(jit.cpu.interrupts.ipl_to_raise, DSPReplaySnapshot),
            VMSTATE_BOOL(jit.cpu.loop_rep, DSPReplaySnapshot),
            VMSTATE_BOOL(jit.cpu.halt_requested, DSPReplaySnapshot),
            VMSTATE_UINT8(jit.cpu.power_state, DSPReplaySnapshot),
            VMSTATE_UINT32_ARRAY(jit.cpu.registers, DSPReplaySnapshot, 64),
            VMSTATE_UINT32_2DARRAY(jit.cpu.stack, DSPReplaySnapshot, 2, 16),
            VMSTATE_UINT32_ARRAY(jit.xram, DSPReplaySnapshot, DSP_XRAM_SIZE),
            VMSTATE_UINT32_ARRAY(jit.yram, DSPReplaySnapshot, DSP_YRAM_SIZE),
            VMSTATE_UINT32_ARRAY(jit.pram, DSPReplaySnapshot, DSP_PRAM_SIZE),
            VMSTATE_UINT32(dma.configuration, DSPReplaySnapshot),
            VMSTATE_UINT32(dma.control, DSPReplaySnapshot),
            VMSTATE_UINT32(dma.start_block, DSPReplaySnapshot),
            VMSTATE_UINT32(dma.next_block, DSPReplaySnapshot),
            VMSTATE_UINT32(dma.dma_read_count, DSPReplaySnapshot),
            VMSTATE_BOOL(dma.error, DSPReplaySnapshot),
            VMSTATE_BOOL(dma.eol, DSPReplaySnapshot),
            VMSTATE_UINT32(interrupts, DSPReplaySnapshot),
            VMSTATE_INT32(save_cycles, DSPReplaySnapshot),
            VMSTATE_END_OF_LIST() },
};

static void free_io(gpointer opaque)
{
    DSPReplayIO *io = opaque;
    g_bytes_unref(io->bytes);
    g_free(io);
}

DSPReplayCase *dsp_replay_case_new(void)
{
    DSPReplayCase *data = g_new0(DSPReplayCase, 1);
    data->is_gp = true;
    data->io = g_ptr_array_new_with_free_func(free_io);
    return data;
}

void dsp_replay_case_free(DSPReplayCase *data)
{
    if (data) {
        g_ptr_array_unref(data->io);
        g_free(data);
    }
}

static bool valid_case(DSPReplayCase *data, Error **errp)
{
    size_t bytes = 0;
    if (!data || !data->io || !data->calls ||
        data->calls > DSP_REPLAY_MAX_CALLS ||
        data->io->len > DSP_REPLAY_MAX_IO) {
        error_setg(errp, "Invalid DSP replay case bounds");
        return false;
    }
    for (unsigned i = 0; i < data->io->len; i++) {
        DSPReplayIO *io = g_ptr_array_index(data->io, i);
        size_t len = g_bytes_get_size(io->bytes);
        if (len > DSP_REPLAY_MAX_BYTES - bytes) {
            error_setg(errp, "DSP replay payload exceeds limit");
            return false;
        }
        bytes += len;
    }
    return true;
}

static GBytes *encode_case(DSPReplayCase *data, Error **errp)
{
    if (!valid_case(data, errp)) {
        return NULL;
    }
    QIOChannelBuffer *buffer = qio_channel_buffer_new(128 * 1024);
    g_autoptr(QEMUFile) f = qemu_file_new_output(QIO_CHANNEL(buffer));
    qemu_put_be64(f, data->is_gp ? REPLAY_MAGIC_GP : REPLAY_MAGIC_EP);
    qemu_put_be32(f, REPLAY_VERSION);
    qemu_put_be32(f, REPLAY_PROVIDER);
    qemu_put_be32(f, data->calls);
    qemu_put_byte(f, data->realtime);
    if (vmstate_save_state(f, &snapshot_vmstate, &data->before, NULL, errp) <
        0) {
        object_unref(buffer);
        return NULL;
    }
    qemu_put_byte(f, 0);
    if (vmstate_save_state(f, &snapshot_vmstate, &data->after, NULL, errp) <
        0) {
        object_unref(buffer);
        return NULL;
    }
    qemu_put_byte(f, 0);
    qemu_put_be32(f, data->io->len);
    for (unsigned i = 0; i < data->io->len; i++) {
        DSPReplayIO *io = g_ptr_array_index(data->io, i);
        gsize len;
        const uint8_t *bytes = g_bytes_get_data(io->bytes, &len);
        qemu_put_byte(f, io->fifo);
        qemu_put_byte(f, io->dir);
        qemu_put_be32(f, io->address);
        qemu_put_be32(f, len);
        if (len) {
            qemu_put_buffer(f, bytes, len);
        }
    }
    qemu_put_be32(f, REPLAY_END);
    if (qemu_fflush(f) < 0) {
        qemu_file_get_error_obj(f, errp);
        object_unref(buffer);
        return NULL;
    }
    GBytes *encoded = g_bytes_new(buffer->data, buffer->usage);
    object_unref(buffer);
    return encoded;
}

bool dsp_replay_save_case(const char *path, DSPReplayCase *data, Error **errp)
{
    g_autoptr(GBytes) buffer = encode_case(data, errp);
    if (!buffer) {
        return false;
    }
    g_autoptr(QIOChannelFile) output = qio_channel_file_new_path(
        path, O_WRONLY | O_CREAT | O_EXCL | O_BINARY, 0600, errp);
    if (!output) {
        return false;
    }
    gsize length;
    const uint8_t *bytes = g_bytes_get_data(buffer, &length);
    if (qio_channel_write_all(QIO_CHANNEL(output), bytes, length, errp) < 0) {
        qio_channel_close(QIO_CHANNEL(output), NULL);
        return false;
    }
    return qio_channel_close(QIO_CHANNEL(output), errp) == 0;
}

DSPReplayCase *dsp_replay_load_case(const char *path, Error **errp)
{
    g_autoptr(QIOChannelFile) input = qio_channel_file_new_path(
        path, O_RDONLY | O_BINARY | O_NONBLOCK, 0, errp);
    if (!input) {
        return NULL;
    }
    struct stat st;
    if (fstat(input->fd, &st) < 0 || !S_ISREG(st.st_mode) || st.st_size < 0 ||
        st.st_size > REPLAY_MAX_FILE) {
        error_setg(errp, "DSP replay input must be a bounded regular file");
        qio_channel_close(QIO_CHANNEL(input), NULL);
        return NULL;
    }
    g_autoptr(QIOChannelBuffer) buffer = qio_channel_buffer_new(st.st_size);
    if (qio_channel_read_all(QIO_CHANNEL(input), buffer->data, st.st_size,
                             errp) < 0) {
        qio_channel_close(QIO_CHANNEL(input), NULL);
        return NULL;
    }
    uint8_t extra;
    ssize_t remaining = qio_channel_read(QIO_CHANNEL(input), &extra, 1, errp);
    qio_channel_close(QIO_CHANNEL(input), NULL);
    if (remaining != 0) {
        if (remaining > 0) {
            error_setg(errp, "DSP replay input changed while reading");
        }
        return NULL;
    }
    buffer->usage = st.st_size;
    g_autoptr(QEMUFile) f = qemu_file_new_input(QIO_CHANNEL(buffer));
    g_autoptr(GBytes) canonical = NULL;
    DSPReplayCase *data = dsp_replay_case_new();
    uint64_t magic = qemu_get_be64(f);
    if ((magic != REPLAY_MAGIC_GP && magic != REPLAY_MAGIC_EP) ||
        qemu_get_be32(f) != REPLAY_VERSION ||
        qemu_get_be32(f) != REPLAY_PROVIDER) {
        error_setg(errp, "Unsupported DSP replay format/provider");
        goto fail;
    }
    data->is_gp = magic == REPLAY_MAGIC_GP;
    data->calls = qemu_get_be32(f);
    unsigned realtime = qemu_get_byte(f);
    if (!data->calls || data->calls > DSP_REPLAY_MAX_CALLS || realtime > 1) {
        error_setg(errp, "Invalid DSP replay call count/flags");
        goto fail;
    }
    data->realtime = realtime;
    if (vmstate_load_state(f, &snapshot_vmstate, &data->before, 1, errp) < 0) {
        goto fail;
    }
    if (qemu_get_byte(f) != 0) {
        error_setg(errp, "Invalid DSP replay snapshot separator");
        goto fail;
    }
    if (vmstate_load_state(f, &snapshot_vmstate, &data->after, 1, errp) < 0) {
        goto fail;
    }
    if (qemu_get_byte(f) != 0) {
        error_setg(errp, "Invalid DSP replay snapshot separator");
        goto fail;
    }
    unsigned count = qemu_get_be32(f);
    if (count > DSP_REPLAY_MAX_IO) {
        error_setg(errp, "DSP replay transaction count exceeds limit");
        goto fail;
    }
    size_t total = 0;
    for (unsigned i = 0; i < count; i++) {
        unsigned fifo = qemu_get_byte(f), dir = qemu_get_byte(f);
        uint32_t address = qemu_get_be32(f), len = qemu_get_be32(f);
        if (fifo > 1 || dir > 1 || len > DSP_REPLAY_MAX_BYTES - total) {
            error_setg(errp, "Invalid DSP replay transaction bounds/flags");
            goto fail;
        }
        total += len;
        uint8_t *bytes = g_malloc(len);
        if (len && qemu_get_buffer(f, bytes, len) != len) {
            g_free(bytes);
            error_setg(errp, "Truncated DSP replay transaction");
            goto fail;
        }
        DSPReplayIO *io = g_new0(DSPReplayIO, 1);
        *io = (DSPReplayIO){ .fifo = fifo,
                             .dir = dir,
                             .address = address,
                             .bytes = g_bytes_new_take(bytes, len) };
        g_ptr_array_add(data->io, io);
    }
    if (qemu_get_be32(f) != REPLAY_END || qemu_file_get_error_obj(f, NULL)) {
        error_setg(errp, "Incomplete DSP replay input");
        goto fail;
    }
    /*
     * Canonical re-encoding detects trailing bytes and noncanonical bools,
     * without probing EOF through VMState's subsection lookahead.
     */
    canonical = encode_case(data, errp);
    if (!canonical) {
        goto fail;
    }
    if (g_bytes_get_size(canonical) != buffer->usage ||
        memcmp(g_bytes_get_data(canonical, NULL), buffer->data,
               buffer->usage)) {
        error_setg(errp, "Noncanonical/trailing DSP replay input");
        goto fail;
    }
    return data;
fail:
    dsp_replay_case_free(data);
    return NULL;
}

bool dsp_replay_take_snapshot(DSPState *dsp, DSPReplaySnapshot *s)
{
    if (!dsp || !s) {
        return false;
    }
    memset(s, 0, sizeof(*s));
    if (!dsp_jit_get_native_state(dsp, &s->jit)) {
        return false;
    }
    s->interrupts = dsp->interrupts;
    s->save_cycles = dsp->save_cycles;
    s->dma = (DSPReplayDMA){
        .configuration = dsp->dma.configuration,
        .control = dsp->dma.control,
        .start_block = dsp->dma.start_block,
        .next_block = dsp->dma.next_block,
        .dma_read_count = dsp->dma.dma_read_count,
        .error = dsp->dma.error,
        .eol = dsp->dma.eol,
    };
    return true;
}

bool dsp_replay_snapshot_equal(const DSPReplaySnapshot *a,
                               const DSPReplaySnapshot *b,
                               const char **different_field)
{
    if (different_field) {
        *different_field = NULL;
    }
    if (!a || !b) {
        return false;
    }
    for (const VMStateField *field = snapshot_vmstate.fields; field->name;
         field++) {
        size_t count = field->flags & VMS_ARRAY ? field->num : 1;
        size_t length = field->size * count;
        if (memcmp((const uint8_t *)a + field->offset,
                   (const uint8_t *)b + field->offset, length)) {
            if (different_field) {
                *different_field = field->name;
            }
            return false;
        }
    }
    return true;
}

static void restore_snapshot(DSPState *dsp, const DSPReplaySnapshot *s)
{
    bool restored = dsp_jit_set_native_state(dsp, &s->jit);
    assert(restored);
    dsp->interrupts = s->interrupts;
    dsp->save_cycles = s->save_cycles;
    dsp->dma.configuration = s->dma.configuration;
    dsp->dma.control = s->dma.control;
    dsp->dma.start_block = s->dma.start_block;
    dsp->dma.next_block = s->dma.next_block;
    dsp->dma.dma_read_count = s->dma.dma_read_count;
    dsp->dma.error = s->dma.error;
    dsp->dma.eol = s->dma.eol;
}

struct DSPReplay {
    DSPState *dsp;
    char *path;
    int64_t deadline_us;
    DSPReplayCase *data;
    void *original_opaque;
    dsp_scratch_rw_func scratch;
    dsp_fifo_rw_func fifo;
    size_t bytes;
    uint32_t requested, completed;
    bool active, failed;
    char excluded_ep_digest[65];
};

bool dsp_replay_configure_sequence(DSPState *dsp, const char *path,
                                   uint32_t delay_ms, uint32_t count,
                                   Error **errp)
{
    if (dsp->ops != &jit_dsp_ops || dsp->replay || !path ||
        !*path || delay_ms > REPLAY_MAX_DELAY_MS || !count ||
        count > DSP_REPLAY_MAX_FRAMES) {
        error_setg(errp,
                   "DSP replay requires an unarmed JIT and valid path/delay/count");
        return false;
    }
    DSPReplay *r = g_new0(DSPReplay, 1);
    r->dsp = dsp;
    r->path = g_strdup(path);
    r->deadline_us = g_get_monotonic_time() + (int64_t)delay_ms * 1000;
    r->requested = count;
    dsp->replay = r;
    return true;
}

bool dsp_replay_configure(DSPState *dsp, const char *path, uint32_t delay_ms,
                          Error **errp)
{
    return dsp_replay_configure_sequence(dsp, path, delay_ms, 1, errp);
}

void dsp_replay_init(DSPState *dsp)
{
    const char *path = g_getenv(dsp->is_gp ? "XEMU_GP_REPLAY_PATH" :
                                             "XEMU_EP_REPLAY_PATH");
    const char *delay = g_getenv(dsp->is_gp ? "XEMU_GP_REPLAY_DELAY_MS" :
                                              "XEMU_EP_REPLAY_DELAY_MS");
    const char *count = g_getenv(dsp->is_gp ? "XEMU_GP_REPLAY_COUNT" :
                                              "XEMU_EP_REPLAY_COUNT");
    const char *excluded = dsp->is_gp ? NULL :
        g_getenv("XEMU_EP_REPLAY_EXCLUDE_SHA256");
    uint64_t ms = 0, frames = 1;
    Error *err = NULL;
    if (!path || !*path) {
        return;
    }
    if (delay &&
        (qemu_strtou64(delay, NULL, 10, &ms) < 0 || ms > REPLAY_MAX_DELAY_MS)) {
        error_report("Invalid %s replay delay", dsp->is_gp ? "GP" : "EP");
        return;
    }
    if (count && (qemu_strtou64(count, NULL, 10, &frames) < 0 || !frames ||
                  frames > DSP_REPLAY_MAX_FRAMES)) {
        error_report("Invalid %s replay count", dsp->is_gp ? "GP" : "EP");
        return;
    }
    if (excluded) {
        if (strlen(excluded) != 64) {
            error_report("Invalid EP replay excluded program digest");
            return;
        }
        for (unsigned i = 0; i < 64; i++) {
            if (!g_ascii_isxdigit(excluded[i])) {
                error_report("Invalid EP replay excluded program digest");
                return;
            }
        }
    }
    if (!dsp_replay_configure_sequence(dsp, path, ms, frames, &err)) {
        error_report_err(err);
    } else if (excluded) {
        for (unsigned i = 0; i < 64; i++) {
            dsp->replay->excluded_ep_digest[i] = g_ascii_tolower(excluded[i]);
        }
    }
}

static void restore_callbacks(DSPReplay *r)
{
    if (r->active) {
        r->dsp->dma.rw_opaque = r->original_opaque;
        r->dsp->dma.scratch_rw = r->scratch;
        r->dsp->dma.fifo_rw = r->fifo;
        r->active = false;
    }
}

void dsp_replay_cleanup(DSPState *dsp)
{
    DSPReplay *r = dsp->replay;
    if (r) {
        restore_callbacks(r);
        dsp->replay = NULL;
        dsp_replay_case_free(r->data);
        g_free(r->path);
        g_free(r);
    }
}

static void record_io(DSPReplay *r, bool fifo, bool dir, uint32_t address,
                      const uint8_t *ptr, size_t len)
{
    if (r->failed) {
        return;
    }
    if (r->data->io->len >= DSP_REPLAY_MAX_IO ||
        len > DSP_REPLAY_MAX_BYTES - r->bytes) {
        r->failed = true;
        return;
    }
    DSPReplayIO *io = g_new0(DSPReplayIO, 1);
    *io = (DSPReplayIO){ .fifo = fifo,
                         .dir = dir,
                         .address = address,
                         .bytes = g_bytes_new(ptr, len) };
    g_ptr_array_add(r->data->io, io);
    r->bytes += len;
}

static void capture_scratch(void *opaque, uint8_t *ptr, uint32_t addr,
                            size_t len, bool dir)
{
    DSPReplay *r = opaque;
    if (dir) {
        record_io(r, false, dir, addr, ptr, len);
    }
    r->scratch(r->original_opaque, ptr, addr, len, dir);
    if (!dir) {
        record_io(r, false, dir, addr, ptr, len);
    }
}

static void capture_fifo(void *opaque, uint8_t *ptr, unsigned index, size_t len,
                         bool dir)
{
    DSPReplay *r = opaque;
    if (dir) {
        record_io(r, true, dir, index, ptr, len);
    }
    r->fifo(r->original_opaque, ptr, index, len, dir);
    if (!dir) {
        record_io(r, true, dir, index, ptr, len);
    }
}

DSPReplay *dsp_replay_begin(DSPState *dsp, bool realtime)
{
    DSPReplay *r = dsp->replay;
    if (!r || r->active || g_get_monotonic_time() < r->deadline_us) {
        return NULL;
    }
    r->data = dsp_replay_case_new();
    r->data->is_gp = dsp->is_gp;
    if (!dsp_replay_take_snapshot(dsp, &r->data->before)) {
        error_report("DSP replay capture no longer has a JIT backend");
        dsp_replay_cleanup(dsp);
        return NULL;
    }
    if (r->excluded_ep_digest[0] && !r->completed) {
        uint32_t words[DSP_PRAM_SIZE];
        for (unsigned i = 0; i < DSP_PRAM_SIZE; i++) {
            words[i] = cpu_to_le32(ep_mix_identity_word(
                i, r->data->before.jit.pram[i]));
        }
        g_autofree char *digest = g_compute_checksum_for_data(
            G_CHECKSUM_SHA256, (const uint8_t *)words, sizeof(words));
        if (!strcmp(digest, r->excluded_ep_digest)) {
            dsp_replay_case_free(r->data);
            r->data = NULL;
            return NULL;
        }
    }
    r->data->realtime = realtime;
    r->original_opaque = dsp->dma.rw_opaque;
    r->scratch = dsp->dma.scratch_rw;
    r->fifo = dsp->dma.fifo_rw;
    dsp->dma.rw_opaque = r;
    dsp->dma.scratch_rw = capture_scratch;
    dsp->dma.fifo_rw = capture_fifo;
    r->active = true;
    return r;
}

bool dsp_replay_end(DSPReplay *r, uint64_t calls, Error **errp)
{
    DSPState *dsp = r->dsp;
    bool ok = false;
    restore_callbacks(r);
    if (r->failed || !calls || calls > DSP_REPLAY_MAX_CALLS ||
        !dsp_replay_take_snapshot(dsp, &r->data->after)) {
        error_setg(errp, "DSP replay capture incomplete or exceeds limits");
    } else {
        r->data->calls = calls;
        g_autofree char *numbered = r->requested > 1 ?
            g_strdup_printf("%s.%04u", r->path, r->completed) : NULL;
        ok = dsp_replay_save_case(numbered ? numbered : r->path, r->data, errp);
    }
    if (ok && ++r->completed < r->requested) {
        dsp_replay_case_free(r->data);
        r->data = NULL;
        r->bytes = 0;
        r->failed = false;
        return true;
    }
    dsp_replay_cleanup(dsp);
    return ok;
}

typedef struct ReplayCursor {
    DSPReplayCase *data;
    unsigned position;
    Error *error;
} ReplayCursor;

static void replay_io(ReplayCursor *cursor, bool fifo, bool dir,
                      uint32_t address, uint8_t *ptr, size_t len)
{
    if (!dir && len) {
        memset(ptr, 0, len);
    }
    if (cursor->error) {
        return;
    }
    if (cursor->position >= cursor->data->io->len) {
        error_setg(&cursor->error, "Unexpected DSP replay transaction %u",
                   cursor->position);
        return;
    }
    DSPReplayIO *io = g_ptr_array_index(cursor->data->io, cursor->position++);
    gsize recorded_len;
    const uint8_t *bytes = g_bytes_get_data(io->bytes, &recorded_len);
    if (io->fifo != fifo || io->dir != dir || io->address != address ||
        recorded_len != len || (dir && len && memcmp(bytes, ptr, len))) {
        error_setg(&cursor->error, "DSP replay transaction %u differs",
                   cursor->position - 1);
        return;
    }
    if (!dir && len) {
        memcpy(ptr, bytes, len);
    }
}

static void replay_scratch(void *opaque, uint8_t *ptr, uint32_t address,
                           size_t len, bool dir)
{
    replay_io(opaque, false, dir, address, ptr, len);
}

static void replay_fifo(void *opaque, uint8_t *ptr, unsigned index, size_t len,
                        bool dir)
{
    replay_io(opaque, true, dir, index, ptr, len);
}

bool dsp_replay_verify_case(DSPReplayCase *data, Error **errp)
{
    if (!valid_case(data, errp)) {
        return false;
    }
    ReplayCursor cursor = { .data = data };
    DSPState *dsp = dsp_init(&cursor, replay_scratch, replay_fifo, data->is_gp);
    dsp_set_engine(dsp, true);
    restore_snapshot(dsp, &data->before);
    for (unsigned i = 0; i < data->calls; i++) {
        dsp_run(dsp, 1000);
        if (cursor.error) {
            break;
        }
        if (i + 1 < data->calls && dsp_get_halt_requested(dsp)) {
            error_setg(&cursor.error,
                       "DSP replay halted before recorded call count");
            break;
        }
    }
    if (!cursor.error && cursor.position != data->io->len) {
        error_setg(&cursor.error, "DSP replay did not consume all transactions");
    }
    DSPReplaySnapshot *actual = g_new0(DSPReplaySnapshot, 1);
    dsp_replay_take_snapshot(dsp, actual);
    dsp_destroy(dsp);
    if (cursor.error) {
        error_propagate(errp, cursor.error);
        g_free(actual);
        return false;
    }
    /*
     * The codec contains only fixed scalar/array fields. Compare those
     * same semantic ranges, excluding all native ABI padding.
     */
    bool matched = true;
    for (const VMStateField *field = snapshot_vmstate.fields; field->name;
         field++) {
        size_t count = field->flags & VMS_ARRAY ? field->num : 1;
        size_t length = field->size * count;
        if (memcmp((uint8_t *)actual + field->offset,
                   (uint8_t *)&data->after + field->offset, length)) {
            error_setg(errp, "DSP replay post-state differs at %s", field->name);
            matched = false;
            break;
        }
    }
    g_free(actual);
    return matched;
}

DSPReplayNativeResult dsp_replay_native_dma_start(DSPState *dsp,
                                                  const DSPJitState *before,
                                                  DSPJitState *scratch,
                                                  bool preserve_cache,
                                                  Error **errp)
{
    if (!dsp || !before || !scratch || before == scratch) {
        return DSP_REPLAY_NATIVE_SKIPPED;
    }
    const Dsp56300State *cpu = &before->cpu;
    uint32_t pc = cpu->pc;
    if (pc >= DSP_PRAM_SIZE - 1 || before->pram[pc] != 0x08f496 ||
        before->pram[pc + 1] != 1 || cpu->pc_on_rep || cpu->loop_rep ||
        cpu->halt_requested || cpu->power_state ||
        cpu->interrupts.pending_bits[0] || cpu->interrupts.pending_bits[1] ||
        cpu->interrupts.pipeline_stage || cpu->interrupts.state ||
        cpu->interrupts.ipl_to_raise ||
        ((cpu->registers[DSP_REG_SR] & (1U << DSP_SR_LF)) &&
         cpu->registers[DSP_REG_LA] == pc + 1)) {
        return DSP_REPLAY_NATIVE_SKIPPED;
    }
    /* DMA can update guest memory and PRAM. Fetch the state again after the
     * transfer so a state restore cannot overwrite those side effects. */
    write_peripheral(dsp, 0xffffd6, 1);
    if (!dsp_jit_get_native_state(dsp, scratch)) {
        error_setg(errp, "Native DMA could not read post-transfer state");
        return DSP_REPLAY_NATIVE_FAILED;
    }
    scratch->cpu.pc = pc + 2;
    scratch->cpu.pc_advance = 2;
    scratch->cpu.cycle_count = cpu->cycle_count + 3;
    bool restored = preserve_cache ?
                        dsp_jit_set_native_data_state(dsp, scratch) :
                        dsp_jit_set_native_state(dsp, scratch);
    if (!restored) {
        error_setg(errp, "Native DMA could not restore post-transfer state");
        return DSP_REPLAY_NATIVE_FAILED;
    }
    return DSP_REPLAY_NATIVE_DONE;
}

DSPReplayNativeResult dsp_replay_native_dma_sequence(DSPState *dsp,
                                                     const DSPJitState *before,
                                                     DSPJitState *scratch,
                                                     bool preserve_cache,
                                                     Error **errp)
{
    static const uint32_t words[8] = {
        0x08f496, 4, 0x0cd623, 0, 0x08f496, 1, 0x0cd604, 0,
    };
    if (!dsp || !before || !scratch || before == scratch) {
        return DSP_REPLAY_NATIVE_SKIPPED;
    }
    const Dsp56300State *cpu = &before->cpu;
    uint32_t pc = cpu->pc;
    if (pc > DSP_PRAM_SIZE - ARRAY_SIZE(words) ||
        memcmp(before->pram + pc, words, sizeof(words)) || cpu->pc_on_rep ||
        cpu->loop_rep || cpu->halt_requested || cpu->power_state ||
        cpu->interrupts.pending_bits[0] || cpu->interrupts.pending_bits[1] ||
        cpu->interrupts.pipeline_stage || cpu->interrupts.state ||
        cpu->interrupts.ipl_to_raise ||
        ((cpu->registers[DSP_REG_SR] & (1U << DSP_SR_LF)) &&
         cpu->registers[DSP_REG_LA] >= pc &&
         cpu->registers[DSP_REG_LA] < pc + ARRAY_SIZE(words)) ||
        dsp->dma.control != (DMA_CONTROL_STOPPED | DMA_CONTROL_FROZEN) ||
        dsp->dma.dma_read_count != 1 || dsp->dma.eol || dsp->dma.error) {
        return DSP_REPLAY_NATIVE_SKIPPED;
    }
    write_peripheral(dsp, 0xffffd6, DMA_CONTROL_ACTION_UNFREEZE);
    uint32_t unfrozen = read_peripheral(dsp, 0xffffd6);
    write_peripheral(dsp, 0xffffd6, DMA_CONTROL_ACTION_START);
    uint32_t running = read_peripheral(dsp, 0xffffd6);
    if ((unfrozen & DMA_CONTROL_FROZEN) || !(running & DMA_CONTROL_RUNNING)) {
        error_setg(errp,
                   "Native DMA sequence observed unexpected control state");
        return DSP_REPLAY_NATIVE_FAILED;
    }
    if (!dsp_jit_get_native_state(dsp, scratch)) {
        error_setg(errp,
                   "Native DMA sequence could not read post-transfer state");
        return DSP_REPLAY_NATIVE_FAILED;
    }
    /* A transfer may rewrite PRAM, including the polling instructions.
     * Side effects have already happened, so fail rather than continue with
     * assumptions about code that is no longer the verified sequence. */
    if (memcmp(scratch->pram + pc, words, sizeof(words))) {
        error_setg(errp, "Native DMA sequence changed its own program");
        return DSP_REPLAY_NATIVE_FAILED;
    }
    scratch->cpu.pc = pc + ARRAY_SIZE(words);
    scratch->cpu.pc_advance = 2;
    scratch->cpu.cycle_count = cpu->cycle_count + 16;
    bool restored = preserve_cache ?
                        dsp_jit_set_native_data_state(dsp, scratch) :
                        dsp_jit_set_native_state(dsp, scratch);
    if (!restored) {
        error_setg(errp,
                   "Native DMA sequence could not restore post-transfer state");
        return DSP_REPLAY_NATIVE_FAILED;
    }
    return DSP_REPLAY_NATIVE_DONE;
}

DSPReplayNativeResult
dsp_replay_native_dma_stopfreeze(DSPState *dsp, const DSPJitState *before,
                                 DSPJitState *scratch, bool preserve_cache,
                                 Error **errp)
{
    static const uint32_t words[8] = {
        0x08f496, 2, 0x0cd624, 0, 0x08f496, 3, 0x0cd603, 0,
    };
    if (!dsp || !before || !scratch || before == scratch) {
        return DSP_REPLAY_NATIVE_SKIPPED;
    }
    const Dsp56300State *cpu = &before->cpu;
    uint32_t pc = cpu->pc;
    if (pc > DSP_PRAM_SIZE - ARRAY_SIZE(words) ||
        memcmp(before->pram + pc, words, sizeof(words)) || cpu->pc_on_rep ||
        cpu->loop_rep || cpu->halt_requested || cpu->power_state ||
        cpu->interrupts.pending_bits[0] || cpu->interrupts.pending_bits[1] ||
        cpu->interrupts.pipeline_stage || cpu->interrupts.state ||
        cpu->interrupts.ipl_to_raise ||
        ((cpu->registers[DSP_REG_SR] & (1U << DSP_SR_LF)) &&
         cpu->registers[DSP_REG_LA] >= pc &&
         cpu->registers[DSP_REG_LA] < pc + ARRAY_SIZE(words)) ||
        dsp->dma.control != DMA_CONTROL_RUNNING ||
        dsp->dma.dma_read_count != 1 || dsp->dma.eol || dsp->dma.error) {
        return DSP_REPLAY_NATIVE_SKIPPED;
    }
    /* Stopping clears RUNNING before the DMA engine is invoked; freezing the
     * stopped controller likewise cannot initiate a transfer. */
    write_peripheral(dsp, 0xffffd6, DMA_CONTROL_ACTION_STOP);
    uint32_t stopped = read_peripheral(dsp, 0xffffd6);
    write_peripheral(dsp, 0xffffd6, DMA_CONTROL_ACTION_FREEZE);
    uint32_t frozen = read_peripheral(dsp, 0xffffd6);
    if ((stopped & DMA_CONTROL_RUNNING) || !(frozen & DMA_CONTROL_FROZEN)) {
        error_setg(errp,
                   "Native DMA stop/freeze observed unexpected control state");
        return DSP_REPLAY_NATIVE_FAILED;
    }
    *scratch = *before;
    scratch->cpu.pc = pc + ARRAY_SIZE(words);
    scratch->cpu.pc_advance = 2;
    scratch->cpu.cycle_count = cpu->cycle_count + 16;
    bool restored = preserve_cache ?
                        dsp_jit_set_native_data_state(dsp, scratch) :
                        dsp_jit_set_native_state(dsp, scratch);
    if (!restored) {
        error_setg(errp, "Native DMA stop/freeze could not restore CPU state");
        return DSP_REPLAY_NATIVE_FAILED;
    }
    return DSP_REPLAY_NATIVE_DONE;
}

bool dsp_replay_verify_gp_hle(DSPReplayCase *data, bool enable_native,
                              bool preserve_cache, DSPGPReplayStats *stats,
                              Error **errp)
{
    if (stats) {
        *stats = (DSPGPReplayStats){ 0 };
    }
    if (!valid_case(data, errp)) {
        return false;
    }
    if (!data->is_gp || !data->realtime) {
        error_setg(errp, "GP feedback replay requires a realtime GP capture");
        return false;
    }
    GChecksum *checksum = g_checksum_new(G_CHECKSUM_SHA256);
    for (unsigned i = 0; i < DSP_PRAM_SIZE; i++) {
        uint32_t word = cpu_to_le32(data->before.jit.pram[i] & 0xffffff);
        g_checksum_update(checksum, (const uint8_t *)&word, sizeof(word));
    }
    bool recognized = !strcmp(
        g_checksum_get_string(checksum),
        "f7c1b74d7ff1493a3c52fd2ca032fe744d96fabf56b6dd93de22114c793e525b");
    g_checksum_free(checksum);
    if (!recognized) {
        error_setg(errp, "Unknown GP feedback program");
        return false;
    }
    ReplayCursor cursor = { .data = data };
    DSPState *dsp = dsp_init(&cursor, replay_scratch, replay_fifo, true);
    dsp_set_engine(dsp, true);
    restore_snapshot(dsp, &data->before);
    g_autofree DSPJitState *state = g_new0(DSPJitState, 1);
    g_autofree DSPJitState *next = g_new0(DSPJitState, 1);
    DSPGPReplayStats executed = { 0 };
    unsigned steps;
    for (steps = 0; steps < 1000000 && !dsp_get_halt_requested(dsp); steps++) {
        if (!dsp_jit_get_native_state(dsp, state)) {
            error_setg(&cursor.error, "GP replay state unavailable");
            break;
        }
        bool stopfreeze_pc = state->cpu.pc == 0x10a || state->cpu.pc == 0x8d1 ||
                             state->cpu.pc == 0x9c1 || state->cpu.pc == 0xa3d ||
                             state->cpu.pc == 0xab9 || state->cpu.pc == 0xb35;
        if (enable_native && stopfreeze_pc &&
            !memcmp(state->pram, data->before.jit.pram, sizeof(state->pram))) {
            Error *native_error = NULL;
            DSPReplayNativeResult result = dsp_replay_native_dma_stopfreeze(
                dsp, state, next, preserve_cache, &native_error);
            if (native_error) {
                if (cursor.error) {
                    error_free(native_error);
                } else {
                    cursor.error = native_error;
                }
            }
            if (result == DSP_REPLAY_NATIVE_FAILED || cursor.error) {
                break;
            }
            if (result == DSP_REPLAY_NATIVE_DONE) {
                executed.dma_stopfreeze_calls++;
                executed.native_cycles += 16;
                continue;
            }
        }
        bool sequence_pc = state->cpu.pc == 0x118 || state->cpu.pc == 0x8df ||
                           state->cpu.pc == 0x9cf || state->cpu.pc == 0xa4b ||
                           state->cpu.pc == 0xac7 || state->cpu.pc == 0xb43;
        if (enable_native && sequence_pc &&
            !memcmp(state->pram, data->before.jit.pram, sizeof(state->pram))) {
            unsigned previous_io = cursor.position;
            Error *native_error = NULL;
            DSPReplayNativeResult result = dsp_replay_native_dma_sequence(
                dsp, state, next, preserve_cache, &native_error);
            if (native_error) {
                if (cursor.error) {
                    error_free(native_error);
                } else {
                    cursor.error = native_error;
                }
            }
            if (result == DSP_REPLAY_NATIVE_FAILED || cursor.error) {
                break;
            }
            if (result == DSP_REPLAY_NATIVE_DONE) {
                executed.dma_sequence_calls++;
                executed.dma_sequence_transactions +=
                    cursor.position - previous_io;
                executed.native_cycles += 16;
                continue;
            }
        }
        bool dma_pc = state->cpu.pc == 0x11c || state->cpu.pc == 0x8e3 ||
                      state->cpu.pc == 0x9d3 || state->cpu.pc == 0xa4f ||
                      state->cpu.pc == 0xacb || state->cpu.pc == 0xb47;
        if (enable_native && dma_pc &&
            !memcmp(state->pram, data->before.jit.pram, sizeof(state->pram))) {
            unsigned previous_io = cursor.position;
            Error *native_error = NULL;
            DSPReplayNativeResult result = dsp_replay_native_dma_start(
                dsp, state, next, preserve_cache, &native_error);
            if (native_error) {
                if (cursor.error) {
                    error_free(native_error);
                } else {
                    cursor.error = native_error;
                }
            }
            if (result == DSP_REPLAY_NATIVE_FAILED || cursor.error) {
                break;
            }
            if (result == DSP_REPLAY_NATIVE_DONE) {
                executed.dma_start_calls++;
                executed.dma_start_transactions +=
                    cursor.position - previous_io;
                executed.native_cycles += 3;
                continue;
            }
        }
        bool filter_pc = state->cpu.pc >= 0x185 && state->cpu.pc <= 0x413 &&
                         ((state->cpu.pc - 0x185) % 0x55 == 0 ||
                          (state->cpu.pc - 0x185) % 0x55 == 0x3b);
        bool stable =
            enable_native &&
            (state->cpu.pc == 0x788 || state->cpu.pc == 0x761 ||
             state->cpu.pc == 0x4a0 || state->cpu.pc == 0x6a2 ||
             state->cpu.pc == 0x431 || state->cpu.pc == 0x49b ||
             state->cpu.pc == 0x944 || state->cpu.pc == 0x986 ||
             state->cpu.pc == 0xa02 || state->cpu.pc == 0xa7e ||
             state->cpu.pc == 0xafa || state->cpu.pc == 0x493 || filter_pc) &&
            !memcmp(state->pram, data->before.jit.pram, sizeof(state->pram));
        bool feedback = stable && state->cpu.pc == 0x788 &&
                        gp_feedback_project(state, next);
        bool mix2 =
            stable && state->cpu.pc == 0x761 && gp_mix2_project(state, next);
        bool add =
            stable && state->cpu.pc == 0x4a0 && gp_add_project(state, next);
        bool matrix =
            stable && state->cpu.pc == 0x6a2 && gp_matrix_project(state, next);
        bool meter =
            stable && state->cpu.pc == 0x431 && gp_meter_project(state, next);
        bool mac =
            stable && state->cpu.pc == 0x49b && gp_mac_project(state, next);
        bool delay =
            stable && state->cpu.pc == 0x944 && gp_delay_project(state, next);
        bool decimate = stable &&
                        (state->cpu.pc == 0x986 || state->cpu.pc == 0xa02 ||
                         state->cpu.pc == 0xa7e || state->cpu.pc == 0xafa) &&
                        gp_decimate_project(state, next);
        bool gain =
            stable && state->cpu.pc == 0x493 && gp_gain_project(state, next);
        bool filter = false;
        bool filter_nonzero = false;
        if (stable && filter_pc) {
            *next = *state;
            next->cpu.cycle_budget = 0;
            filter = gp_filter_hle_run_verified(&next->cpu, next->xram,
                                                next->yram, 1000);
            next->cpu.cycle_budget = state->cpu.cycle_budget;
            if (filter) {
                unsigned start = state->cpu.registers[DSP_REG_R3];
                unsigned end = next->cpu.registers[DSP_REG_R3];
                if (start <= end && end <= DSP_XRAM_SIZE) {
                    for (unsigned i = start; i < end; i++) {
                        filter_nonzero |= next->xram[i] != 0;
                    }
                }
                if ((next->cpu.pc - 0x185) % 0x55 == 0x3b) {
                    filter_nonzero |= next->cpu.registers[DSP_REG_A1] != 0;
                }
            }
        }
        if (feedback || mix2 || add || matrix || meter || filter || mac ||
            delay || decimate || gain) {
            bool restored = preserve_cache ?
                                dsp_jit_set_native_data_state(dsp, next) :
                                dsp_jit_set_native_state(dsp, next);
            if (!restored) {
                error_setg(&cursor.error, "GP replay state restore failed");
                break;
            }
            executed.feedback_calls += feedback;
            executed.mix2_calls += mix2;
            executed.add_calls += add;
            executed.matrix_calls += matrix;
            executed.meter_calls += meter;
            executed.filter_calls += filter;
            executed.mac_calls += mac;
            executed.delay_calls += delay;
            executed.decimate_calls += decimate;
            executed.gain_calls += gain;
            executed.filter_nonzero_calls += filter_nonzero;
            executed.native_cycles += feedback ? GP_FEEDBACK_PROJECT_CYCLES :
                                      mix2     ? GP_MIX2_PROJECT_CYCLES :
                                      add      ? GP_ADD_PROJECT_CYCLES :
                                      matrix   ? GP_MATRIX_PROJECT_CYCLES :
                                      meter    ? GP_METER_PROJECT_CYCLES :
                                      mac      ? GP_MAC_PROJECT_CYCLES :
                                      gain ? GP_GAIN_PROJECT_CYCLES :
                                             (uint32_t)(next->cpu.cycle_count -
                                                        state->cpu.cycle_count);
        } else {
            dsp_step(dsp);
            executed.dsp_steps++;
        }
        if (cursor.error) {
            break;
        }
    }
    if (!cursor.error && !dsp_get_halt_requested(dsp)) {
        error_setg(&cursor.error,
                   "GP feedback replay did not halt within step limit");
    }
    if (!cursor.error && cursor.position != data->io->len) {
        error_setg(&cursor.error,
                   "GP feedback replay did not consume all transactions");
    }
    if (!cursor.error && enable_native && !executed.feedback_calls &&
        !executed.mix2_calls && !executed.add_calls && !executed.matrix_calls &&
        !executed.meter_calls && !executed.filter_calls &&
        !executed.mac_calls && !executed.delay_calls &&
        !executed.decimate_calls && !executed.gain_calls &&
        !executed.dma_start_calls && !executed.dma_sequence_calls &&
        !executed.dma_stopfreeze_calls) {
        error_setg(&cursor.error,
                   "GP feedback replay executed no native calls");
    }
    g_autofree DSPReplaySnapshot *actual = g_new0(DSPReplaySnapshot, 1);
    if (!cursor.error && !dsp_replay_take_snapshot(dsp, actual)) {
        error_setg(&cursor.error, "GP replay final state unavailable");
    }
    dsp_destroy(dsp);
    if (stats) {
        *stats = executed;
    }
    if (cursor.error) {
        error_propagate(errp, cursor.error);
        return false;
    }
    for (const VMStateField *field = snapshot_vmstate.fields; field->name;
         field++) {
        if (!strcmp(field->name, "jit.cpu.cycle_budget")) {
            continue; /* dsp_step does not use the run-loop budget. */
        }
        size_t count = field->flags & VMS_ARRAY ? field->num : 1;
        if (memcmp((uint8_t *)actual + field->offset,
                   (uint8_t *)&data->after + field->offset,
                   field->size * count)) {
            error_setg(errp, "GP feedback replay post-state differs at %s",
                       field->name);
            return false;
        }
    }
    return true;
}

typedef struct NativeEPReplayCursor {
    DSPReplayCase *data;
    unsigned position;
} NativeEPReplayCursor;
static bool native_ep_replay_read(void *opaque, uint32_t address, uint8_t *out,
                                  size_t length)
{
    NativeEPReplayCursor *c = opaque;
    if (c->position >= c->data->io->len)
        return false;
    DSPReplayIO *io = g_ptr_array_index(c->data->io, c->position++);
    gsize size;
    const uint8_t *bytes = g_bytes_get_data(io->bytes, &size);
    if (io->fifo || io->dir || io->address != address || size != length)
        return false;
    memcpy(out, bytes, size);
    return true;
}
static bool native_ep_overlay_check(void *opaque, const uint8_t *bytes,
                                    size_t length)
{
    (void)opaque;
    const char *expected =
        length == 96 ?
            "9494761d6c24f15823888f78fe0e25f081005183f5d492b626003724d916b582" :
        length == 804 ?
            "68da34164ce85698200097ee113446181ee6381aacc3a499ea1ca5c5d058a9d3" :
            NULL;
    if (!expected)
        return false;
    g_autofree char *actual =
        g_compute_checksum_for_data(G_CHECKSUM_SHA256, bytes, length);
    return strcmp(expected, actual) == 0;
}

static bool native_ep_replay_projection(DSPReplayCase *d, DSPReplaySnapshot *a,
                                        Error **errp)
{
    uint32_t phase = d->before.jit.pram[1], slot = d->before.jit.pram[0x116];
    uint32_t canonical[DSP_PRAM_SIZE], input[5][256];
    uint8_t pcm[1024];
    uint32_t predicted_calls;
    unsigned n = 0;
    g_autofree char *program = NULL;
    if (d->is_gp || !d->realtime || phase >= 6 || (slot != 0 && slot != 0x100))
        goto reject;
    for (unsigned i = 0; i < DSP_PRAM_SIZE; i++)
        canonical[i] =
            cpu_to_le32(ep_mix_identity_word(i, d->before.jit.pram[i]));
    program = g_compute_checksum_for_data(
        G_CHECKSUM_SHA256, (uint8_t *)canonical, sizeof(canonical));
    if (!ep_mix_digest_known(program))
        goto reject;
    if (d->io->len != (phase == 0 ? 8 : 7))
        goto reject;
    NativeEPReplayCursor cursor = { .data = d };
    if (!ep_native_prepare_input(&cursor, native_ep_replay_read,
                                 native_ep_overlay_check, phase, slot, input))
        goto reject;
    n = cursor.position;
    if (!ep_native_project_frame(&d->before, input, a, pcm, &predicted_calls) ||
        predicted_calls != d->calls) {
        goto reject;
    }
    DSPReplayIO *io = g_ptr_array_index(d->io, n);
    gsize len;
    const uint8_t *bytes = g_bytes_get_data(io->bytes, &len);
    if (!io->fifo || !io->dir || io->address || len != sizeof(pcm) ||
        memcmp(bytes, pcm, sizeof(pcm))) {
        goto reject;
    }
    return true;
reject:
    error_setg(errp, "native EP replay rejected program, state or I/O");
    return false;
}

/* Offline diagnostic: this checks a known captured route but does not expose
 * a live execution or fallback path. */
bool dsp_replay_verify_native_ep(DSPReplayCase *data, Error **errp)
{
    if (!valid_case(data, errp)) {
        return false;
    }
    g_autofree DSPReplaySnapshot *actual = g_new0(DSPReplaySnapshot, 1);
    if (!native_ep_replay_projection(data, actual, errp)) {
        return false;
    }
    for (const VMStateField *field = snapshot_vmstate.fields; field->name;
         field++) {
        size_t count = field->flags & VMS_ARRAY ? field->num : 1;
        size_t length = field->size * count;
        if (memcmp((uint8_t *)actual + field->offset,
                   (uint8_t *)&data->after + field->offset, length)) {
            error_setg(errp, "native EP replay post-state differs at %s",
                       field->name);
            return false;
        }
    }
    return true;
}

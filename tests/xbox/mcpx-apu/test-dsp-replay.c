/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qobject/qdict.h"
#include "qobject/qjson.h"
#include "hw/xbox/mcpx/apu/apu_int.h"
#include <glib/gstdio.h>
#include "hw/xbox/mcpx/apu/dsp/dsp-replay.h"

static char *test_directory;
static const char *test_executable;

static void test_capture_frame(void)
{
    g_autoptr(GError) error = NULL;
    g_autofree char *directory =
        g_dir_make_tmp("xemu-gp-replay-XXXXXX", &error);
    g_assert_no_error(error);
    g_autofree char *path = g_build_filename(directory, "frame.bin", NULL);
    MCPXAPUState *d = g_new0(MCPXAPUState, 1);
    float bins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME] = { 0 };

    g_setenv("XEMU_GP_REPLAY_PATH", path, true);
    g_setenv("XEMU_GP_REPLAY_DELAY_MS", "0", true);
    g_config.audio.use_dsp = true;
    g_config.audio.use_dsp_jit = true;
    mcpx_apu_dsp_init(d);
    d->gp.regs[NV_PAPU_GPRST] = 3;
    d->gp.realtime = true;
    /* Own program: movep #1,x:<<$ffffc4; nop. Assembled with dsp56300-asm. */
    dsp_write_memory(d->gp.dsp, 'P', 0, 0x08f484);
    dsp_write_memory(d->gp.dsp, 'P', 1, 1);
    dsp_write_memory(d->gp.dsp, 'P', 2, 0);
    mcpx_apu_dsp_frame(d, bins);
    g_assert_true(dsp_get_halt_requested(d->gp.dsp));
    g_assert_true(g_file_test(path, G_FILE_TEST_IS_REGULAR));
    Error *err = NULL;
    DSPReplayCase *data = dsp_replay_load_case(path, &err);
    g_assert_null(err);
    g_assert_nonnull(data);
    g_assert_true(dsp_replay_verify_case(data, &err));
    g_assert_null(err);
    dsp_replay_case_free(data);
    dsp_destroy(d->gp.dsp);
    dsp_destroy(d->ep.dsp);
    g_free(d);
    g_unsetenv("XEMU_GP_REPLAY_PATH");
    g_unsetenv("XEMU_GP_REPLAY_DELAY_MS");
    g_unlink(path);
    g_rmdir(directory);
}


typedef struct TestIO {
    unsigned reads, writes, fifos;
    uint8_t output[16];
} TestIO;

static void scratch_io(void *opaque, uint8_t *ptr, uint32_t address, size_t len,
                       bool dir)
{
    TestIO *io = opaque;
    if (dir) {
        io->writes++;
        memcpy(io->output, ptr, MIN(len, sizeof(io->output)));
    } else {
        io->reads++;
        for (size_t i = 0; i < len; i++) {
            ptr[i] = 0x40 + i;
        }
    }
}

static void fifo_io(void *opaque, uint8_t *ptr, unsigned index, size_t len,
                    bool dir)
{
    TestIO *io = opaque;
    g_assert_true(dir);
    g_assert_cmpuint(index, ==, 2);
    g_assert_cmpuint(len, ==, 16);
    g_assert_cmpmem(ptr, len, io->output, sizeof(io->output));
    io->fifos++;
}

static DSPState *make_dsp(TestIO *io, bool dma)
{
    /*
     * Independently assembled: movep #$40,x:<<$ffffd4;
     * movep #1,x:<<$ffffd6; movep #1,x:<<$ffffc4; nop.
     */
    static const uint32_t program[] = { 0x08f494, 0x40, 0x08f496, 1,
                                        0x08f484, 1,    0 };
    g_config.audio.use_dsp_jit = true;
    DSPState *dsp = dsp_init(io, scratch_io, fifo_io, true);
    if (dma) {
        for (unsigned i = 0; i < ARRAY_SIZE(program); i++) {
            dsp_write_memory(dsp, 'P', i, program[i]);
        }
        /* Scratch read -> scratch write -> FIFO write, four 24-bit words. */
        const uint32_t blocks[][7] = {
            { 0x50, 0x19e0, 4, 0x100, 0, 0x800, 0x3ff },
            { 0x60, 0x19e2, 4, 0x100, 0, 0x900, 0x3ff },
            { 0x4000, 0x1842, 4, 0x100, 0, 0, 0x3ff },
        };
        for (unsigned block = 0; block < 3; block++) {
            for (unsigned word = 0; word < 7; word++) {
                dsp_write_memory(dsp, 'X', 0x40 + 0x10 * block + word,
                                 blocks[block][word]);
            }
        }
    } else {
        dsp_write_memory(dsp, 'P', 0, 0x08f484);
        dsp_write_memory(dsp, 'P', 1, 1);
    }
    dsp_start_frame(dsp);
    dsp_set_halt_requested(dsp, false);
    dsp_set_cycle_count(dsp, 0);
    return dsp;
}

static unsigned run_dsp(DSPState *dsp)
{
    unsigned calls = 0;
    do {
        dsp_run(dsp, 1000);
        g_assert_cmpuint(++calls, <, 100);
    } while (!dsp_get_halt_requested(dsp));
    return calls;
}

static void assert_snapshot_equal(DSPReplaySnapshot *a, DSPReplaySnapshot *b);

static void test_dma_replay(void)
{
    TestIO io = { 0 };
    DSPState *dsp = make_dsp(&io, true);
    g_autofree char *path = g_build_filename(test_directory, "dma.bin", NULL);
    Error *err = NULL;
    g_assert_true(dsp_replay_configure(dsp, path, 0, &err));
    DSPReplay *capture = dsp_replay_begin(dsp, true);
    g_assert_nonnull(capture);
    unsigned calls = run_dsp(dsp);
    g_assert_true(dsp_replay_end(capture, calls, &err));
    g_assert_null(err);
    g_assert_null(dsp->replay);
    g_assert_true(dsp->dma.rw_opaque == &io);
    g_assert_true(dsp->dma.scratch_rw == scratch_io);
    g_assert_true(dsp->dma.fifo_rw == fifo_io);
    g_assert_cmpuint(io.reads, ==, 1);
    g_assert_cmpuint(io.writes, ==, 1);
    g_assert_cmpuint(io.fifos, ==, 1);
    g_assert_cmphex(dsp_read_memory(dsp, 'X', 0x100), ==, 0x424140);
    for (unsigned i = 0; i < 16; i++) {
        g_assert_cmpuint(io.output[i], ==, i % 4 == 3 ? 0 : 0x40 + i);
    }
    DSPReplayCase *data = dsp_replay_load_case(path, &err);
    g_assert_nonnull(data);
    g_assert_null(err);
    g_assert_cmpuint(data->io->len, ==, 3);
    g_assert_cmpuint(dsp->dma.dma_read_count, ==, 0);
    g_assert_cmphex(dsp->dma.control, ==, DMA_CONTROL_RUNNING);
    g_assert_null(dsp_replay_begin(dsp, true));
    TestIO baseline_io = { 0 };
    DSPState *baseline = make_dsp(&baseline_io, true);
    g_assert_cmpuint(run_dsp(baseline), ==, calls);
    DSPReplaySnapshot *plain = g_new0(DSPReplaySnapshot, 1);
    g_assert_true(dsp_jit_get_native_state(baseline, &plain->jit));
    plain->interrupts = baseline->interrupts;
    plain->save_cycles = baseline->save_cycles;
    plain->dma.configuration = baseline->dma.configuration;
    plain->dma.control = baseline->dma.control;
    plain->dma.start_block = baseline->dma.start_block;
    plain->dma.next_block = baseline->dma.next_block;
    plain->dma.dma_read_count = baseline->dma.dma_read_count;
    plain->dma.error = baseline->dma.error;
    plain->dma.eol = baseline->dma.eol;
    assert_snapshot_equal(&data->after, plain);
    g_assert_cmpmem(baseline_io.output, 16, io.output, 16);
    g_free(plain);
    dsp_destroy(baseline);
    g_assert_true(dsp_replay_verify_case(data, &err));
    g_assert_null(err);

    gchar *output = NULL, *errors = NULL;
    gint status;
    gchar replay_option[] = "--replay";
    gchar *arguments[] = { (gchar *)test_executable, replay_option,
                           (gchar *)path, NULL };
    g_assert_true(g_spawn_sync(NULL, arguments, NULL, 0, NULL, NULL, &output,
                               &errors, &status, NULL));
    g_assert_cmpint(status, ==, 0);
    QObject *object = qobject_from_json(output, &err);
    g_assert_null(err);
    QDict *reply = qobject_to(QDict, object);
    g_assert_nonnull(reply);
    g_assert_true(qdict_get_bool(reply, "matched"));
    g_assert_cmpint(qdict_get_int(reply, "transactions"), ==, 3);
    g_assert_cmpint(qdict_get_int(reply, "before_cycles"), ==, 0);
    g_assert_false(qdict_get_bool(reply, "before_halted"));
    g_assert_true(qdict_get_bool(reply, "halted"));
    g_assert_cmpuint(strlen(qdict_get_str(reply, "program_sha256")), ==, 64);
    qobject_unref(object);
    g_free(output);
    g_free(errors);

    /* Independently alter CPU, wide IRQ, power, memory and DMA expectations. */
    for (unsigned mutation = 0; mutation < 5; mutation++) {
        switch (mutation) {
        case 0:
            data->after.jit.cpu.pc ^= 1;
            break;
        case 1:
            data->after.jit.cpu.interrupts.pending_bits[1] ^= UINT64_C(1) << 63;
            break;
        case 2:
            data->after.jit.cpu.power_state ^= 1;
            break;
        case 3:
            data->after.jit.pram[DSP_PRAM_SIZE - 1] ^= 1;
            break;
        case 4:
            data->after.dma.dma_read_count ^= 1;
            break;
        }
        g_assert_false(dsp_replay_verify_case(data, &err));
        g_assert_nonnull(err);
        error_free(err);
        err = NULL;
        switch (mutation) {
        case 0:
            data->after.jit.cpu.pc ^= 1;
            break;
        case 1:
            data->after.jit.cpu.interrupts.pending_bits[1] ^= UINT64_C(1) << 63;
            break;
        case 2:
            data->after.jit.cpu.power_state ^= 1;
            break;
        case 3:
            data->after.jit.pram[DSP_PRAM_SIZE - 1] ^= 1;
            break;
        case 4:
            data->after.dma.dma_read_count ^= 1;
            break;
        }
    }
    DSPReplayIO *last = g_ptr_array_index(data->io, 2);
    gsize len;
    const uint8_t *old = g_bytes_get_data(last->bytes, &len);
    uint8_t *changed = g_memdup2(old, len);
    changed[0] ^= 1;
    g_bytes_unref(last->bytes);
    last->bytes = g_bytes_new_take(changed, len);
    g_assert_false(dsp_replay_verify_case(data, &err));
    g_assert_nonnull(err);
    error_free(err);
    dsp_replay_case_free(data);
    dsp_destroy(dsp);
    g_unlink(path);
}

static void seed_snapshot(DSPReplaySnapshot *s)
{
    s->jit.cpu.pc = 0xabcdef;
    s->jit.cpu.pc_advance = 3;
    s->jit.cpu.pc_on_rep = true;
    s->jit.cpu.cycle_count = 0xff123456;
    s->jit.cpu.cycle_budget = -17;
    s->jit.cpu.interrupts.pending_bits[0] = UINT64_C(0x8000000000000001);
    s->jit.cpu.interrupts.pending_bits[1] = UINT64_C(0xfedcba9876543210);
    s->jit.cpu.interrupts.pipeline_stage = 3;
    s->jit.cpu.interrupts.vector_addr = 0x123456;
    s->jit.cpu.interrupts.saved_pc = 0xfedcba;
    s->jit.cpu.interrupts.state = 2;
    s->jit.cpu.interrupts.ipl_to_raise = 7;
    for (unsigned i = 0; i < 128; i++) {
        s->jit.cpu.interrupts.ipl[i] = (int)i - 64;
    }
    s->jit.cpu.loop_rep = true;
    s->jit.cpu.halt_requested = true;
    s->jit.cpu.power_state = 2;
    for (unsigned i = 0; i < 64; i++) {
        s->jit.cpu.registers[i] = 0x81000000 + i;
    }
    for (unsigned i = 0; i < 16; i++) {
        s->jit.cpu.stack[0][i] = 0x120000 + i;
        s->jit.cpu.stack[1][i] = 0x340000 + i;
    }
    for (unsigned i = 0; i < DSP_XRAM_SIZE; i++) {
        s->jit.xram[i] = 0xcaca0000 + i;
        s->jit.pram[i] = 0x120000 + i;
    }
    for (unsigned i = 0; i < DSP_YRAM_SIZE; i++) {
        s->jit.yram[i] = 0x340000 + i;
    }
    s->dma.configuration = 0xff000123;
    s->dma.control = DMA_CONTROL_RUNNING;
    s->dma.start_block = 0x456;
    s->dma.next_block = 0x789;
    s->dma.dma_read_count = 2;
    s->dma.error = true;
    s->dma.eol = true;
    s->interrupts = 0x80000083;
    s->save_cycles = -9;
}

static void assert_snapshot_equal(DSPReplaySnapshot *a, DSPReplaySnapshot *b)
{
#define SAME(field) \
    g_assert_cmpmem(&a->field, sizeof(a->field), &b->field, sizeof(b->field))
    SAME(jit.cpu.pc);
    SAME(jit.cpu.pc_advance);
    SAME(jit.cpu.pc_on_rep);
    SAME(jit.cpu.cycle_count);
    SAME(jit.cpu.cycle_budget);
    SAME(jit.cpu.interrupts.pending_bits);
    SAME(jit.cpu.interrupts.pipeline_stage);
    SAME(jit.cpu.interrupts.vector_addr);
    SAME(jit.cpu.interrupts.saved_pc);
    SAME(jit.cpu.interrupts.state);
    SAME(jit.cpu.interrupts.ipl);
    SAME(jit.cpu.interrupts.ipl_to_raise);
    SAME(jit.cpu.loop_rep);
    SAME(jit.cpu.halt_requested);
    SAME(jit.cpu.power_state);
    SAME(jit.cpu.registers);
    SAME(jit.cpu.stack);
    SAME(jit.xram);
    SAME(jit.yram);
    SAME(jit.pram);
    SAME(dma.configuration);
    SAME(dma.control);
    SAME(dma.start_block);
    SAME(dma.next_block);
    SAME(dma.dma_read_count);
    SAME(dma.error);
    SAME(dma.eol);
    SAME(interrupts);
    SAME(save_cycles);
#undef SAME
}

static void test_codec(void)
{
    g_autofree char *path = g_build_filename(test_directory, "codec.bin", NULL);
    DSPReplayCase *data = dsp_replay_case_new();
    seed_snapshot(&data->before);
    seed_snapshot(&data->after);
    data->after.jit.cpu.power_state = 1;
    data->after.jit.yram[DSP_YRAM_SIZE - 1] ^= 0xff;
    data->calls = 1;
    Error *err = NULL;
    g_assert_true(dsp_replay_save_case(path, data, &err));
    g_assert_null(err);
    DSPReplayCase *loaded = dsp_replay_load_case(path, &err);
    g_assert_nonnull(loaded);
    g_assert_null(err);
    assert_snapshot_equal(&data->before, &loaded->before);
    assert_snapshot_equal(&data->after, &loaded->after);
    dsp_replay_case_free(loaded);

    g_autofree char *bytes = NULL;
    gsize length;
    g_assert_true(g_file_get_contents(path, &bytes, &length, NULL));
    g_autofree char *bad = g_build_filename(test_directory, "bad.bin", NULL);
    for (unsigned kind = 0; kind < 5; kind++) {
        gsize n = length;
        char *copy = g_malloc(length + 1);
        memcpy(copy, bytes, length);
        if (kind == 0) {
            copy[0] ^= 1;
        }
        if (kind == 1) {
            n--;
        }
        if (kind == 2) {
            copy[n++] = 0;
        }
        if (kind == 3) {
            memset(copy + n - 8, 0xff, 4);
        }
        if (kind == 4) {
            copy[20] = 2;
        }
        g_assert_true(g_file_set_contents(bad, copy, n, NULL));
        g_free(copy);
        loaded = dsp_replay_load_case(bad, &err);
        g_assert_null(loaded);
        g_assert_nonnull(err);
        error_free(err);
        err = NULL;
    }
    dsp_replay_case_free(data);
    g_unlink(path);
    g_unlink(bad);
}

static void test_disabled_delay_backend(void)
{
    TestIO io = { 0 };
    DSPState *dsp = make_dsp(&io, false);
    g_assert_null(dsp_replay_begin(dsp, true));
    g_autofree char *path =
        g_build_filename(test_directory, "delayed.bin", NULL);
    Error *err = NULL;
    g_assert_true(dsp_replay_configure(dsp, path, 86400000, &err));
    g_assert_null(dsp_replay_begin(dsp, true));
    run_dsp(dsp);
    g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));
    dsp_replay_cleanup(dsp);
    dsp_set_engine(dsp, false);
    g_assert_false(dsp_replay_configure(dsp, path, 0, &err));
    g_assert_nonnull(err);
    error_free(err);
    g_assert_null(dsp->replay);
    g_assert_true(dsp->dma.rw_opaque == &io);
    dsp_destroy(dsp);
}

static void test_failures(gconstpointer arg)
{
    unsigned kind = GPOINTER_TO_UINT(arg);
    TestIO io = { 0 };
    DSPState *dsp = make_dsp(&io, false);
    g_autofree char *path =
        g_build_filename(test_directory, "failure.bin", NULL);
    Error *err = NULL;
    if (kind == 0) {
        g_assert_true(g_file_set_contents(path, "keep", 4, NULL));
    }
    g_assert_true(dsp_replay_configure(dsp, path, 0, &err));
    DSPReplay *capture = dsp_replay_begin(dsp, true);
    g_assert_nonnull(capture);
    if (kind == 1) {
        g_autofree uint8_t *payload = g_malloc(DSP_REPLAY_MAX_BYTES + 1);
        dsp->dma.scratch_rw(dsp->dma.rw_opaque, payload, 0,
                            DSP_REPLAY_MAX_BYTES + 1, false);
        g_assert_cmpuint(payload[0], ==, 0x40);
        g_assert_cmpuint(payload[DSP_REPLAY_MAX_BYTES], ==, 0x40);
        g_assert_cmpuint(io.reads, ==, 1);
    } else if (kind == 2) {
        for (unsigned i = 0; i <= DSP_REPLAY_MAX_IO; i++) {
            dsp->dma.scratch_rw(dsp->dma.rw_opaque, NULL, 0, 0, false);
        }
        g_assert_cmpuint(io.reads, ==, DSP_REPLAY_MAX_IO + 1);
    }
    unsigned calls = run_dsp(dsp);
    g_assert_false(dsp_replay_end(capture, calls, &err));
    g_assert_nonnull(err);
    error_free(err);
    g_assert_null(dsp->replay);
    g_assert_true(dsp->dma.rw_opaque == &io);
    g_assert_true(dsp->dma.scratch_rw == scratch_io);
    g_assert_true(dsp->dma.fifo_rw == fifo_io);
    if (kind == 0) {
        g_autofree char *contents = NULL;
        gsize length;
        g_assert_true(g_file_get_contents(path, &contents, &length, NULL));
        g_assert_cmpmem(contents, length, "keep", 4);
        g_unlink(path);
    } else {
        g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));
    }
    dsp_destroy(dsp);
}

static void test_active_cleanup(void)
{
    TestIO io = { 0 };
    DSPState *dsp = make_dsp(&io, false);
    g_autofree char *path = g_build_filename(test_directory, "abort.bin", NULL);
    Error *err = NULL;
    g_assert_true(dsp_replay_configure(dsp, path, 0, &err));
    g_assert_nonnull(dsp_replay_begin(dsp, true));
    dsp_replay_cleanup(dsp);
    g_assert_null(dsp->replay);
    uint8_t value = 0;
    dsp->dma.scratch_rw(dsp->dma.rw_opaque, &value, 0, 1, false);
    g_assert_cmpuint(value, ==, 0x40);
    g_assert_cmpuint(io.reads, ==, 1);
    run_dsp(dsp);
    g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));
    dsp_destroy(dsp);
}

int __wrap_main(int argc, char **argv);
int __wrap_main(int argc, char **argv)
{
    module_call_init(MODULE_INIT_QOM);
    if (argc == 3 && strcmp(argv[1], "--replay") == 0) {
        Error *err = NULL;
        DSPReplayCase *data = dsp_replay_load_case(argv[2], &err);
        bool matched = data && dsp_replay_verify_case(data, &err);
        g_autofree char *program = NULL;
        uint64_t payload = 0;
        if (data) {
            GChecksum *checksum = g_checksum_new(G_CHECKSUM_SHA256);
            for (unsigned i = 0; i < DSP_PRAM_SIZE; i++) {
                uint32_t word =
                    cpu_to_le32(data->before.jit.pram[i] & 0xffffff);
                g_checksum_update(checksum, (const uint8_t *)&word, 4);
            }
            program = g_strdup(g_checksum_get_string(checksum));
            g_checksum_free(checksum);
            for (unsigned i = 0; i < data->io->len; i++) {
                DSPReplayIO *io = g_ptr_array_index(data->io, i);
                payload += g_bytes_get_size(io->bytes);
            }
        }
        printf("{\"matched\":%s,\"calls\":%u,\"transactions\":%u,"
               "\"program_sha256\":\"%s\",\"payload_bytes\":%" PRIu64 ","
               "\"before_cycles\":%u,\"before_halted\":%s,\"halted\":%s,"
               "\"realtime\":%s}\n",
               matched ? "true" : "false", data ? data->calls : 0,
               data ? data->io->len : 0, program ? program : "", payload,
               data ? data->before.jit.cpu.cycle_count : 0,
               data && data->before.jit.cpu.halt_requested ? "true" : "false",
               data && data->after.jit.cpu.halt_requested ? "true" : "false",
               data && data->realtime ? "true" : "false");
        if (err) {
            fprintf(stderr, "%s\n", error_get_pretty(err));
            error_free(err);
        }
        dsp_replay_case_free(data);
        return matched ? 0 : 1;
    }
    test_executable = argv[0];
    g_test_init(&argc, &argv, NULL);
    test_directory = g_dir_make_tmp("xemu-dsp-replay-tests-XXXXXX", NULL);
    g_assert_nonnull(test_directory);
    g_test_add_func("/mcpx-apu/dsp-replay/capture-frame", test_capture_frame);
    g_test_add_func("/mcpx-apu/dsp-replay/dma", test_dma_replay);
    g_test_add_func("/mcpx-apu/dsp-replay/codec", test_codec);
    g_test_add_func("/mcpx-apu/dsp-replay/disabled-delay-backend",
                    test_disabled_delay_backend);
    g_test_add_data_func("/mcpx-apu/dsp-replay/existing-file",
                         GUINT_TO_POINTER(0), test_failures);
    g_test_add_data_func("/mcpx-apu/dsp-replay/byte-overflow",
                         GUINT_TO_POINTER(1), test_failures);
    g_test_add_data_func("/mcpx-apu/dsp-replay/count-overflow",
                         GUINT_TO_POINTER(2), test_failures);
    g_test_add_func("/mcpx-apu/dsp-replay/active-cleanup", test_active_cleanup);
    int result = g_test_run();
    g_rmdir(test_directory);
    g_free(test_directory);
    return result;
}

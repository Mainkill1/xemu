/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Exercises the production JIT adapter against the linked DSP library. */
#include "qemu/osdep.h"
#include "hw/xbox/mcpx/apu/dsp/dsp_internal.h"
#include "hw/xbox/mcpx/apu/dsp/dsp_capture.h"
#include "dsp-capture-compare.h"
#include "dsp-replay-io.h"
#include "dsp-replay-transcript.h"
#include "hw/xbox/mcpx/apu/dsp/capture_wire.h"

static unsigned peripheral_reads;

uint32_t read_peripheral(DSPState *dsp, uint32_t address)
{
    peripheral_reads++;
    return 0;
}

void write_peripheral(DSPState *dsp, uint32_t address, uint32_t value)
{
}
void dsp_start_frame_impl(DSPState *dsp)
{
}

static void test_capture_restore(void)
{
    DSPState dsp = { 0 };
    DspJitCapture *before = g_new0(DspJitCapture, 1);
    DspJitCapture *after = g_new0(DspJitCapture, 1);

    dsp_jit_init(&dsp);
    dsp.ops->reset(&dsp);
    dsp.is_gp = true;
    dsp.dma.control = DMA_CONTROL_RUNNING;
    dsp.dma.dma_read_count = 2;
    dsp.interrupts = 0x81;
    dsp.ops->write_memory(&dsp, 'X', 0xc00, 0x123456);
    dsp.ops->write_memory(&dsp, 'P', 0, 0x000000); /* NOP */

    g_assert_true(dsp_jit_capture(&dsp, before));
    g_assert_cmpuint(peripheral_reads, ==, 0);
    g_assert_cmpuint(dsp.dma.dma_read_count, ==, 2);
    g_assert_cmphex(before->xram[0xc00], ==, 0x123456);
    before->state.interrupts.pending_bits[1] = 1ULL << 37;
    before->state.interrupts.saved_pc = 0x123456;
    before->state.registers[16] = 0xabcdef;
    before->state.stack[1][15] = 0x654321;
    g_assert_true(dsp_jit_restore(&dsp, before));
    dsp.ops->write_memory(&dsp, 'X', 0xc00, 0);
    dsp.dma.dma_read_count = 0;
    g_assert_true(dsp_jit_restore(&dsp, before));
    g_assert_true(dsp_jit_capture(&dsp, after));
    g_assert_cmpuint(peripheral_reads, ==, 0);
    g_assert_cmpuint(after->dma_read_count, ==, 2);
    g_assert_cmphex(after->interrupts, ==, 0x81);
    g_assert_cmphex(after->xram[0xc00], ==, 0x123456);
    g_assert_cmphex(dsp.ops->read_memory(&dsp, 'X', 0x1400), ==, 0x123456);
    g_assert_cmphex(after->state.interrupts.pending_bits[1], ==, 1ULL << 37);
    g_assert_cmphex(after->state.interrupts.saved_pc, ==, 0x123456);
    g_assert_cmphex(after->state.registers[16], ==, 0xabcdef);
    g_assert_cmphex(after->state.stack[1][15], ==, 0x654321);

    dsp.ops->finalize(&dsp);
    g_free(before);
    g_free(after);
}

static void test_repeated_execution(void)
{
    DSPState source = { 0 }, replay = { 0 };
    DspJitCapture *initial = g_new0(DspJitCapture, 1);
    DspJitCapture *expected = g_new0(DspJitCapture, 1);
    DspJitCapture *actual = g_new0(DspJitCapture, 1);
    DspCaptureDifference difference;

    dsp_jit_init(&source);
    dsp_jit_init(&replay);
    source.ops->reset(&source);
    replay.ops->reset(&replay);
    for (unsigned i = 0; i < 32; i++) {
        source.ops->write_memory(&source, 'P', i, 0); /* NOP */
    }
    g_assert_true(dsp_jit_capture(&source, initial));
    initial->state.pc = 0;
    initial->state.halt_requested = false;
    g_assert_true(dsp_jit_restore(&source, initial));
    for (unsigned i = 0; i < 16; i++) {
        source.ops->step(&source);
    }
    g_assert_true(dsp_jit_capture(&source, expected));
    g_assert_cmpuint(expected->state.pc, ==, 16);
    for (unsigned repeat = 0; repeat < 100; repeat++) {
        g_assert_true(dsp_jit_restore(&replay, initial));
        for (unsigned i = 0; i < 16; i++) {
            replay.ops->step(&replay);
        }
        g_assert_true(dsp_jit_capture(&replay, actual));
        if (!dsp_capture_equal(expected, actual, &difference)) {
            g_error("repeat %u: %s[%zu] expected 0x%" PRIx64
                    " actual 0x%" PRIx64,
                    repeat, difference.field, difference.index,
                    difference.expected, difference.actual);
        }
    }
    replay.is_gp = true;
    g_assert_false(dsp_jit_restore(&replay, initial));
    g_assert_false(dsp_jit_capture(NULL, actual));
    g_assert_false(dsp_jit_restore(&replay, NULL));
    source.ops->finalize(&source);
    replay.ops->finalize(&replay);
    g_free(initial);
    g_free(expected);
    g_free(actual);
}

static void test_program_replacement(void)
{
    DSPState dsp = { 0 };
    DspJitCapture *a = g_new0(DspJitCapture, 1);
    DspJitCapture *b = g_new0(DspJitCapture, 1);
    DspJitCapture *result = g_new0(DspJitCapture, 1);

    dsp_jit_init(&dsp);
    dsp.ops->reset(&dsp);
    g_assert_true(dsp_jit_capture(&dsp, a));
    a->state.pc = 0;
    a->state.halt_requested = false;
    a->pram[0] = 0x44f400; /* move immediate to x0 */
    a->pram[1] = 0x123456;
    *b = *a;
    b->pram[1] = 0x654321;
    g_assert_true(dsp_jit_restore(&dsp, a));
    dsp.ops->run(&dsp, 1);
    g_assert_true(dsp_jit_capture(&dsp, result));
    g_assert_cmphex(result->state.registers[DSP_REG_X0], ==, 0x123456);
    g_assert_true(dsp_jit_restore(&dsp, b));
    dsp.ops->run(&dsp, 1);
    g_assert_true(dsp_jit_capture(&dsp, result));
    g_assert_cmphex(result->state.registers[DSP_REG_X0], ==, 0x654321);
    dsp.ops->finalize(&dsp);
    g_free(a);
    g_free(b);
    g_free(result);
}

static void test_first_divergence(void)
{
    DspJitCapture *reference = g_new0(DspJitCapture, 1);
    DspJitCapture *candidate = g_new0(DspJitCapture, 1);
    DspCaptureDifference difference;

    g_assert_true(dsp_capture_equal(reference, candidate, &difference));
    candidate->xram[17] = 0x123456;
    g_assert_false(dsp_capture_equal(reference, candidate, &difference));
    g_assert_cmpstr(difference.field, ==, "xram");
    g_assert_cmpuint(difference.index, ==, 17);
    g_assert_cmphex(difference.expected, ==, 0);
    g_assert_cmphex(difference.actual, ==, 0x123456);
    candidate->xram[17] = 0;
    candidate->state.interrupts.pending_bits[1] = 1ULL << 37;
    g_assert_false(dsp_capture_equal(reference, candidate, &difference));
    g_assert_cmpstr(difference.field, ==, "interrupt.pending_bits");
    g_assert_cmpuint(difference.index, ==, 1);
    g_assert_cmphex(difference.actual, ==, 1ULL << 37);
    candidate->state.interrupts.pending_bits[1] = 0;
    candidate->dma_read_count = 2;
    g_assert_false(dsp_capture_equal(reference, candidate, &difference));
    g_assert_cmpstr(difference.field, ==, "dma_read_count");
    g_assert_cmpuint(difference.actual, ==, 2);
    g_free(reference);
    g_free(candidate);
}

static void test_ordered_io(void)
{
    DspIoEvent events[2] = { 0 };
    uint8_t payload[8] = { 0 };
    uint8_t input[] = { 1, 2, 3 }, output[] = { 4, 5 };
    uint8_t received[] = { 0, 0, 0 };
    DspIoLog log;

    dsp_io_record_init(&log, events, G_N_ELEMENTS(events), payload,
                       sizeof(payload));
    g_assert_true(dsp_io_record(&log, DSP_IO_SCRATCH, 0, 4, false, 32, input,
                                sizeof(input)));
    g_assert_true(dsp_io_record(&log, DSP_IO_FIFO, 2, 0, true, 33, output,
                                sizeof(output)));
    g_assert_true(dsp_io_complete(&log));
    dsp_io_replay_init(&log);
    g_assert_true(dsp_io_replay(&log, DSP_IO_SCRATCH, 0, 4, false, 32, received,
                                sizeof(received)));
    g_assert_cmpmem(received, sizeof(received), input, sizeof(input));
    g_assert_false(dsp_io_complete(&log)); /* required FIFO write remains */
    g_assert_true(dsp_io_replay(&log, DSP_IO_FIFO, 2, 0, true, 33, output,
                                sizeof(output)));
    g_assert_true(dsp_io_complete(&log));

    dsp_io_replay_init(&log);
    memset(received, 0xa5, sizeof(received));
    g_assert_false(dsp_io_replay(&log, DSP_IO_SCRATCH, 0, 5, false, 32,
                                 received, sizeof(received)));
    g_assert_cmphex(received[0], ==, 0xa5); /* mismatch does not supply input */
    g_assert_false(dsp_io_complete(&log));
    g_assert_cmpuint(log.cursor, ==, 0);

    dsp_io_replay_init(&log);
    g_assert_true(dsp_io_replay(&log, DSP_IO_SCRATCH, 0, 4, false, 32, received,
                                sizeof(received)));
    output[1] = 6;
    g_assert_false(dsp_io_replay(&log, DSP_IO_FIFO, 2, 0, true, 33, output,
                                 sizeof(output)));
    g_assert_cmpuint(log.cursor, ==, 1);
    g_assert_false(dsp_io_complete(&log));
}

static void test_io_observed_read_is_not_replaced(void)
{
    DspIoEvent event[1];
    uint8_t payload[3], recorded[] = { 1, 2, 3 };
    uint8_t observed[] = { 1, 2, 4 };
    DspIoLog log;
    dsp_io_record_init(&log, event, 1, payload, sizeof(payload));
    g_assert_true(dsp_io_record(&log, DSP_IO_SCRATCH, 1, 4, false, 32, recorded,
                                sizeof(recorded)));
    dsp_io_replay_init(&log);
    g_assert_false(dsp_io_expect(&log, DSP_IO_SCRATCH, 1, 4, false, 32,
                                 observed, sizeof(observed)));
    g_assert_cmphex(observed[2], ==, 4);
    g_assert_cmpuint(log.cursor, ==, 0);
    dsp_io_replay_init(&log);
    g_assert_true(dsp_io_expect(&log, DSP_IO_SCRATCH, 1, 4, false, 32, recorded,
                                sizeof(recorded)));
    g_assert_true(dsp_io_complete(&log));
}

static void test_io_overflow_is_incomplete(void)
{
    DspIoEvent event[1] = { 0 };
    uint8_t payload[2] = { 0 }, data[3] = { 1, 2, 3 };
    DspIoLog log;

    dsp_io_record_init(&log, event, 1, payload, sizeof(payload));
    g_assert_false(dsp_io_record(&log, DSP_IO_SCRATCH, 0, 0, false, 0, data,
                                 sizeof(data)));
    g_assert_false(dsp_io_complete(&log));
    g_assert_cmpuint(log.count, ==, 0);
    g_assert_cmpuint(log.bytes_used, ==, 0);
    dsp_io_replay_init(&log);
    g_assert_false(dsp_io_replay(&log, DSP_IO_SCRATCH, 0, 0, false, 0, data,
                                 sizeof(data)));
    g_assert_false(dsp_io_complete(&log));
}

static void test_io_corrupt_bounds_are_rejected(void)
{
    DspIoEvent event[1] = { 0 };
    uint8_t payload[2] = { 0 }, data[2] = { 1, 2 };
    DspIoLog log;

    dsp_io_record_init(&log, event, 1, payload, sizeof(payload));
    g_assert_true(dsp_io_record(&log, DSP_IO_SCRATCH, 0, 0, false, 0, data,
                                sizeof(data)));
    dsp_io_replay_init(&log);
    log.bytes_used = 3; /* corrupt or untrusted imported metadata */
    g_assert_false(dsp_io_replay(&log, DSP_IO_SCRATCH, 0, 0, false, 0, data,
                                 sizeof(data)));
    g_assert_false(dsp_io_complete(&log));
}

static void test_transcript_decode(void)
{
    /* XIO1, one scratch write, index 0, address 12, sample 0, two bytes. */
    const uint8_t bytes[] = {
        'X', 'I', 'O', '1', 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,    12,
        0,   0,   0,   0,   0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0xaa, 0x55,
    };
    uint8_t bad[sizeof(bytes)], payload[4], data[2] = { 0xaa, 0x55 };
    DspIoEvent events[2];
    DspIoLog io;
    g_assert_true(dsp_io_decode(bytes, sizeof(bytes), &io, events, 2, payload,
                                sizeof(payload)));
    g_assert_true(
        dsp_io_replay(&io, DSP_IO_SCRATCH, 0, 12, true, 0, data, sizeof(data)));
    g_assert_true(dsp_io_complete(&io));
    g_assert_false(dsp_io_decode(bytes, sizeof(bytes) - 1, &io, events, 2,
                                 payload, sizeof(payload)));
    g_assert_false(dsp_io_complete(&io));
    memcpy(bad, bytes, sizeof(bytes));
    bad[10] = 1; /* reserved bytes must be zero */
    g_assert_false(dsp_io_decode(bad, sizeof(bad), &io, events, 2, payload,
                                 sizeof(payload)));
    g_assert_false(dsp_io_complete(&io));
    memcpy(bad, bytes, sizeof(bytes));
    bad[28] = 0xff; /* length exceeds the available payload */
    g_assert_false(dsp_io_decode(bad, sizeof(bad), &io, events, 2, payload,
                                 sizeof(payload)));
}

static void test_wire_roundtrip(void)
{
    DspJitCapture *source = g_new0(DspJitCapture, 1);
    DspJitCapture *decoded = g_new0(DspJitCapture, 1);
    DspJitCapture *unchanged = g_new0(DspJitCapture, 1);
    DspCaptureDifference difference;
    size_t size = dsp_jit_wire_size();
    uint8_t *bytes = g_malloc0(size + 1);

    source->state.pc = 0x123456;
    source->state.pc_on_rep = true;
    source->state.cycle_budget = -7;
    source->state.interrupts.pending_bits[1] = 1ULL << 57;
    source->state.interrupts.ipl[127] = -1;
    source->state.stack[1][15] = 0xabcdef;
    source->xram[4095] = 0x88776655;
    source->yram[2047] = 0xaabbccdd;
    source->pram[4095] = 0x123456;
    source->dma_read_count = 2;
    source->eol = true;
    source->is_gp = true;
    g_assert_true(dsp_jit_wire_encode(source, bytes, size));
    g_assert_cmpmem(bytes, 4, "XDJT", 4);
    g_assert_cmpuint(bytes[8], ==, 0x56);
    g_assert_cmpuint(bytes[9], ==, 0x34);
    g_assert_cmpuint(bytes[10], ==, 0x12);
    g_assert_cmpuint(bytes[11], ==, 0);
    g_assert_true(dsp_jit_wire_decode(bytes, size, decoded));
    g_assert_true(dsp_capture_equal(source, decoded, &difference));
    *unchanged = *decoded;
    g_assert_cmpuint(size, ==, 41554);
    g_assert_false(dsp_jit_wire_decode(bytes, size - 1, decoded));
    g_assert_true(dsp_capture_equal(unchanged, decoded, &difference));
    g_assert_false(dsp_jit_wire_decode(bytes, size + 1, decoded));
    bytes[size - 1] = 2; /* invalid boolean */
    g_assert_false(dsp_jit_wire_decode(bytes, size, decoded));
    g_assert_true(dsp_capture_equal(unchanged, decoded, &difference));
    bytes[size - 1] = 1;
    bytes[27] = 255; /* unsupported power state is not silently normalized */
    g_assert_false(dsp_jit_wire_decode(bytes, size, decoded));
    g_assert_true(dsp_capture_equal(unchanged, decoded, &difference));
    bytes[27] = 0;
    bytes[53] = 255; /* invalid interrupt state */
    g_assert_false(dsp_jit_wire_decode(bytes, size, decoded));
    g_assert_true(dsp_capture_equal(unchanged, decoded, &difference));
    bytes[53] = 0;
    bytes[4] = 2; /* unsupported format version */
    g_assert_false(dsp_jit_wire_decode(bytes, size, decoded));
    g_assert_true(dsp_capture_equal(unchanged, decoded, &difference));
    g_free(source);
    g_free(decoded);
    g_free(unchanged);
    g_free(bytes);
}

static void test_wire_seeded_roundtrips(void)
{
    GRand *random = g_rand_new_with_seed(0x383);
    DspJitCapture *source = g_new0(DspJitCapture, 1);
    DspJitCapture *decoded = g_new0(DspJitCapture, 1);
    DspCaptureDifference difference;
    size_t size = dsp_jit_wire_size();
    uint8_t *bytes = g_malloc(size);

    for (unsigned run = 0; run < 100; run++) {
        source->state.pc = g_rand_int(random);
        source->state.pc_advance = g_rand_int(random);
        source->state.cycle_count = g_rand_int(random);
        source->state.cycle_budget = g_rand_int_range(random, -1000, 1000);
        source->state.pc_on_rep = g_rand_int_range(random, 0, 2);
        source->state.loop_rep = g_rand_int_range(random, 0, 2);
        source->state.halt_requested = g_rand_int_range(random, 0, 2);
        source->state.power_state = g_rand_int_range(random, 0, 3);
        source->state.interrupts.state = g_rand_int_range(random, 0, 3);
        source->state.interrupts.pipeline_stage = g_rand_int(random);
        source->state.interrupts.ipl_to_raise = g_rand_int(random);
        source->state.interrupts.vector_addr = g_rand_int(random);
        source->state.interrupts.saved_pc = g_rand_int(random);
        for (size_t i = 0; i < 2; i++) {
            source->state.interrupts.pending_bits[i] =
                ((uint64_t)g_rand_int(random) << 32) | g_rand_int(random);
        }
        for (size_t i = 0; i < 128; i++) {
            source->state.interrupts.ipl[i] = g_rand_int_range(random, -1, 4);
        }
        for (size_t i = 0; i < 64; i++) {
            source->state.registers[i] = g_rand_int(random);
        }
        for (size_t b = 0; b < 2; b++) {
            for (size_t i = 0; i < 16; i++) {
                source->state.stack[b][i] = g_rand_int(random);
            }
        }
        for (size_t i = 0; i < DSP_XRAM_SIZE; i++) {
            source->xram[i] = g_rand_int(random);
        }
        for (size_t i = 0; i < DSP_YRAM_SIZE; i++) {
            source->yram[i] = g_rand_int(random);
        }
        for (size_t i = 0; i < DSP_PRAM_SIZE; i++) {
            source->pram[i] = g_rand_int(random);
        }
        source->configuration = g_rand_int(random);
        source->control = g_rand_int(random);
        source->start_block = g_rand_int(random);
        source->next_block = g_rand_int(random);
        source->dma_read_count = g_rand_int(random);
        source->interrupts = g_rand_int(random);
        source->error = g_rand_int_range(random, 0, 2);
        source->eol = g_rand_int_range(random, 0, 2);
        source->is_gp = g_rand_int_range(random, 0, 2);
        g_assert_true(dsp_jit_wire_encode(source, bytes, size));
        g_assert_true(dsp_jit_wire_decode(bytes, size, decoded));
        if (!dsp_capture_equal(source, decoded, &difference)) {
            g_error("seed 0x383 iteration %u: %s[%zu]", run, difference.field,
                    difference.index);
        }
    }
    g_rand_free(random);
    g_free(source);
    g_free(decoded);
    g_free(bytes);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/dsp/jit-capture/restore", test_capture_restore);
    g_test_add_func("/dsp/jit-capture/repeated-execution",
                    test_repeated_execution);
    g_test_add_func("/dsp/jit-capture/program-replacement",
                    test_program_replacement);
    g_test_add_func("/dsp/jit-capture/first-divergence", test_first_divergence);
    g_test_add_func("/dsp/replay/ordered-io", test_ordered_io);
    g_test_add_func("/dsp/replay/observed-read",
                    test_io_observed_read_is_not_replaced);
    g_test_add_func("/dsp/replay/io-overflow", test_io_overflow_is_incomplete);
    g_test_add_func("/dsp/replay/io-corrupt-bounds",
                    test_io_corrupt_bounds_are_rejected);
    g_test_add_func("/dsp/replay/transcript-decode", test_transcript_decode);
    g_test_add_func("/dsp/replay/wire-roundtrip", test_wire_roundtrip);
    g_test_add_func("/dsp/replay/wire-seeded-roundtrips",
                    test_wire_seeded_roundtrips);
    return g_test_run();
}

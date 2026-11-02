/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Production DSP/DMA integration: independent memory and ordered effects. */
#include "qemu/osdep.h"
#include "hw/xbox/mcpx/apu/apu_int.h"
#include "hw/xbox/mcpx/apu/dsp/dsp.h"
#include "hw/xbox/mcpx/apu/dsp/dsp_capture.h"
#include "hw/xbox/mcpx/apu/dsp/capture_ring.h"
#include "hw/xbox/mcpx/apu/dsp/capture_stream.h"
#include <glib/gstdio.h>
#include "hw/xbox/mcpx/apu/dsp/dsp_dma_regs.h"
#include "ui/xemu-settings.h"
#include "dsp-capture-compare.h"
#include "hw/xbox/mcpx/apu/dsp/capture_wire.h"
#include "dsp-replay-io.h"
#include "dsp-replay-core.h"
#include "dsp-replay-frame.h"
#include "dsp-replay-registers.h"
#include "qemu/module.h"
#include "exec/cpu-common.h"
#include "system/address-spaces.h"
#include "system/cpus.h"
#include "hw/boards.h"

typedef struct ReplayMemory {
    uint8_t bytes[64];
    unsigned calls;
    uint32_t address;
    size_t length;
    bool direction;
    DspIoLog *log;
} ReplayMemory;

static void scratch(void *opaque, uint8_t *data, uint32_t address, size_t len,
                    bool direction)
{
    ReplayMemory *mem = opaque;
    g_assert_cmpuint(address, <=, sizeof(mem->bytes));
    g_assert_cmpuint(len, <=, sizeof(mem->bytes) - address);
    mem->calls++;
    mem->address = address;
    mem->length = len;
    mem->direction = direction;
    if (mem->log && !mem->log->recording) {
        uint8_t recorded[sizeof(mem->bytes)];
        g_assert_cmpuint(len, <=, sizeof(recorded));
        if (direction) {
            g_assert_true(dsp_io_replay(mem->log, DSP_IO_SCRATCH, 0, address,
                                        true, 0, data, len));
        } else {
            g_assert_true(dsp_io_replay(mem->log, DSP_IO_SCRATCH, 0, address,
                                        false, 0, recorded, len));
            /* A separate memory model must agree with the recorded read. */
            g_assert_cmpmem(recorded, len, mem->bytes + address, len);
        }
    }
    if (direction) {
        memcpy(mem->bytes + address, data, len);
    } else {
        memcpy(data, mem->bytes + address, len);
    }
    if (mem->log && mem->log->recording) {
        g_assert_true(dsp_io_record(mem->log, DSP_IO_SCRATCH, 0, address,
                                    direction, 0, data, len));
    }
}

static void fifo(void *opaque, uint8_t *data, unsigned index, size_t len,
                 bool direction)
{
    g_error("unexpected FIFO access");
}

static void test_dma_replay(gconstpointer arg)
{
    const bool is_gp = GPOINTER_TO_INT(arg);
    ReplayMemory source_memory = { 0 }, replay_memory = { 0 };
    g_config.audio.use_dsp_jit = true;
    DSPState *source = dsp_init(&source_memory, scratch, fifo, is_gp);
    DSPState *replay = dsp_init(&replay_memory, scratch, fifo, is_gp);
    DspJitCapture *initial = g_new0(DspJitCapture, 1);
    DspJitCapture *after = g_new0(DspJitCapture, 1);
    DspJitCapture *expected = g_new0(DspJitCapture, 1);
    DspCaptureDifference difference;
    uint32_t words[2];
    DspIoEvent events[4];
    uint8_t payload[64];
    DspIoLog io;

    /* One 24-bit, two-word DSP-to-scratch transfer at guest byte 12. */
    dsp_write_memory(source, 'X', 0x100, NODE_POINTER_EOL);
    dsp_write_memory(source, 'X', 0x101,
                     (6 << 10) | (15 << 5) | NODE_CONTROL_DIRECTION);
    dsp_write_memory(source, 'X', 0x102, 2);
    dsp_write_memory(source, 'X', 0x103, 0x200);
    dsp_write_memory(source, 'X', 0x104, 4);
    dsp_write_memory(source, 'X', 0x105, 8);
    dsp_write_memory(source, 'X', 0x106, 31);
    dsp_write_memory(source, 'X', 0x200, 0x123456);
    dsp_write_memory(source, 'X', 0x201, 0x654321);
    dsp_write_memory(source, 'X', 0xffffd4, 0x100);
    /* movep #1,X:$FFFFD6 starts DMA; movep #1,X:$FFFFC4 halts. */
    dsp_write_memory(source, 'P', 0, 0x08f496);
    dsp_write_memory(source, 'P', 1, DMA_CONTROL_ACTION_START);
    dsp_write_memory(source, 'P', 2, 0x08f484);
    dsp_write_memory(source, 'P', 3, 1);
    g_assert_true(dsp_jit_capture(source, initial));
    initial->state.pc = 0;
    initial->state.halt_requested = false;
    g_assert_cmpuint(source_memory.calls, ==, 0);
    dsp_io_record_init(&io, events, G_N_ELEMENTS(events), payload,
                       sizeof(payload));
    source_memory.log = &io;
    g_assert_true(dsp_jit_restore(source, initial));
    dsp_run(source, 100);
    g_assert_true(dsp_get_halt_requested(source));
    g_assert_true(dsp_io_complete(&io));
    g_assert_cmpuint(io.count, ==, 1);
    g_assert_true(dsp_jit_capture(source, expected));

    for (unsigned run = 0; run < 100; run++) {
        memset(&replay_memory, 0, sizeof(replay_memory));
        replay_memory.log = &io;
        dsp_io_replay_init(&io);
        g_assert_true(dsp_jit_restore(replay, initial));
        dsp_run(replay, 100);
        g_assert_true(dsp_get_halt_requested(replay));
        g_assert_cmpuint(replay_memory.calls, ==, 1);
        g_assert_cmpuint(replay_memory.address, ==, 12);
        g_assert_cmpuint(replay_memory.length, ==, 8);
        g_assert_true(replay_memory.direction);
        memcpy(words, replay_memory.bytes + 12, sizeof(words));
        g_assert_cmphex(words[0], ==, 0x123456);
        g_assert_cmphex(words[1], ==, 0x654321);
        g_assert_true(dsp_jit_capture(replay, after));
        if (!dsp_capture_equal(expected, after, &difference)) {
            g_error("repeat %u: %s[%zu] expected 0x%" PRIx64
                    " actual 0x%" PRIx64,
                    run, difference.field, difference.index,
                    difference.expected, difference.actual);
        }
        g_assert_true(dsp_io_complete(&io));
        g_assert_true(after->eol);
        g_assert_cmpuint(after->dma_read_count, ==, 0);
        g_assert_true(dsp_read_memory(replay, 'X', 0xffffd6) &
                      DMA_CONTROL_RUNNING);
        g_assert_true(dsp_read_memory(replay, 'X', 0xffffd6) &
                      DMA_CONTROL_RUNNING);
        g_assert_true(dsp_read_memory(replay, 'X', 0xffffd6) &
                      DMA_CONTROL_STOPPED);
    }
    g_assert_cmpuint(source_memory.calls, ==, 1);
    dsp_destroy(source);
    dsp_destroy(replay);
    g_free(initial);
    g_free(after);
    g_free(expected);
}

/* Independent fresh backends receive the same bounded synthetic program and
 * scratch input. Compare observed effects, not their incompatible native
 * checkpoint layouts; neither engine is a hardware oracle. */
static void test_jit_and_interpreter_dma_effects(void)
{
    if (!g_test_subprocess()) {
        g_test_trap_subprocess(NULL, 10 * G_USEC_PER_SEC, 0);
        g_test_trap_assert_passed();
        return;
    }
    ReplayMemory memories[2] = { 0 };
    DspIoEvent events[2][2];
    uint8_t payload[2][32];
    DspIoLog logs[2];
    DSPState *engines[2];
    for (unsigned i = 0; i < 2; i++) {
        g_config.audio.use_dsp_jit = i == 0;
        dsp_io_record_init(&logs[i], events[i], G_N_ELEMENTS(events[i]),
                           payload[i], sizeof(payload[i]));
        memories[i].log = &logs[i];
        engines[i] = dsp_init(&memories[i], scratch, fifo, true);
        const uint32_t node[] = { NODE_POINTER_EOL,
                                  (6 << 10) | (15 << 5) |
                                      NODE_CONTROL_DIRECTION,
                                  2,
                                  0x200,
                                  4,
                                  8,
                                  31 };
        for (unsigned j = 0; j < G_N_ELEMENTS(node); j++) {
            dsp_write_memory(engines[i], 'X', 0x100 + j, node[j]);
        }
        dsp_write_memory(engines[i], 'X', 0x200, 0x123456);
        dsp_write_memory(engines[i], 'X', 0x201, 0x654321);
        dsp_write_memory(engines[i], 'X', 0xffffd4, 0x100);
        const uint32_t program[] = { 0x08f496, DMA_CONTROL_ACTION_START,
                                     0x08f484, 1 };
        for (unsigned j = 0; j < G_N_ELEMENTS(program); j++) {
            dsp_write_memory(engines[i], 'P', j, program[j]);
        }
        dsp_set_halt_requested(engines[i], false);
        dsp_run(engines[i], 1000);
        g_assert_true(dsp_get_halt_requested(engines[i]));
        g_assert_true(dsp_io_complete(&logs[i]));
        g_assert_cmpuint(logs[i].count, ==, 1);
    }
    g_assert_cmpuint(memories[0].calls, ==, memories[1].calls);
    g_assert_cmpmem(memories[0].bytes, sizeof(memories[0].bytes),
                    memories[1].bytes, sizeof(memories[1].bytes));
    g_assert_cmpuint(events[0][0].kind, ==, events[1][0].kind);
    g_assert_cmpuint(events[0][0].index, ==, events[1][0].index);
    g_assert_cmpuint(events[0][0].address, ==, events[1][0].address);
    g_assert_cmpuint(events[0][0].length, ==, events[1][0].length);
    g_assert_cmpint(events[0][0].write, ==, events[1][0].write);
    g_assert_cmpmem(payload[0], logs[0].bytes_used, payload[1],
                    logs[1].bytes_used);
    g_assert_cmpuint(engines[0]->interrupts, ==, engines[1]->interrupts);
    g_assert_cmpuint(engines[0]->dma.control, ==, engines[1]->dma.control);
    g_assert_cmpint(engines[0]->dma.eol, ==, engines[1]->dma.eol);
    for (unsigned i = 0; i < 2; i++) {
        dsp_destroy(engines[i]);
    }
}

static void test_jit_and_interpreter_stack_execution(void)
{
    if (!g_test_subprocess()) {
        g_test_trap_subprocess(NULL, 10 * G_USEC_PER_SEC, 0);
        g_test_trap_assert_passed();
        return;
    }
    ReplayMemory memories[2] = { 0 };
    DSPState *engines[2];
    /* JSR P:$5 pushes return PC 1; RTS returns to the halt instruction. */
    const uint32_t program[] = { 0x0d0005, 0x08f484, 1, 0, 0, 0x00000c };
    for (unsigned i = 0; i < 2; i++) {
        g_config.audio.use_dsp_jit = i == 0;
        engines[i] = dsp_init(&memories[i], scratch, fifo, true);
        for (unsigned j = 0; j < G_N_ELEMENTS(program); j++) {
            dsp_write_memory(engines[i], 'P', j, program[j]);
        }
        if (i == 0) {
            DspJitCapture *initial = g_new0(DspJitCapture, 1);
            g_assert_true(dsp_jit_capture(engines[i], initial));
            initial->state.registers[DSP_REG_SR] = 0;
            initial->state.registers[DSP_REG_OMR] = 2;
            g_assert_true(dsp_jit_restore(engines[i], initial));
            g_free(initial);
        }
        dsp_set_halt_requested(engines[i], false);
        dsp_run(engines[i], 1000);
        g_assert_true(dsp_get_halt_requested(engines[i]));
    }
    DspJitCapture *jit = g_new0(DspJitCapture, 1);
    DspCCapture *interp = g_new0(DspCCapture, 1);
    g_assert_true(dsp_jit_capture(engines[0], jit));
    g_assert_true(dsp_c_capture(engines[1], interp));
    g_assert_cmphex(jit->state.stack[0][1], ==, 1);
    g_assert_cmphex(interp->core.stack[0][1], ==, 1);
    g_assert_cmphex(jit->state.registers[DSP_REG_SR], ==,
                    interp->core.registers[DSP_REG_SR]);
    g_assert_cmphex(jit->state.registers[DSP_REG_OMR], ==,
                    interp->core.registers[DSP_REG_OMR]);
    for (unsigned bank = 0; bank < 2; bank++) {
        for (unsigned index = 0; index < 16; index++) {
            if (jit->state.stack[bank][index] !=
                interp->core.stack[bank][index]) {
                g_printerr(
                    "stack[%u][%u]: jit=%06x c=%06x; SR jit=%06x c=%06x\n",
                    bank, index, jit->state.stack[bank][index],
                    interp->core.stack[bank][index],
                    jit->state.registers[DSP_REG_SR],
                    interp->core.registers[DSP_REG_SR]);
            }
            g_assert_cmphex(jit->state.stack[bank][index], ==,
                            interp->core.stack[bank][index]);
        }
    }
    g_free(jit);
    g_free(interp);
    dsp_destroy(engines[0]);
    dsp_destroy(engines[1]);
}

static void test_interrupt_underflow_replay(gconstpointer arg)
{
    int mode = GPOINTER_TO_INT(arg);
    bool jit = mode == 1;
    bool disable_assertion = mode == 2;
    if (!g_test_subprocess()) {
        g_test_trap_subprocess(NULL, 10 * G_USEC_PER_SEC, 0);
#ifndef NDEBUG
        if (!jit && !disable_assertion) {
            /* The interpreter intentionally asserts with exception_debugging
             * enabled. */
            g_test_trap_assert_failed();
            g_test_trap_assert_stderr("*Dsp stack underflow*");
            return;
        }
#endif
        g_test_trap_assert_passed();
        return;
    }
    ReplayMemory memories[2] = { 0 };
    DSPState *engines[2];
    g_config.audio.use_dsp_jit = jit;
    for (unsigned i = 0; i < 2; i++) {
        engines[i] = dsp_init(&memories[i], scratch, fifo, true);
        for (unsigned address = 0; address < 0x40; address++) {
            dsp_write_memory(engines[i], 'P', address,
                             0); /* NOPs, including vectors */
        }
        dsp_write_memory(engines[i], 'P', 0x20, 0x00000c); /* RTS at SP=0 */
        if (jit) {
            DspJitCapture *initial = g_new0(DspJitCapture, 1);
            g_assert_true(dsp_jit_capture(engines[i], initial));
            initial->state.pc = 0x20;
            initial->state.registers[DSP_REG_SR] = 0;
            initial->state.registers[DSP_REG_OMR] = 2;
            initial->state.registers[DSP_REG_SP] = 0;
            initial->state.halt_requested = false;
            g_assert_cmpuint(initial->state.interrupts.state, ==, 0);
            g_assert_cmphex(initial->state.interrupts.pending_bits[0], ==, 0);
            g_assert_cmphex(initial->state.interrupts.pending_bits[1], ==, 0);
            g_assert_true(dsp_jit_restore(engines[i], initial));
            g_free(initial);
        } else {
            DspCCapture *initial = g_new0(DspCCapture, 1);
            g_assert_true(dsp_c_capture(engines[i], initial));
            g_assert_true(initial->exception_debugging);
            initial->exception_debugging = !disable_assertion;
            initial->core.pc = 0x20;
            initial->core.registers[DSP_REG_SR] = 0;
            initial->core.registers[DSP_REG_OMR] = 2;
            initial->core.registers[DSP_REG_SP] = 0;
            initial->core.halt_requested = false;
            g_assert_cmpuint(initial->core.interrupt_state, ==, 0);
            g_assert_cmpuint(initial->core.interrupt_counter, ==, 0);
            g_assert_true(dsp_c_restore(engines[i], initial));
            g_assert_true(dsp_c_capture(engines[i], initial));
            g_assert_cmpint(initial->exception_debugging, ==,
                            !disable_assertion);
            g_free(initial);
        }
    }
    bool saw_interrupt = false;
    for (unsigned step = 0; step < 10; step++) {
        dsp_step(engines[0]);
        dsp_step(engines[1]);
        DspCaptureDifference difference;
        if (jit) {
            DspJitCapture *a = g_new0(DspJitCapture, 1),
                          *b = g_new0(DspJitCapture, 1);
            g_assert_true(dsp_jit_capture(engines[0], a));
            if (step == 2) {
                g_assert_true(a->state.interrupts.state != 0 ||
                              a->state.interrupts.pending_bits[0] != 0 ||
                              a->state.interrupts.pending_bits[1] != 0);
                dsp_write_memory(engines[1], 'X', 0x200, 0x123456);
                g_assert_true(dsp_jit_restore(engines[1], a));
            }
            g_assert_true(dsp_jit_capture(engines[1], b));
            g_assert_true(dsp_capture_equal(a, b, &difference));
            saw_interrupt |= a->state.interrupts.state != 0 ||
                             a->state.interrupts.pending_bits[0] != 0 ||
                             a->state.interrupts.pending_bits[1] != 0;
            g_free(a);
            g_free(b);
        } else {
            DspCCapture *a = g_new0(DspCCapture, 1),
                        *b = g_new0(DspCCapture, 1);
            g_assert_true(dsp_c_capture(engines[0], a));
            if (step == 2) {
                g_assert_true(a->core.interrupt_state != 0 ||
                              a->core.interrupt_counter != 0);
                dsp_write_memory(engines[1], 'X', 0x200, 0x123456);
                g_assert_true(dsp_c_restore(engines[1], a));
            }
            g_assert_true(dsp_c_capture(engines[1], b));
            g_assert_true(dsp_c_capture_equal(a, b, &difference));
            saw_interrupt |=
                a->core.interrupt_state != 0 || a->core.interrupt_counter != 0;
            g_free(a);
            g_free(b);
        }
    }
    g_assert_true(saw_interrupt);
    dsp_destroy(engines[0]);
    dsp_destroy(engines[1]);
}

static void test_gp_ep_frame(void)
{
    MCPXAPUState *apu = g_new0(MCPXAPUState, 1);
    ReplayMemory gp_memory = { 0 }, ep_memory = { 0 };
    DspJitCapture *gp_initial = g_new0(DspJitCapture, 1);
    DspJitCapture *ep_initial = g_new0(DspJitCapture, 1);
    float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME] = { { 0 } };

    g_config.audio.use_dsp_jit = true;
    apu->gp.dsp = dsp_init(&gp_memory, scratch, fifo, true);
    apu->ep.dsp = dsp_init(&ep_memory, scratch, fifo, false);
    dsp_write_memory(apu->gp.dsp, 'P', 0, 0x08f484); /* halt */
    dsp_write_memory(apu->gp.dsp, 'P', 1, 1);
    dsp_write_memory(apu->ep.dsp, 'P', 0, 0x08f484);
    dsp_write_memory(apu->ep.dsp, 'P', 1, 1);
    g_assert_true(dsp_jit_capture(apu->gp.dsp, gp_initial));
    g_assert_true(dsp_jit_capture(apu->ep.dsp, ep_initial));
    gp_initial->state.pc = ep_initial->state.pc = 0;
    gp_initial->state.halt_requested = ep_initial->state.halt_requested = false;
    apu->gp.regs[NV_PAPU_GPRST] = NV_PAPU_GPRST_GPRST | NV_PAPU_GPRST_GPDSPRST;
    apu->ep.regs[NV_PAPU_EPRST] = NV_PAPU_GPRST_GPRST | NV_PAPU_GPRST_GPDSPRST;
    apu->monitor.point = MCPX_APU_DEBUG_MON_GP;
    mixbins[0][0] = 0.5f;
    mixbins[1][0] = -0.5f;

    for (unsigned run = 0; run < 100; run++) {
        g_assert_true(dsp_jit_restore(apu->gp.dsp, gp_initial));
        g_assert_true(dsp_jit_restore(apu->ep.dsp, ep_initial));
        apu->ep_frame_div = run % 8;
        memset(apu->monitor.frame_buf, 0, sizeof(apu->monitor.frame_buf));
        mcpx_apu_dsp_frame(apu, mixbins);
        unsigned offset = (run % 8) * NUM_SAMPLES_PER_FRAME;
        g_assert_cmpint(apu->monitor.frame_buf[offset][0], ==, 0x4000);
        g_assert_cmpint(apu->monitor.frame_buf[offset][1], ==, -0x4000);
        g_assert_cmpint(apu->monitor.frame_buf[offset + 1][0], ==, 0);
        g_assert_true(dsp_get_halt_requested(apu->gp.dsp));
        g_assert_cmpint(dsp_get_halt_requested(apu->ep.dsp), ==, run % 8 == 0);
        g_assert_cmpuint(gp_memory.calls, ==, 0);
        g_assert_cmpuint(ep_memory.calls, ==, 0);
    }
    dsp_destroy(apu->gp.dsp);
    dsp_destroy(apu->ep.dsp);
    g_free(gp_initial);
    g_free(ep_initial);
    g_free(apu);
}

static void test_dma_read_replay(void)
{
    ReplayMemory source_memory = { 0 }, replay_memory = { 0 };
    const uint32_t input[2] = { 0x112233, 0x445566 };
    DspIoEvent events[2];
    uint8_t payload[32];
    DspIoLog io;
    g_config.audio.use_dsp_jit = true;
    DSPState *source = dsp_init(&source_memory, scratch, fifo, true);
    DSPState *replay = dsp_init(&replay_memory, scratch, fifo, true);
    DspJitCapture *initial = g_new0(DspJitCapture, 1);
    DspJitCapture *expected = g_new0(DspJitCapture, 1);
    DspJitCapture *actual = g_new0(DspJitCapture, 1);
    DspCaptureDifference difference;
    size_t wire_size = dsp_jit_wire_size();
    uint8_t *wire = g_malloc(wire_size);

    dsp_write_memory(source, 'X', 0x100, NODE_POINTER_EOL);
    dsp_write_memory(source, 'X', 0x101, (6 << 10) | (15 << 5));
    dsp_write_memory(source, 'X', 0x102, 2);
    dsp_write_memory(source, 'X', 0x103, 0x200);
    dsp_write_memory(source, 'X', 0x104, 4);
    dsp_write_memory(source, 'X', 0x105, 8);
    dsp_write_memory(source, 'X', 0x106, 31);
    dsp_write_memory(source, 'X', 0xffffd4, 0x100);
    dsp_write_memory(source, 'P', 0, 0x08f496);
    dsp_write_memory(source, 'P', 1, DMA_CONTROL_ACTION_START);
    dsp_write_memory(source, 'P', 2, 0x08f484);
    dsp_write_memory(source, 'P', 3, 1);
    g_assert_true(dsp_jit_capture(source, initial));
    initial->state.pc = 0;
    initial->state.halt_requested = false;
    g_assert_true(dsp_jit_wire_encode(initial, wire, wire_size));
    memset(initial, 0, sizeof(*initial));
    g_assert_true(dsp_jit_wire_decode(wire, wire_size, initial));
    memcpy(source_memory.bytes + 12, input, sizeof(input));
    dsp_io_record_init(&io, events, G_N_ELEMENTS(events), payload,
                       sizeof(payload));
    source_memory.log = &io;
    g_assert_true(dsp_jit_restore(source, initial));
    dsp_run(source, 100);
    g_assert_true(dsp_get_halt_requested(source));
    g_assert_true(dsp_io_complete(&io));
    g_assert_true(dsp_jit_capture(source, expected));
    g_assert_cmphex(expected->xram[0x200], ==, input[0]);
    g_assert_cmphex(expected->xram[0x201], ==, input[1]);
    for (unsigned run = 0; run < 100; run++) {
        memset(&replay_memory, 0, sizeof(replay_memory));
        memcpy(replay_memory.bytes + 12, input, sizeof(input));
        replay_memory.log = &io;
        dsp_io_replay_init(&io);
        g_assert_true(dsp_jit_restore(replay, initial));
        dsp_run(replay, 100);
        g_assert_true(dsp_get_halt_requested(replay));
        g_assert_true(dsp_io_complete(&io));
        g_assert_true(dsp_jit_capture(replay, actual));
        if (!dsp_capture_equal(expected, actual, &difference)) {
            g_error("repeat %u: %s[%zu] expected 0x%" PRIx64
                    " actual 0x%" PRIx64,
                    run, difference.field, difference.index,
                    difference.expected, difference.actual);
        }
    }
    dsp_destroy(source);
    dsp_destroy(replay);
    g_free(initial);
    g_free(expected);
    g_free(actual);
    g_free(wire);
}

static void test_capture_does_not_consume_dma_status(void)
{
    ReplayMemory memory = { 0 }, clone_memory = { 0 };
    g_config.audio.use_dsp_jit = true;
    DSPState *dsp = dsp_init(&memory, scratch, fifo, true);
    DSPState *clone = dsp_init(&clone_memory, scratch, fifo, true);
    DspJitCapture *checkpoint = g_new0(DspJitCapture, 1);

    dsp_write_memory(dsp, 'X', 0xffffd4, NODE_POINTER_EOL);
    dsp_write_memory(dsp, 'X', 0xffffd6, DMA_CONTROL_ACTION_START);
    g_assert_true(dsp_read_memory(dsp, 'X', 0xffffd6) & DMA_CONTROL_RUNNING);
    g_assert_true(dsp_read_memory(dsp, 'X', 0xffffd6) & DMA_CONTROL_RUNNING);
    g_assert_cmpuint(dsp->dma.dma_read_count, ==, 2);
    for (unsigned i = 0; i < 100; i++) {
        g_assert_true(dsp_jit_capture(dsp, checkpoint));
        g_assert_cmpuint(dsp->dma.dma_read_count, ==, 2);
        g_assert_true(dsp->dma.control & DMA_CONTROL_RUNNING);
    }
    g_assert_true(dsp_jit_restore(clone, checkpoint));
    uint32_t original_status = dsp_read_memory(dsp, 'X', 0xffffd6);
    uint32_t cloned_status = dsp_read_memory(clone, 'X', 0xffffd6);
    g_assert_cmphex(original_status, ==, cloned_status);
    g_assert_true(original_status & DMA_CONTROL_STOPPED);
    g_assert_cmpuint(memory.calls, ==, 0);
    g_assert_cmpuint(clone_memory.calls, ==, 0);
    dsp_destroy(dsp);
    dsp_destroy(clone);
    g_free(checkpoint);
}

static void test_interpreter_checkpoint(void)
{
    ReplayMemory memory = { 0 }, clone_memory = { 0 };
    g_config.audio.use_dsp_jit = false;
    DSPState *dsp = dsp_init(&memory, scratch, fifo, true);
    DSPState *clone = dsp_init(&clone_memory, scratch, fifo, true);
    DspCCapture *checkpoint = g_new0(DspCCapture, 1);
    DspCCapture *after = g_new0(DspCCapture, 1);
    dsp_write_memory(dsp, 'P', 0, 0x44f400); /* move immediate to x0 */
    dsp_write_memory(dsp, 'P', 1, 0x123456);
    dsp_write_memory(dsp, 'X', 0xffffd4, NODE_POINTER_EOL);
    dsp_write_memory(dsp, 'X', 0xffffd6, DMA_CONTROL_ACTION_START);
    g_assert_true(dsp_read_memory(dsp, 'X', 0xffffd6) & DMA_CONTROL_RUNNING);
    g_assert_true(dsp_read_memory(dsp, 'X', 0xffffd6) & DMA_CONTROL_RUNNING);
    dsp->save_cycles = -7;
    g_assert_true(dsp_c_capture(dsp, checkpoint));
    checkpoint->core.pc = 0;
    checkpoint->core.halt_requested = false;
    g_assert_cmpuint(dsp->dma.dma_read_count, ==, 2);
    for (unsigned run = 0; run < 100; run++) {
        g_assert_true(dsp_c_restore(clone, checkpoint));
        dsp_step(clone);
        g_assert_true(dsp_c_capture(clone, after));
        g_assert_cmphex(after->core.registers[DSP_REG_X0], ==, 0x123456);
        g_assert_cmpuint(after->core.pc, ==, 2);
        g_assert_cmpint(after->save_cycles, ==, -7);
        g_assert_cmpuint(after->dma_read_count, ==, 2);
        g_assert_true(dsp_read_memory(clone, 'X', 0xffffd6) &
                      DMA_CONTROL_STOPPED);
    }
    g_assert_cmpuint(dsp->dma.dma_read_count, ==, 2);
    dsp_destroy(dsp);
    dsp_destroy(clone);
    g_free(checkpoint);
    g_free(after);
}

static void test_headless_execution(void)
{
    ReplayMemory scratch_memory = { 0 };
    DspIoEvent events[2];
    uint8_t event_data[32], output[64];
    const uint32_t value = 0x123456;
    DspIoLog io;
    g_config.audio.use_dsp_jit = true;
    DSPState *seed = dsp_init(&scratch_memory, scratch, fifo, true);
    DspJitCapture *initial = g_new0(DspJitCapture, 1);
    DspJitCapture *after = g_new0(DspJitCapture, 1);

    dsp_write_memory(seed, 'X', 0x100, NODE_POINTER_EOL);
    dsp_write_memory(seed, 'X', 0x101,
                     (6 << 10) | (15 << 5) | NODE_CONTROL_DIRECTION);
    dsp_write_memory(seed, 'X', 0x102, 1);
    dsp_write_memory(seed, 'X', 0x103, 0x200);
    dsp_write_memory(seed, 'X', 0x104, 4);
    dsp_write_memory(seed, 'X', 0x105, 8);
    dsp_write_memory(seed, 'X', 0x106, 31);
    dsp_write_memory(seed, 'X', 0x200, value);
    dsp_write_memory(seed, 'X', 0xffffd4, 0x100);
    dsp_write_memory(seed, 'P', 0, 0x08f496);
    dsp_write_memory(seed, 'P', 1, DMA_CONTROL_ACTION_START);
    dsp_write_memory(seed, 'P', 2, 0x08f484);
    dsp_write_memory(seed, 'P', 3, 1);
    g_assert_true(dsp_jit_capture(seed, initial));
    initial->state.pc = 0;
    initial->state.halt_requested = false;
    dsp_io_record_init(&io, events, G_N_ELEMENTS(events), event_data,
                       sizeof(event_data));
    g_assert_true(dsp_io_record(&io, DSP_IO_SCRATCH, 0, 12, true, 0,
                                (const uint8_t *)&value, sizeof(value)));
    dsp_io_replay_init(&io);
    memset(output, 0xa5, sizeof(output));
    g_assert_cmpint(dsp_replay_execute(initial, scratch_memory.bytes,
                                       sizeof(scratch_memory.bytes), &io, 16,
                                       after, output),
                    ==, DSP_REPLAY_HALTED);
    g_assert_cmpmem(output + 12, sizeof(value), &value, sizeof(value));
    g_assert_true(after->state.halt_requested);
    g_assert_true(dsp_io_complete(&io));

    /* A divergent DMA write must not publish candidate memory or state. */
    initial->xram[0x200] ^= 1;
    dsp_io_replay_init(&io);
    memset(output, 0xa5, sizeof(output));
    after->state.pc = 0x765432;
    g_assert_cmpint(dsp_replay_execute(initial, scratch_memory.bytes,
                                       sizeof(scratch_memory.bytes), &io, 16,
                                       after, output),
                    ==, DSP_REPLAY_IO_MISMATCH);
    g_assert_cmphex(output[12], ==, 0xa5);
    g_assert_cmphex(after->state.pc, ==, 0x765432);

    /* Read responses must agree with the independent scratch memory image. */
    initial->xram[0x101] &= ~NODE_CONTROL_DIRECTION;
    memcpy(scratch_memory.bytes + 12, &value, sizeof(value));
    dsp_io_record_init(&io, events, G_N_ELEMENTS(events), event_data,
                       sizeof(event_data));
    g_assert_true(dsp_io_record(&io, DSP_IO_SCRATCH, 0, 12, false, 0,
                                (const uint8_t *)&value, sizeof(value)));
    dsp_io_replay_init(&io);
    g_assert_cmpint(dsp_replay_execute(initial, scratch_memory.bytes,
                                       sizeof(scratch_memory.bytes), &io, 16,
                                       after, output),
                    ==, DSP_REPLAY_HALTED);
    g_assert_cmphex(after->xram[0x200], ==, value);
    scratch_memory.bytes[12] ^= 1;
    dsp_io_replay_init(&io);
    memset(output, 0xa5, sizeof(output));
    after->state.pc = 0x765432;
    g_assert_cmpint(dsp_replay_execute(initial, scratch_memory.bytes,
                                       sizeof(scratch_memory.bytes), &io, 16,
                                       after, output),
                    ==, DSP_REPLAY_IO_MISMATCH);
    g_assert_cmphex(output[12], ==, 0xa5);
    g_assert_cmphex(after->state.pc, ==, 0x765432);

    initial->xram[0x104] = 64; /* scratch address lies outside mapped memory */
    dsp_io_replay_init(&io);
    g_assert_cmpint(dsp_replay_execute(initial, scratch_memory.bytes,
                                       sizeof(scratch_memory.bytes), &io, 16,
                                       after, output),
                    ==, DSP_REPLAY_UNSUPPORTED);
    g_assert_cmphex(output[12], ==, 0xa5);
    g_assert_cmphex(after->state.pc, ==, 0x765432);

    /* A non-halting program exhausts its declared instruction budget. */
    initial->pram[0] = 0;
    initial->pram[1] = 0;
    initial->pram[2] = 0;
    dsp_io_record_init(&io, events, G_N_ELEMENTS(events), event_data,
                       sizeof(event_data));
    dsp_io_replay_init(&io);
    g_assert_cmpint(dsp_replay_execute(initial, scratch_memory.bytes,
                                       sizeof(scratch_memory.bytes), &io, 2,
                                       after, output),
                    ==, DSP_REPLAY_TIMEOUT);
    dsp_destroy(seed);
    g_free(initial);
    g_free(after);
}

static void test_frame_replay_boundary(void)
{
    ReplayMemory gp_mem = { 0 }, ep_mem = { 0 };
    g_config.audio.use_dsp_jit = true;
    DSPState *gp = dsp_init(&gp_mem, scratch, fifo, true);
    DSPState *ep = dsp_init(&ep_mem, scratch, fifo, false);
    DspJitCapture *gp_start = g_new0(DspJitCapture, 1);
    DspJitCapture *ep_start = g_new0(DspJitCapture, 1);
    DspFrameResult *result = g_new0(DspFrameResult, 1);
    float input[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME] = { { 0 } };
    int16_t prior_pcm[256][2] = { { 0 } };
    DspFrameMemory memory = { 0 };
    DspIoLog io;

    dsp_write_memory(gp, 'P', 0, 0x08f484);
    dsp_write_memory(gp, 'P', 1, 1);
    dsp_write_memory(gp, 'P', 2, 0x0c0000); /* return to halt after restart */
    dsp_write_memory(ep, 'P', 0, 0x08f484);
    dsp_write_memory(ep, 'P', 1, 1);
    dsp_write_memory(ep, 'P', 2, 0x0c0000);
    g_assert_true(dsp_jit_capture(gp, gp_start));
    g_assert_true(dsp_jit_capture(ep, ep_start));
    gp_start->state.pc = ep_start->state.pc = 0;
    gp_start->state.halt_requested = ep_start->state.halt_requested = false;
    input[0][0] = 0.5f;
    input[1][0] = -0.5f;
    prior_pcm[0][0] = 123;
    dsp_io_record_init(&io, NULL, 0, NULL, 0);
    dsp_io_replay_init(&io);
    g_assert_cmpint(dsp_replay_gp_ep_frame(gp_start, ep_start, 7, input,
                                           prior_pcm, &memory, &io, result),
                    ==, DSP_REPLAY_HALTED);
    g_assert_cmpint(result->pcm[0][0], ==, 123);
    g_assert_cmpint(result->pcm[224][0], ==, 0x4000);
    g_assert_cmpint(result->pcm[224][1], ==, -0x4000);
    g_assert_true(result->gp.state.halt_requested);
    g_assert_false(result->ep.state.halt_requested);
    dsp_io_replay_init(&io);
    g_assert_cmpint(dsp_replay_gp_ep_frame(gp_start, ep_start, 0, input,
                                           prior_pcm, &memory, &io, result),
                    ==, DSP_REPLAY_HALTED);
    g_assert_true(result->ep.state.halt_requested);
    g_assert_cmpint(result->pcm[0][0], ==, 0x4000);
    memset(prior_pcm, 0, sizeof(prior_pcm));
    for (unsigned frame = 0; frame < 9; frame++) {
        dsp_io_replay_init(&io);
        g_assert_cmpint(dsp_replay_gp_ep_frame(gp_start, ep_start, frame % 8,
                                               input, prior_pcm, &memory, &io,
                                               result),
                        ==, DSP_REPLAY_HALTED);
        g_assert_cmpuint(result->gp.state.pc, ==, 2);
        g_assert_cmpuint(result->ep.state.pc, ==, 2);
        g_assert_cmpint(result->pcm[(frame % 8) * 32][0], ==, 0x4000);
        *gp_start = result->gp;
        *ep_start = result->ep;
        memcpy(prior_pcm, result->pcm, sizeof(prior_pcm));
    }
    for (unsigned phase = 0; phase < 8; phase++) {
        g_assert_cmpint(prior_pcm[phase * 32][0], ==, 0x4000);
        g_assert_cmpint(prior_pcm[phase * 32][1], ==, -0x4000);
    }
    result->pcm[0][0] = 321;
    dsp_io_replay_init(&io);
    input[0][0] = NAN;
    g_assert_cmpint(dsp_replay_gp_ep_frame(gp_start, ep_start, 0, input,
                                           prior_pcm, &memory, &io, result),
                    ==, DSP_REPLAY_INVALID);
    g_assert_cmpint(result->pcm[0][0], ==, 321);
    dsp_destroy(gp);
    dsp_destroy(ep);
    g_free(gp_start);
    g_free(ep_start);
    g_free(result);
}

static void test_frame_sequence(gconstpointer arg)
{
    const size_t count = GPOINTER_TO_UINT(arg);
    ReplayMemory unused_gp = { 0 }, unused_ep = { 0 };
    g_config.audio.use_dsp_jit = true;
    DSPState *gp = dsp_init(&unused_gp, scratch, fifo, true);
    DSPState *ep = dsp_init(&unused_ep, scratch, fifo, false);
    DspJitCapture *initial_gp = g_new0(DspJitCapture, 1);
    DspJitCapture *initial_ep = g_new0(DspJitCapture, 1);
    DspFrameResult *result = g_new0(DspFrameResult, 1);
    float (*mix)[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME] =
        g_new0(typeof(*mix), count);
    int16_t (*pcm)[NUM_SAMPLES_PER_FRAME][2] = g_new0(typeof(*pcm), count);
    int16_t prior[256][2] = { { 0 } };
    DspIoLog *logs = g_new0(DspIoLog, count);
    DspFrameMemory memory = { 0 };
    size_t failed = SIZE_MAX;
    DSPState *units[] = { gp, ep };

    for (size_t i = 0; i < G_N_ELEMENTS(units); i++) {
        dsp_write_memory(units[i], 'P', 0, 0x08f484);
        dsp_write_memory(units[i], 'P', 1, 1);
        dsp_write_memory(units[i], 'P', 2, 0x0c0000);
    }
    g_assert_true(dsp_jit_capture(gp, initial_gp));
    g_assert_true(dsp_jit_capture(ep, initial_ep));
    initial_gp->state.pc = initial_ep->state.pc = 0;
    initial_gp->state.halt_requested = initial_ep->state.halt_requested = false;
    for (size_t i = 0; i < count; i++) {
        mix[i][0][0] = (i & 1) ? -0.5f : 0.5f;
        dsp_io_record_init(&logs[i], NULL, 0, NULL, 0);
        dsp_io_replay_init(&logs[i]);
    }
    g_assert_cmpint(dsp_replay_gp_ep_sequence(initial_gp, initial_ep, 7, count,
                                              mix, prior, &memory, logs, result,
                                              pcm, &failed),
                    ==, DSP_REPLAY_HALTED);
    for (size_t i = 0; i < count; i++) {
        g_assert_cmpint(pcm[i][0][0], ==, (i & 1) ? -0x4000 : 0x4000);
    }
    g_assert_cmpuint(result->gp.state.pc, ==, 2);
    g_assert_cmpuint(result->ep.state.pc, ==, count > 1 ? 2 : 0);
    g_assert_cmpint(result->pcm[((7 + count - 1) % 8) * 32][0], ==,
                    (count & 1) ? 0x4000 : -0x4000);
    if (count < 4) {
        goto out;
    }
    result->pcm[0][0] = 123;
    pcm[0][0][0] = 456;
    mix[3][0][0] = NAN;
    for (size_t i = 0; i < count; i++) {
        dsp_io_replay_init(&logs[i]);
    }
    g_assert_cmpint(dsp_replay_gp_ep_sequence(initial_gp, initial_ep, 7, count,
                                              mix, prior, &memory, logs, result,
                                              pcm, &failed),
                    ==, DSP_REPLAY_INVALID);
    g_assert_cmpuint(failed, ==, 3);
    g_assert_cmpint(result->pcm[0][0], ==, 123);
    g_assert_cmpint(pcm[0][0][0], ==, 456);
out:
    dsp_destroy(gp);
    dsp_destroy(ep);
    g_free(initial_gp);
    g_free(initial_ep);
    g_free(result);
    g_free(mix);
    g_free(pcm);
    g_free(logs);
}

static void test_frame_separate_scratch(void)
{
    ReplayMemory unused_gp = { 0 }, unused_ep = { 0 };
    g_config.audio.use_dsp_jit = true;
    DSPState *gp = dsp_init(&unused_gp, scratch, fifo, true);
    DSPState *ep = dsp_init(&unused_ep, scratch, fifo, false);
    DspJitCapture *gp_start = g_new0(DspJitCapture, 1);
    DspJitCapture *ep_start = g_new0(DspJitCapture, 1);
    DspFrameResult *result = g_new0(DspFrameResult, 1);
    float input[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME] = { { 0 } };
    int16_t prior_pcm[256][2] = { { 0 } };
    uint8_t gp_mem[64] = { 0 }, ep_mem[64] = { 0 };
    uint8_t gp_out[64] = { 0 }, ep_out[64] = { 0 };
    DspFrameMemory memory = { .gp = gp_mem,
                              .ep = ep_mem,
                              .gp_size = sizeof(gp_mem),
                              .ep_size = sizeof(ep_mem),
                              .gp_result = gp_out,
                              .ep_result = ep_out };
    DspIoEvent events[2];
    uint8_t payload[8];
    DspIoLog io;
    const uint32_t gp_value = 0x123456, ep_value = 0x654321;
    DSPState *units[2] = { gp, ep };
    const uint32_t values[2] = { gp_value, ep_value };

    for (unsigned i = 0; i < 2; i++) {
        dsp_write_memory(units[i], 'X', 0x100, NODE_POINTER_EOL);
        dsp_write_memory(units[i], 'X', 0x101,
                         (6 << 10) | (15 << 5) | NODE_CONTROL_DIRECTION);
        dsp_write_memory(units[i], 'X', 0x102, 1);
        dsp_write_memory(units[i], 'X', 0x103, 0x200);
        dsp_write_memory(units[i], 'X', 0x104, 4);
        dsp_write_memory(units[i], 'X', 0x105, 8);
        dsp_write_memory(units[i], 'X', 0x106, 31);
        dsp_write_memory(units[i], 'X', 0x200, values[i]);
        dsp_write_memory(units[i], 'X', 0xffffd4, 0x100);
        dsp_write_memory(units[i], 'P', 0, 0x08f496);
        dsp_write_memory(units[i], 'P', 1, DMA_CONTROL_ACTION_START);
        dsp_write_memory(units[i], 'P', 2, 0x08f484);
        dsp_write_memory(units[i], 'P', 3, 1);
        dsp_write_memory(units[i], 'P', 4, 0x0c0002); /* return to halt */
    }
    g_assert_true(dsp_jit_capture(gp, gp_start));
    g_assert_true(dsp_jit_capture(ep, ep_start));
    gp_start->state.pc = ep_start->state.pc = 0;
    gp_start->state.halt_requested = ep_start->state.halt_requested = false;
    dsp_io_record_init(&io, events, G_N_ELEMENTS(events), payload,
                       sizeof(payload));
    g_assert_true(dsp_io_record(&io, DSP_IO_SCRATCH, 0, 12, true, 0,
                                (const uint8_t *)&gp_value, sizeof(gp_value)));
    g_assert_true(dsp_io_record(&io, DSP_IO_SCRATCH, 1, 12, true, 0,
                                (const uint8_t *)&ep_value, sizeof(ep_value)));
    dsp_io_replay_init(&io);
    g_assert_cmpint(dsp_replay_gp_ep_frame(gp_start, ep_start, 0, input,
                                           prior_pcm, &memory, &io, result),
                    ==, DSP_REPLAY_HALTED);
    g_assert_true(dsp_io_complete(&io));
    g_assert_cmpmem(gp_out + 12, sizeof(gp_value), &gp_value, sizeof(gp_value));
    g_assert_cmpmem(ep_out + 12, sizeof(ep_value), &ep_value, sizeof(ep_value));
    g_assert_cmpint(gp_mem[12], ==, 0);
    g_assert_cmpint(ep_mem[12], ==, 0);
    float sequence_mix[2][NUM_MIXBINS][NUM_SAMPLES_PER_FRAME] = { 0 };
    int16_t sequence_pcm[2][NUM_SAMPLES_PER_FRAME][2] = { 0 };
    DspIoLog sequence_logs[2];
    size_t failed_frame;
    dsp_io_replay_init(&io);
    sequence_logs[0] = io;
    dsp_io_record_init(&sequence_logs[1], NULL, 0, NULL, 0);
    dsp_io_replay_init(&sequence_logs[1]);
    memset(gp_out, 0, sizeof(gp_out));
    memset(ep_out, 0, sizeof(ep_out));
    g_assert_cmpint(dsp_replay_gp_ep_sequence(gp_start, ep_start, 0, 2,
                                              sequence_mix, prior_pcm, &memory,
                                              sequence_logs, result,
                                              sequence_pcm, &failed_frame),
                    ==, DSP_REPLAY_HALTED);
    g_assert_cmpmem(gp_out + 12, sizeof(gp_value), &gp_value, sizeof(gp_value));
    g_assert_cmpmem(ep_out + 12, sizeof(ep_value), &ep_value, sizeof(ep_value));
    dsp_io_replay_init(&sequence_logs[0]);
    dsp_io_replay_init(&sequence_logs[1]);
    sequence_mix[1][0][0] = NAN;
    memset(gp_out, 0xa5, sizeof(gp_out));
    memset(ep_out, 0xa5, sizeof(ep_out));
    g_assert_cmpint(dsp_replay_gp_ep_sequence(gp_start, ep_start, 0, 2,
                                              sequence_mix, prior_pcm, &memory,
                                              sequence_logs, result,
                                              sequence_pcm, &failed_frame),
                    ==, DSP_REPLAY_INVALID);
    g_assert_cmpuint(failed_frame, ==, 1);
    g_assert_cmphex(gp_out[12], ==, 0xa5);
    g_assert_cmphex(ep_out[12], ==, 0xa5);
    events[1].index = 0; /* EP must not be mistaken for GP */
    memset(gp_out, 0xa5, sizeof(gp_out));
    memset(ep_out, 0xa5, sizeof(ep_out));
    result->pcm[0][0] = 321;
    dsp_io_replay_init(&io);
    g_assert_cmpint(dsp_replay_gp_ep_frame(gp_start, ep_start, 0, input,
                                           prior_pcm, &memory, &io, result),
                    ==, DSP_REPLAY_IO_MISMATCH);
    g_assert_cmphex(gp_out[12], ==, 0xa5);
    g_assert_cmphex(ep_out[12], ==, 0xa5);
    g_assert_cmpint(result->pcm[0][0], ==, 321);
    dsp_destroy(gp);
    dsp_destroy(ep);
    g_free(gp_start);
    g_free(ep_start);
    g_free(result);
}

static MCPXAPUState *test_private_mapped_apu(void)
{
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
    memory_region_init_ram_ptr(ram, NULL, "dsp-fifo-test-ram", 0x10000,
                               backing);
    memory_region_add_subregion(get_system_memory(), 0, ram);
    MCPXAPUState *apu = g_new0(MCPXAPUState, 1);
    apu->ram = ram;
    apu->ram_ptr = memory_region_get_ram_ptr(ram);
    g_config.audio.use_dsp = true;
    g_config.audio.use_dsp_jit = true;
    mcpx_apu_dsp_init(apu);
    return apu;
}

static uint64_t capture_test_descriptor_read(void *opaque, hwaddr addr,
                                             unsigned size)
{
    unsigned *reads = opaque;
    g_assert_cmpuint(addr, ==, 0);
    g_assert_cmpuint(size, ==, 4);
    return 0x4000 + 0x1000 * (*reads)++;
}

static const MemoryRegionOps capture_test_descriptor_ops = {
    .read = capture_test_descriptor_read,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
};

typedef struct CaptureRemapDescriptor {
    MemoryRegion *overlay;
    MCPXAPUState *apu;
    unsigned reads;
} CaptureRemapDescriptor;

static uint64_t capture_remap_descriptor_read(void *opaque, hwaddr addr,
                                              unsigned size)
{
    CaptureRemapDescriptor *ctx = opaque;
    g_assert_cmpuint(addr, ==, 0);
    g_assert_cmpuint(size, ==, 4);
    ctx->reads++;
    memory_region_del_subregion(get_system_memory(), ctx->overlay);
    stl_le_p(ctx->apu->ram_ptr + 0x1000, 0x4000);
    return 0x4000;
}

static const MemoryRegionOps capture_remap_descriptor_ops = {
    .read = capture_remap_descriptor_read,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
};

static void test_capture_descriptor_remaps_during_read(void)
{
    if (!g_test_subprocess()) {
        g_test_trap_subprocess(NULL, 10 * G_USEC_PER_SEC, 0);
        g_test_trap_assert_passed();
        return;
    }
    MCPXAPUState *apu = test_private_mapped_apu();
    MemoryRegion *overlay = g_new0(MemoryRegion, 1);
    CaptureRemapDescriptor ctx = { .overlay = overlay, .apu = apu };
    memory_region_init_io(overlay, OBJECT(apu->ram),
                          &capture_remap_descriptor_ops, &ctx,
                          "capture-remap-descriptor", 4);
    memory_region_add_subregion_overlap(get_system_memory(), 0x1000, overlay,
                                        1);
    apu->regs[NV_PAPU_GPSADDR] = 0x1000;
    apu->regs[NV_PAPU_GPSMAXSGE] = 0;
    DSPState *gp = apu->gp.dsp;
    const uint32_t node[] = { NODE_POINTER_EOL,
                              (6 << 10) | (15 << 5) | NODE_CONTROL_DIRECTION,
                              1,
                              0x200,
                              0,
                              0,
                              31 };
    for (unsigned i = 0; i < G_N_ELEMENTS(node); i++) {
        dsp_write_memory(gp, 'X', 0x100 + i, node[i]);
    }
    dsp_write_memory(gp, 'X', 0x200, 0x123456);
    dsp_write_memory(gp, 'X', 0xffffd4, 0x100);
    const uint32_t program[] = { 0x08f496, DMA_CONTROL_ACTION_START, 0x08f484,
                                 1 };
    for (unsigned i = 0; i < G_N_ELEMENTS(program); i++) {
        dsp_write_memory(gp, 'P', i, program[i]);
    }
    apu->gp.regs[NV_PAPU_GPRST] = NV_PAPU_GPRST_GPRST | NV_PAPU_GPRST_GPDSPRST;
    apu->ep.regs[NV_PAPU_EPRST] = 0;
    apu->gp.realtime = apu->ep.realtime = false;
    apu->monitor.point = MCPX_APU_DEBUG_MON_GP;
    float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME] = { 0 };
    DspCaptureRing *ring = dsp_capture_ring_new(1);
    mcpx_apu_capture_dsp_frame(apu, ring, 0, mixbins);
    g_assert_cmpuint(ctx.reads, ==, 1);
    g_assert_cmphex(ldl_le_p(apu->ram_ptr + 0x4000), ==, 0x123456);
    const DspCaptureFrame *frame = dsp_capture_ring_peek(ring);
    g_assert_nonnull(frame);
#ifdef CONFIG_XEMU_DSP_CAPTURE
    g_assert_true(frame->flags & DSP_CAPTURE_NONRAM_DESCRIPTOR);
#endif
    dsp_capture_ring_release(ring);
    dsp_capture_ring_free(ring);
    dsp_destroy(apu->gp.dsp);
    dsp_destroy(apu->ep.dsp);
}

static void assert_capture_events_equal(const DspCaptureFrame *a,
                                        const DspCaptureFrame *b)
{
    g_assert_cmpuint(a->event_count, ==, b->event_count);
    g_assert_cmpuint(a->payload_size, ==, b->payload_size);
    g_assert_cmpmem(a->payload, a->payload_size, b->payload, b->payload_size);
    for (unsigned i = 0; i < a->event_count; i++) {
        const DspCaptureEvent *x = &a->events[i], *y = &b->events[i];
        g_assert_cmpuint(x->kind, ==, y->kind);
        g_assert_cmpuint(x->index, ==, y->index);
        g_assert_cmpuint(x->address, ==, y->address);
        g_assert_cmpuint(x->sample, ==, y->sample);
        g_assert_cmpuint(x->offset, ==, y->offset);
        g_assert_cmpuint(x->length, ==, y->length);
        g_assert_cmpint(x->write, ==, y->write);
    }
}

/* Verify no additional descriptor reads, including reads with side effects.
 * This does not establish coherence against concurrent guest CPU writes. */
static void test_capture_does_not_duplicate_mmio_reads(void)
{
    if (!g_test_subprocess()) {
        g_test_trap_subprocess(NULL, 10 * G_USEC_PER_SEC, 0);
        g_test_trap_assert_passed();
        return;
    }
    MCPXAPUState *apu = test_private_mapped_apu();
    unsigned reads = 0;
    MemoryRegion *overlay = g_new0(MemoryRegion, 1);
    memory_region_init_io(overlay, OBJECT(apu->ram),
                          &capture_test_descriptor_ops, &reads,
                          "capture-test-descriptor", 4);
    memory_region_add_subregion_overlap(get_system_memory(), 0x1000, overlay,
                                        1);
    apu->regs[NV_PAPU_GPFADDR] = 0x1000;
    apu->regs[NV_PAPU_GPFMAXSGE] = 0;
    apu->regs[NV_PAPU_GPOFBASE0] = 0;
    apu->regs[NV_PAPU_GPOFEND0] = 16;
    apu->regs[NV_PAPU_GPOFCUR0] = 12;
    DSPState *gp = apu->gp.dsp;
    const uint32_t node[] = {
        NODE_POINTER_EOL, (6 << 10) | NODE_CONTROL_DIRECTION, 2, 0x200, 0, 0, 0
    };
    for (unsigned i = 0; i < G_N_ELEMENTS(node); i++) {
        dsp_write_memory(gp, 'X', 0x100 + i, node[i]);
    }
    dsp_write_memory(gp, 'X', 0x200, 0x123456);
    dsp_write_memory(gp, 'X', 0x201, 0x654321);
    dsp_write_memory(gp, 'X', 0xffffd4, 0x100);
    const uint32_t program[] = { 0x08f496, DMA_CONTROL_ACTION_START, 0x08f484,
                                 1 };
    for (unsigned i = 0; i < G_N_ELEMENTS(program); i++) {
        dsp_write_memory(gp, 'P', i, program[i]);
    }
    apu->gp.regs[NV_PAPU_GPRST] = NV_PAPU_GPRST_GPRST | NV_PAPU_GPRST_GPDSPRST;
    apu->ep.regs[NV_PAPU_EPRST] = 0;
    apu->gp.realtime = apu->ep.realtime = false;
    apu->monitor.point = MCPX_APU_DEBUG_MON_GP;
    apu->ep_frame_div = 7;
    float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME] = { 0 };
    mixbins[0][0] = 0.5f;
    DspJitCapture *initial_gp = g_new0(DspJitCapture, 1);
    DspJitCapture *initial_ep = g_new0(DspJitCapture, 1);
    DspJitCapture *baseline_gp = g_new0(DspJitCapture, 1);
    DspJitCapture *baseline_ep = g_new0(DspJitCapture, 1);
    DspJitCapture *after = g_new0(DspJitCapture, 1);
    DspCaptureDifference difference;
    g_assert_true(dsp_jit_capture(gp, initial_gp));
    g_assert_true(dsp_jit_capture(apu->ep.dsp, initial_ep));
    uint8_t *initial_ram = g_memdup2(apu->ram_ptr, 0x10000);
    uint32_t *initial_regs = g_memdup2(apu->regs, sizeof(apu->regs));
    mcpx_apu_dsp_frame(apu, mixbins);
    g_assert_cmpuint(reads, ==, 2);
    g_assert_cmphex(ldl_le_p(apu->ram_ptr + 0x400c), ==, 0x123456);
    g_assert_cmphex(ldl_le_p(apu->ram_ptr + 0x5000), ==, 0x654321);
    g_assert_true(dsp_jit_capture(gp, baseline_gp));
    g_assert_true(dsp_jit_capture(apu->ep.dsp, baseline_ep));
    uint8_t *baseline_ram = g_memdup2(apu->ram_ptr, 0x10000);
    uint32_t *baseline_regs = g_memdup2(apu->regs, sizeof(apu->regs));
    int16_t baseline_pcm[256][2];
    memcpy(baseline_pcm, apu->monitor.frame_buf, sizeof(baseline_pcm));
    DspCaptureRing *ring = dsp_capture_ring_new(1);
    DspCaptureFrame *first = NULL;
    for (unsigned run = 0; run < 2; run++) {
        g_assert_true(dsp_jit_restore(gp, initial_gp));
        g_assert_true(dsp_jit_restore(apu->ep.dsp, initial_ep));
        memcpy(apu->ram_ptr, initial_ram, 0x10000);
        memcpy(apu->regs, initial_regs, sizeof(apu->regs));
        memset(apu->monitor.frame_buf, 0, sizeof(apu->monitor.frame_buf));
        reads = 0;
        mcpx_apu_capture_dsp_frame(apu, ring, 0, mixbins);
        g_assert_cmpuint(reads, ==, 2);
        g_assert_cmpmem(apu->ram_ptr, 0x10000, baseline_ram, 0x10000);
        g_assert_cmpmem(apu->regs, sizeof(apu->regs), baseline_regs,
                        sizeof(apu->regs));
        g_assert_cmpmem(apu->monitor.frame_buf, sizeof(baseline_pcm),
                        baseline_pcm, sizeof(baseline_pcm));
        g_assert_true(dsp_jit_capture(gp, after));
        g_assert_true(dsp_capture_equal(baseline_gp, after, &difference));
        g_assert_true(dsp_jit_capture(apu->ep.dsp, after));
        g_assert_true(dsp_capture_equal(baseline_ep, after, &difference));
        const DspCaptureFrame *frame = dsp_capture_ring_peek(ring);
        g_assert_nonnull(frame);
#ifdef CONFIG_XEMU_DSP_CAPTURE
        /* Side-effecting descriptors cannot be reconstructed as RAM. */
        g_assert_true(frame->flags & DSP_CAPTURE_NONRAM_DESCRIPTOR);
#endif
        if (!first) {
            first = g_memdup2(frame, sizeof(*frame));
        } else {
            g_assert_cmpuint(first->flags, ==, frame->flags);
            g_assert_true(dsp_capture_equal(&first->gp_before,
                                            &frame->gp_before, &difference));
            g_assert_true(dsp_capture_equal(&first->gp_after, &frame->gp_after,
                                            &difference));
            g_assert_true(dsp_capture_equal(&first->ep_before,
                                            &frame->ep_before, &difference));
            g_assert_true(dsp_capture_equal(&first->ep_after, &frame->ep_after,
                                            &difference));
            g_assert_cmpmem(first->apu_regs, sizeof(first->apu_regs),
                            frame->apu_regs, sizeof(frame->apu_regs));
            g_assert_cmpmem(
                first->apu_regs_after, sizeof(first->apu_regs_after),
                frame->apu_regs_after, sizeof(frame->apu_regs_after));
            g_assert_cmpmem(first->pcm_before, sizeof(first->pcm_before),
                            frame->pcm_before, sizeof(frame->pcm_before));
            g_assert_cmpmem(first->pcm_after, sizeof(first->pcm_after),
                            frame->pcm_after, sizeof(frame->pcm_after));
            assert_capture_events_equal(first, frame);
        }
        dsp_capture_ring_release(ring);
    }
    dsp_capture_ring_free(ring);
    dsp_destroy(apu->gp.dsp);
    dsp_destroy(apu->ep.dsp);
    g_free(first);
    g_free(initial_gp);
    g_free(initial_ep);
    g_free(baseline_gp);
    g_free(baseline_ep);
    g_free(after);
    g_free(initial_ram);
    g_free(initial_regs);
    g_free(baseline_ram);
    g_free(baseline_regs);
}

/* Exercise the actual GP FIFO, SGE and guest RAM path in an isolated QEMU
 * memory system. This is a source-behavior baseline, not a silicon oracle. */
static void test_production_gp_fifo_wrap(void)
{
    if (!g_test_subprocess()) {
        g_test_trap_subprocess(NULL, 10 * G_USEC_PER_SEC, 0);
        g_test_trap_assert_passed();
        return;
    }
    MCPXAPUState *apu = test_private_mapped_apu();
    stl_le_phys(&address_space_memory, 0x1000, 0x4000);
    apu->regs[NV_PAPU_GPFADDR] = 0x1000;
    apu->regs[NV_PAPU_GPFMAXSGE] = 0;
    apu->regs[NV_PAPU_GPOFBASE0] = 0;
    apu->regs[NV_PAPU_GPOFEND0] = 16;
    apu->regs[NV_PAPU_GPOFCUR0] = 12;
    DSPState *gp = apu->gp.dsp;
    dsp_write_memory(gp, 'X', 0x100, NODE_POINTER_EOL);
    dsp_write_memory(gp, 'X', 0x101, (6 << 10) | NODE_CONTROL_DIRECTION);
    dsp_write_memory(gp, 'X', 0x102, 2);
    dsp_write_memory(gp, 'X', 0x103, 0x200);
    dsp_write_memory(gp, 'X', 0x104, 0);
    dsp_write_memory(gp, 'X', 0x105, 0);
    dsp_write_memory(gp, 'X', 0x106, 0);
    dsp_write_memory(gp, 'X', 0x200, 0x123456);
    dsp_write_memory(gp, 'X', 0x201, 0x654321);
    dsp_write_memory(gp, 'X', 0xffffd4, 0x100);
    dsp_write_memory(gp, 'P', 0, 0x08f496);
    dsp_write_memory(gp, 'P', 1, DMA_CONTROL_ACTION_START);
    dsp_write_memory(gp, 'P', 2, 0x08f484);
    dsp_write_memory(gp, 'P', 3, 1);
    apu->gp.regs[NV_PAPU_GPRST] = NV_PAPU_GPRST_GPRST | NV_PAPU_GPRST_GPDSPRST;
    apu->ep.regs[NV_PAPU_EPRST] = 0;
    apu->gp.realtime = apu->ep.realtime = false;
    apu->monitor.point = MCPX_APU_DEBUG_MON_GP;
    apu->ep_frame_div = 7;
    DspMappedFrame *initial = g_new0(DspMappedFrame, 1);
    DspMappedFrame *replayed = g_new0(DspMappedFrame, 1);
    DspJitCapture *expected_gp = g_new0(DspJitCapture, 1);
    DspJitCapture *expected_ep = g_new0(DspJitCapture, 1);
    uint8_t *initial_ram = g_memdup2(apu->ram_ptr, 0x10000);
    uint8_t *expected_ram = g_new0(uint8_t, 0x10000);
    uint8_t *replayed_ram = g_new0(uint8_t, 0x10000);
    float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME] = { 0 };
    DspCaptureDifference difference;
    g_assert_true(dsp_jit_capture(apu->gp.dsp, &initial->dsp.gp));
    g_assert_true(dsp_jit_capture(apu->ep.dsp, &initial->dsp.ep));
    memcpy(initial->apu_regs, apu->regs, sizeof(initial->apu_regs));
    initial->gp_reset = apu->gp.regs[NV_PAPU_GPRST];
    initial->ep_reset = apu->ep.regs[NV_PAPU_EPRST];
    initial->monitor_point = apu->monitor.point;
    initial->phase = apu->ep_frame_div;
    DspCaptureRing *ring = dsp_capture_ring_new(1);
    g_assert_nonnull(ring);
    mcpx_apu_capture_dsp_frame(apu, ring, 42, mixbins);
    g_assert_cmphex(ldl_le_phys(&address_space_memory, 0x400c), ==, 0x123456);
    g_assert_cmphex(ldl_le_phys(&address_space_memory, 0x4000), ==, 0x654321);
    g_assert_cmphex(apu->regs[NV_PAPU_GPOFCUR0], ==, 4);
    g_assert_true(dsp_jit_capture(apu->gp.dsp, expected_gp));
    g_assert_true(dsp_jit_capture(apu->ep.dsp, expected_ep));
    const DspCaptureFrame *published = dsp_capture_ring_peek(ring);
    g_assert_nonnull(published);
    g_assert_cmpuint(published->frame_id, ==, 42);
    g_assert_true(published->flags & DSP_CAPTURE_EXTERNAL_UNVERIFIED);
    g_assert_false(published->flags & DSP_CAPTURE_INCOMPLETE_BOUNDARY);
    g_assert_false(published->flags & DSP_CAPTURE_NONRAM_DESCRIPTOR);
    g_assert_true(dsp_capture_equal(&initial->dsp.gp, &published->gp_before,
                                    &difference));
    g_assert_true(
        dsp_capture_equal(expected_gp, &published->gp_after, &difference));
    g_assert_true(
        dsp_capture_equal(expected_ep, &published->ep_after, &difference));
    g_assert_cmphex(published->apu_regs_after[NV_PAPU_GPOFCUR0], ==, 4);
#ifdef CONFIG_XEMU_DSP_CAPTURE
    g_assert_cmpuint(published->event_count, ==, 7);
#else
    g_assert_cmpuint(published->event_count, ==, 1);
#endif
    g_assert_cmpuint(published->events[0].kind, ==, DSP_CAPTURE_FIFO);
    g_assert_cmpuint(published->events[0].index, ==, 0);
    g_assert_cmpuint(published->events[0].sample, ==, 42 * 32);
    g_assert_true(published->events[0].write);
    const uint8_t expected_transfer[] = { 0x56, 0x34, 0x12, 0,
                                          0x21, 0x43, 0x65, 0 };
    g_assert_cmpmem(published->payload, published->events[0].length,
                    expected_transfer, sizeof(expected_transfer));
#ifdef CONFIG_XEMU_DSP_CAPTURE
    g_assert_cmpuint(published->events[1].kind, ==, DSP_CAPTURE_SGE_READ);
    g_assert_cmphex(published->events[1].address, ==, 0x1000);
    g_assert_cmpuint(published->events[2].kind, ==, DSP_CAPTURE_RAM_PREWRITE);
    g_assert_cmphex(published->events[2].address, ==, 0x400c);
    g_assert_cmpuint(published->events[3].kind, ==, DSP_CAPTURE_RAM_WRITE);
    g_assert_cmpuint(published->events[4].kind, ==, DSP_CAPTURE_SGE_READ);
    g_assert_cmpuint(published->events[5].kind, ==, DSP_CAPTURE_RAM_PREWRITE);
    g_assert_cmphex(published->events[5].address, ==, 0x4000);
    g_assert_cmpuint(published->events[6].kind, ==, DSP_CAPTURE_RAM_WRITE);
#endif
    memcpy(expected_ram, apu->ram_ptr, 0x10000);
    dsp_write_memory(gp, 'P', 4, 0x0c0002); /* next frame returns to halt */
    mcpx_apu_capture_dsp_frame(apu, ring, 43, mixbins);
    g_assert_cmpuint(dsp_capture_ring_drops(ring), ==, 1);
    g_assert_true(dsp_get_halt_requested(gp));
    g_assert_cmpuint(dsp_get_cycle_count(gp), ==, 6);
    DspJitCapture *after_drop = g_new(DspJitCapture, 1);
    g_assert_true(dsp_jit_capture(gp, after_drop));
    g_assert_cmpint(after_drop->state.cycle_budget, ==,
                    expected_gp->state.cycle_budget + 994);
    g_free(after_drop);
    g_assert_cmpmem(expected_ram, 0x10000, apu->ram_ptr, 0x10000);
    dsp_capture_ring_release(ring);
    DspCaptureFrame *invalid = dsp_capture_ring_begin(ring, 44);
    g_assert_nonnull(invalid);
    g_assert_true(dsp_capture_frame_start(invalid, apu, mixbins));
    g_assert_false(dsp_capture_frame_start(invalid, apu, mixbins));
    g_assert_false(dsp_capture_frame_finish(invalid, apu));
    dsp_capture_ring_publish(ring);
    published = dsp_capture_ring_peek(ring);
    g_assert_true(published->flags & DSP_CAPTURE_INCOMPLETE_BOUNDARY);
    dsp_capture_ring_release(ring);
    DspCaptureFrame *modified = dsp_capture_ring_begin(ring, 45);
    g_assert_true(dsp_capture_frame_start(modified, apu, mixbins));
    dsp_write_memory(gp, 'P', 4, 0x0c0000);
    g_assert_true(dsp_capture_frame_finish(modified, apu));
    dsp_capture_ring_publish(ring);
    published = dsp_capture_ring_peek(ring);
    g_assert_true(published->flags & DSP_CAPTURE_PROGRAM_DIFFERENT);
    dsp_capture_ring_release(ring);
    dsp_capture_ring_free(ring);
    g_assert_cmpint(dsp_replay_mapped_frame(apu, initial, mixbins, initial_ram,
                                            0x10000, replayed, replayed_ram),
                    ==, DSP_REPLAY_HALTED);
    g_assert_true(
        dsp_capture_equal(expected_gp, &replayed->dsp.gp, &difference));
    g_assert_true(
        dsp_capture_equal(expected_ep, &replayed->dsp.ep, &difference));
    g_assert_cmphex(replayed->apu_regs[NV_PAPU_GPOFCUR0], ==, 4);
    g_assert_cmpmem(expected_ram, 0x10000, replayed_ram, 0x10000);
    g_free(initial);
    g_free(replayed);
    g_free(expected_gp);
    g_free(expected_ep);
    g_free(initial_ram);
    g_free(expected_ram);
    g_free(replayed_ram);
    dsp_destroy(apu->gp.dsp);
    dsp_destroy(apu->ep.dsp);
    /* Process exits immediately, so the registered RAM remains alive. */
}

static void test_production_ep_sink(void)
{
    if (!g_test_subprocess()) {
        g_test_trap_subprocess(NULL, 10 * G_USEC_PER_SEC, 0);
        g_test_trap_assert_passed();
        return;
    }
    MCPXAPUState *apu = test_private_mapped_apu();
    stl_le_phys(&address_space_memory, 0x1000, 0x4000);
    apu->regs[NV_PAPU_EPFADDR] = 0x1000;
    apu->regs[NV_PAPU_EPFMAXSGE] = 0;
    apu->regs[NV_PAPU_EPOFBASE0] = 0;
    apu->regs[NV_PAPU_EPOFEND0] = 1024;
    apu->regs[NV_PAPU_EPOFCUR0] = 0;
    memset(apu->ram_ptr + 0x4000, 0x77, 1024);
    DSPState *ep = apu->ep.dsp;
    dsp_write_memory(ep, 'X', 0x100, NODE_POINTER_EOL);
    dsp_write_memory(ep, 'X', 0x101, (1 << 10) | NODE_CONTROL_DIRECTION);
    dsp_write_memory(ep, 'X', 0x102, 512);
    dsp_write_memory(ep, 'X', 0x103, 0x200);
    for (unsigned i = 0; i < 512; i++) {
        dsp_write_memory(ep, 'X', 0x200 + i, (i & 1) ? 0xe00000 : 0x200000);
    }
    dsp_write_memory(ep, 'X', 0xffffd4, 0x100);
    dsp_write_memory(ep, 'P', 0, 0x08f496);
    dsp_write_memory(ep, 'P', 1, DMA_CONTROL_ACTION_START);
    dsp_write_memory(ep, 'P', 2, 0x08f484);
    dsp_write_memory(ep, 'P', 3, 1);
    apu->gp.regs[NV_PAPU_GPRST] = 0;
    apu->ep.regs[NV_PAPU_EPRST] = NV_PAPU_GPRST_GPRST | NV_PAPU_GPRST_GPDSPRST;
    apu->gp.realtime = apu->ep.realtime = false;
    apu->monitor.point = MCPX_APU_DEBUG_MON_EP;
    apu->ep_frame_div = 0;
    DspMappedFrame *initial = g_new0(DspMappedFrame, 1);
    DspMappedFrame *replayed = g_new0(DspMappedFrame, 1);
    DspJitCapture *expected_ep = g_new0(DspJitCapture, 1);
    uint8_t *initial_ram = g_memdup2(apu->ram_ptr, 0x10000);
    uint8_t *expected_ram = g_new0(uint8_t, 0x10000);
    uint8_t *replayed_ram = g_new0(uint8_t, 0x10000);
    int16_t expected_pcm[256][2];
    float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME] = { 0 };
    DspCaptureDifference difference;
    g_assert_true(dsp_jit_capture(apu->gp.dsp, &initial->dsp.gp));
    g_assert_true(dsp_jit_capture(apu->ep.dsp, &initial->dsp.ep));
    memcpy(initial->apu_regs, apu->regs, sizeof(initial->apu_regs));
    initial->ep_reset = apu->ep.regs[NV_PAPU_EPRST];
    initial->monitor_point = apu->monitor.point;
    mcpx_apu_dsp_frame(apu, mixbins);
    g_assert_cmpint(apu->monitor.frame_buf[0][0], ==, 8192);
    g_assert_cmpint(apu->monitor.frame_buf[0][1], ==, -8192);
    for (unsigned i = 0; i < 1024; i++) {
        g_assert_cmpuint(apu->ram_ptr[0x4000 + i], ==, 0);
    }
    g_assert_true(dsp_jit_capture(ep, expected_ep));
    memcpy(expected_pcm, apu->monitor.frame_buf, sizeof(expected_pcm));
    memcpy(expected_ram, apu->ram_ptr, 0x10000);
    float sequence_mix[2][NUM_MIXBINS][NUM_SAMPLES_PER_FRAME] = { 0 };
    int16_t sequence_pcm[2][256][2] = { 0 };
    size_t failed_frame = SIZE_MAX;
    apu->ep_frame_div = 1;
    mcpx_apu_dsp_frame(apu, mixbins);
    DspJitCapture *second_ep = g_new(DspJitCapture, 1);
    g_assert_true(dsp_jit_capture(ep, second_ep));
    g_assert_true(dsp_capture_equal(expected_ep, second_ep, &difference));
    g_free(second_ep);
    g_assert_cmpint(dsp_replay_mapped_sequence(
                        apu, initial, 2, sequence_mix, initial_ram, 0x10000,
                        replayed, replayed_ram, sequence_pcm, &failed_frame),
                    ==, DSP_REPLAY_HALTED);
    g_assert_true(
        dsp_capture_equal(expected_ep, &replayed->dsp.ep, &difference));
    g_assert_cmpuint(replayed->phase, ==, 2);
    g_assert_cmpmem(expected_pcm, sizeof(expected_pcm), sequence_pcm[0],
                    sizeof(sequence_pcm[0]));
    g_assert_cmpmem(expected_pcm, sizeof(expected_pcm), sequence_pcm[1],
                    sizeof(sequence_pcm[1]));
    g_assert_cmpmem(expected_ram, 0x10000, replayed_ram, 0x10000);
    DspIoEvent trace_events[1];
    uint8_t trace_bytes[1024];
    DspIoLog trace;
    dsp_io_record_init(&trace, trace_events, G_N_ELEMENTS(trace_events),
                       trace_bytes, sizeof(trace_bytes));
    g_assert_cmpint(dsp_replay_mapped_trace(apu, initial, 2, sequence_mix,
                                            initial_ram, 0x10000, replayed,
                                            replayed_ram, sequence_pcm,
                                            &failed_frame, &trace),
                    ==, DSP_REPLAY_HALTED);
    g_assert_cmpuint(trace.count, ==, 1);
    g_assert_cmpint(trace.events[0].kind, ==, DSP_IO_FIFO);
    g_assert_cmpuint(trace.events[0].index, ==, 0x10000);
    g_assert_cmpuint(trace.events[0].sample, ==, 0);
    g_assert_cmpmem(trace.bytes, 1024, expected_pcm, sizeof(expected_pcm));
    dsp_io_replay_init(&trace);
    trace.bytes[4] ^= 1;
    sequence_pcm[0][0][0] = 321;
    replayed_ram[0] = 0xa5;
    g_assert_cmpint(dsp_replay_mapped_trace(apu, initial, 2, sequence_mix,
                                            initial_ram, 0x10000, replayed,
                                            replayed_ram, sequence_pcm,
                                            &failed_frame, &trace),
                    ==, DSP_REPLAY_IO_MISMATCH);
    g_assert_cmpuint(failed_frame, ==, 0);
    g_assert_cmpint(sequence_pcm[0][0][0], ==, 321);
    g_assert_cmphex(replayed_ram[0], ==, 0xa5);
    dsp_io_record_init(&trace, NULL, 0, NULL, 0);
    g_assert_cmpint(dsp_replay_mapped_trace(apu, initial, 2, sequence_mix,
                                            initial_ram, 0x10000, replayed,
                                            replayed_ram, sequence_pcm,
                                            &failed_frame, &trace),
                    ==, DSP_REPLAY_IO_MISMATCH);
    g_assert_false(trace.valid);
    g_assert_cmpuint(failed_frame, ==, 0);
    g_assert_cmpint(sequence_pcm[0][0][0], ==, 321);
    g_assert_cmphex(replayed_ram[0], ==, 0xa5);
    sequence_mix[1][0][0] = NAN;
    sequence_pcm[0][0][0] = 321;
    replayed_ram[0] = 0xa5;
    g_assert_cmpint(dsp_replay_mapped_sequence(
                        apu, initial, 2, sequence_mix, initial_ram, 0x10000,
                        replayed, replayed_ram, sequence_pcm, &failed_frame),
                    ==, DSP_REPLAY_INVALID);
    g_assert_cmpuint(failed_frame, ==, 1);
    g_assert_cmpint(sequence_pcm[0][0][0], ==, 321);
    g_assert_cmphex(replayed_ram[0], ==, 0xa5);
    g_assert_cmpint(dsp_replay_mapped_frame(apu, initial, mixbins, initial_ram,
                                            0x10000, replayed, replayed_ram),
                    ==, DSP_REPLAY_HALTED);
    g_assert_true(
        dsp_capture_equal(expected_ep, &replayed->dsp.ep, &difference));
    g_assert_cmpmem(expected_pcm, sizeof(expected_pcm), replayed->dsp.pcm,
                    sizeof(replayed->dsp.pcm));
    g_assert_cmpint(replayed->dsp.pcm[0][0], ==, 8192);
    g_assert_cmpint(replayed->dsp.pcm[0][1], ==, -8192);
    g_assert_cmpmem(expected_ram, 0x10000, replayed_ram, 0x10000);
    initial->ep_realtime = true;
    g_assert_cmpint(dsp_replay_mapped_frame(apu, initial, mixbins, initial_ram,
                                            0x10000, replayed, replayed_ram),
                    ==, DSP_REPLAY_HALTED);
    g_assert_true(
        dsp_capture_equal(expected_ep, &replayed->dsp.ep, &difference));
    replayed->dsp.pcm[0][0] = 123;
    replayed_ram[0] = 0xa5;
    mixbins[0][0] = NAN;
    g_assert_cmpint(dsp_replay_mapped_frame(apu, initial, mixbins, initial_ram,
                                            0x10000, replayed, replayed_ram),
                    ==, DSP_REPLAY_INVALID);
    g_assert_cmpint(replayed->dsp.pcm[0][0], ==, 123);
    g_assert_cmphex(replayed_ram[0], ==, 0xa5);
    dsp_destroy(apu->gp.dsp);
    dsp_destroy(apu->ep.dsp);
    g_free(initial);
    g_free(replayed);
    g_free(expected_ep);
    g_free(initial_ram);
    g_free(expected_ram);
    g_free(replayed_ram);
}

static void test_capture_rejects_interpreter_without_skipping_frame(void)
{
    ReplayMemory gp_memory = { 0 }, ep_memory = { 0 };
    MCPXAPUState *apu = g_new0(MCPXAPUState, 1);
    g_config.audio.use_dsp_jit = false;
    apu->gp.dsp = dsp_init(&gp_memory, scratch, fifo, true);
    apu->ep.dsp = dsp_init(&ep_memory, scratch, fifo, false);
    DspCaptureRing *ring = dsp_capture_ring_new(1);
#ifdef CONFIG_XEMU_DSP_CAPTURE
    qemu_mutex_init(&apu->lock);
    qemu_mutex_lock(&apu->lock);
    apu->capture_frame_id = 19;
    mcpx_apu_capture_set_ring_locked(apu, ring);
    g_assert_true(apu->capture_ring == ring);
    g_assert_cmpuint(apu->capture_frame_id, ==, 0);
    qemu_mutex_unlock(&apu->lock);
    gp_ops.write(apu, NV_PAPU_GPPMEM, 0, 4);
#endif
    float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME] = { 0 };
    mixbins[0][0] = 0.5f;
    mcpx_apu_capture_dsp_frame(apu, ring, 0, mixbins);
    const DspCaptureFrame *frame = dsp_capture_ring_peek(ring);
    g_assert_nonnull(frame);
    g_assert_true(frame->flags & DSP_CAPTURE_UNSUPPORTED_BACKEND);
    g_assert_true(frame->flags & DSP_CAPTURE_EXTERNAL_UNVERIFIED);
#ifdef CONFIG_XEMU_DSP_CAPTURE
    g_assert_true(frame->flags & DSP_CAPTURE_DISCONTINUITY);
#endif
    g_assert_cmphex(dsp_read_memory(apu->gp.dsp, 'X', 0x1400), ==, 0x400000);
    dsp_capture_ring_release(ring);
#ifdef CONFIG_XEMU_DSP_CAPTURE
    qemu_mutex_lock(&apu->lock);
    mcpx_apu_capture_set_ring_locked(apu, NULL);
    g_assert_null(apu->capture_ring);
    qemu_mutex_unlock(&apu->lock);
    qemu_mutex_destroy(&apu->lock);
#endif
    dsp_capture_ring_free(ring);
    dsp_destroy(apu->gp.dsp);
    dsp_destroy(apu->ep.dsp);
    g_free(apu);
}

static void test_capture_session_finishes_while_apu_continues(void)
{
    GError *error = NULL;
    char *directory = g_dir_make_tmp("xemu-dsp-session-XXXXXX", &error);
    g_assert_no_error(error);
    char *path = g_build_filename(directory, "partial.xcr", NULL);
    DspCaptureSession *session = dsp_capture_session_start(path, 1);
    g_assert_nonnull(session);
    g_assert_true(dsp_capture_session_set_skip(session, 2));
    ReplayMemory gp_memory = { 0 }, ep_memory = { 0 };
    MCPXAPUState *apu = g_new0(MCPXAPUState, 1);
    g_config.audio.use_dsp_jit = true;
    apu->gp.dsp = dsp_init(&gp_memory, scratch, fifo, true);
    apu->ep.dsp = dsp_init(&ep_memory, scratch, fifo, false);
    float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME] = { 0 };
    mixbins[0][0] = 0.5f;
    mcpx_apu_capture_session_frame(apu, session, mixbins);
    g_assert_true(dsp_capture_session_active(session));
    mcpx_apu_capture_session_frame(apu, session, mixbins);
    g_assert_true(dsp_capture_session_active(session));
    mcpx_apu_capture_session_frame(apu, session, mixbins);
    g_assert_false(dsp_capture_session_active(session));
    g_assert_cmphex(dsp_read_memory(apu->gp.dsp, 'X', 0x1400), ==, 0x400000);
    mixbins[0][0] = -0.5f;
    mcpx_apu_capture_session_frame(apu, session, mixbins);
    g_assert_cmphex(dsp_read_memory(apu->gp.dsp, 'X', 0x1400), ==, 0xc00000);
    DspCaptureWriterResult result;
    dsp_capture_session_stop(session, &result);
    g_assert_false(result.failed);
    g_assert_cmpuint(result.written, ==, 1);
    g_assert_cmpuint(result.dropped, ==, 0);
    gchar *contents = NULL;
    gsize size = 0;
    g_assert_true(g_file_get_contents(path, &contents, &size, &error));
    g_assert_no_error(error);
    g_assert_cmpmem(contents, 4, "XCR1", 4);
    g_assert_cmpmem(contents + size - 16, 4, "END1", 4);
    uint32_t header_size = ldl_le_p(contents + 8);
    g_assert_cmpuint(header_size, <=, size - 40 - 16);
    g_assert_cmpuint(ldq_le_p(contents + header_size + 40 + 4), ==, 2);
    g_free(contents);
    dsp_destroy(apu->gp.dsp);
    dsp_destroy(apu->ep.dsp);
    g_free(apu);
    g_assert_cmpint(g_unlink(path), ==, 0);
    g_assert_cmpint(g_rmdir(directory), ==, 0);
    g_free(path);
    g_free(directory);
}

static void test_c_checkpoint_hidden_state_comparison(void)
{
    DspCCapture *a = g_new0(DspCCapture, 1);
    DspCCapture *b = g_new0(DspCCapture, 1);
    DspCaptureDifference difference;
    g_assert_true(dsp_c_capture_equal(a, b, &difference));
    b->core.interrupt_pipeline_count = 2;
    g_assert_false(dsp_c_capture_equal(a, b, &difference));
    g_assert_cmpstr(difference.field, ==, "core.interrupt_pipeline_count");
    b->core.interrupt_pipeline_count = 0;
    b->core.interrupt_ipl[11] = 2;
    g_assert_false(dsp_c_capture_equal(a, b, &difference));
    g_assert_cmpstr(difference.field, ==, "core.interrupt_ipl");
    g_assert_cmpuint(difference.index, ==, 11);
    b->core.interrupt_ipl[11] = 0;
    b->core.loop_rep = 1;
    g_assert_false(dsp_c_capture_equal(a, b, &difference));
    g_assert_cmpstr(difference.field, ==, "core.loop_rep");
    b->core.loop_rep = 0;
    b->core.periph[DSP_PERIPH_SIZE - 1] = 1;
    g_assert_false(dsp_c_capture_equal(a, b, &difference));
    g_assert_cmpstr(difference.field, ==, "core.periph");
    g_assert_cmpuint(difference.index, ==, DSP_PERIPH_SIZE - 1);
    b->core.periph[DSP_PERIPH_SIZE - 1] = 0;
    b->exception_debugging = true;
    g_assert_false(dsp_c_capture_equal(a, b, &difference));
    g_assert_cmpstr(difference.field, ==, "exception_debugging");
    g_free(a);
    g_free(b);
}

static void test_address_register_normalization(void)
{
    g_assert_cmphex(dsp_replay_normalize_register(false, DSP_REG_N0, 0x7fff),
                    ==, 0x7fff);
    g_assert_cmphex(dsp_replay_normalize_register(false, DSP_REG_N0, 0x8000),
                    ==, 0xff8000);
    g_assert_cmphex(dsp_replay_normalize_register(false, DSP_REG_N7, 0xffff),
                    ==, 0xffffff);
    g_assert_cmphex(dsp_replay_normalize_register(false, DSP_REG_M0, 0xffff),
                    ==, 0xffffff);
    g_assert_cmphex(dsp_replay_normalize_register(false, DSP_REG_M7, 0xfffe),
                    ==, 0xfffe);
    g_assert_cmphex(dsp_replay_normalize_register(false, DSP_REG_R0, 0xffff),
                    ==, 0xffff);
    g_assert_cmphex(dsp_replay_normalize_register(true, DSP_REG_N0, 0x8000), ==,
                    0x8000);
    g_assert_cmphex(dsp_replay_normalize_register(true, DSP_REG_M0, 0xffff), ==,
                    0xffff);
}

int __wrap_main(int argc, char **argv);

int __wrap_main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_data_func("/dsp/replay/dma-gp", GINT_TO_POINTER(true),
                         test_dma_replay);
    g_test_add_data_func("/dsp/replay/dma-ep", GINT_TO_POINTER(false),
                         test_dma_replay);
    g_test_add_func("/dsp/replay/jit-interpreter-dma-effects",
                    test_jit_and_interpreter_dma_effects);
    g_test_add_func("/dsp/replay/jit-interpreter-stack",
                    test_jit_and_interpreter_stack_execution);
    g_test_add_data_func("/dsp/replay/interrupt-underflow/jit",
                         GINT_TO_POINTER(true),
                         test_interrupt_underflow_replay);
    g_test_add_data_func("/dsp/replay/interrupt-underflow/interpreter",
                         GINT_TO_POINTER(false),
                         test_interrupt_underflow_replay);
    g_test_add_data_func(
        "/dsp/replay/interrupt-underflow/interpreter-no-assert",
        GINT_TO_POINTER(2), test_interrupt_underflow_replay);
    g_test_add_func("/dsp/replay/address-register-normalization",
                    test_address_register_normalization);
    g_test_add_func("/dsp/replay/c-checkpoint-hidden-state",
                    test_c_checkpoint_hidden_state_comparison);
    g_test_add_func("/dsp/replay/gp-ep-frame", test_gp_ep_frame);
    g_test_add_func("/dsp/replay/dma-read", test_dma_read_replay);
    g_test_add_func("/dsp/replay/non-destructive-status",
                    test_capture_does_not_consume_dma_status);
    g_test_add_func("/dsp/replay/interpreter-checkpoint",
                    test_interpreter_checkpoint);
    g_test_add_func("/dsp/replay/headless-execution", test_headless_execution);
    g_test_add_func("/dsp/replay/gp-ep-frame-boundary",
                    test_frame_replay_boundary);
    g_test_add_data_func("/dsp/replay/gp-ep-sequence/1", GUINT_TO_POINTER(1),
                         test_frame_sequence);
    g_test_add_data_func("/dsp/replay/gp-ep-sequence/8", GUINT_TO_POINTER(8),
                         test_frame_sequence);
    g_test_add_data_func("/dsp/replay/gp-ep-sequence/9", GUINT_TO_POINTER(9),
                         test_frame_sequence);
    g_test_add_data_func("/dsp/replay/gp-ep-sequence/128",
                         GUINT_TO_POINTER(128), test_frame_sequence);
    g_test_add_data_func("/dsp/replay/gp-ep-sequence/1024",
                         GUINT_TO_POINTER(1024), test_frame_sequence);
    g_test_add_func("/dsp/replay/gp-ep-separate-scratch",
                    test_frame_separate_scratch);
    g_test_add_func("/dsp/production/gp-fifo-wrap",
                    test_production_gp_fifo_wrap);
    g_test_add_func("/dsp/capture/no-extra-mmio-reads",
                    test_capture_does_not_duplicate_mmio_reads);
    g_test_add_func("/dsp/capture/descriptor-remap",
                    test_capture_descriptor_remaps_during_read);
    g_test_add_func("/dsp/production/ep-sink", test_production_ep_sink);
    g_test_add_func("/dsp/capture/unsupported-interpreter",
                    test_capture_rejects_interpreter_without_skipping_frame);
    g_test_add_func("/dsp/capture/session-finish",
                    test_capture_session_finishes_while_apu_continues);
    return g_test_run();
}

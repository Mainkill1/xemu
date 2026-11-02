/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "hw/xbox/mcpx/apu/apu_int.h"
#include "hw/xbox/mcpx/apu/dsp/dsp_dma_regs.h"
#include "ui/xemu-settings.h"
#include "dsp-replay-dual.h"
#include "dsp-replay-registers.h"
#include "dsp-capture-compare.h"

#define DUAL_RAM_SIZE 0x10000
#define DUAL_MAX_EVENTS 4096
#define DUAL_MAX_PAYLOAD (4 * 1024 * 1024)

typedef struct DualTrace {
    void *opaque;
    dsp_scratch_rw_func scratch;
    dsp_fifo_rw_func fifo;
    DspIoLog *log;
    uint64_t sample;
    unsigned unit;
    bool failed;
} DualTrace;

static void dual_transfer(DualTrace *trace, DspIoKind kind, uint32_t index,
                          uint32_t address, bool write, uint8_t *bytes,
                          size_t len)
{
    if (!trace->failed && !dsp_io_record(trace->log, kind, index, address,
                                         write, trace->sample, bytes, len)) {
        trace->failed = true;
    }
}

static void dual_scratch(void *opaque, uint8_t *bytes, uint32_t address,
                         size_t len, bool write)
{
    DualTrace *trace = opaque;
    if (write) {
        dual_transfer(trace, DSP_IO_SCRATCH, trace->unit, address, true, bytes,
                      len);
    }
    trace->scratch(trace->opaque, bytes, address, len, write);
    if (!write) {
        dual_transfer(trace, DSP_IO_SCRATCH, trace->unit, address, false, bytes,
                      len);
    }
}

static void dual_fifo(void *opaque, uint8_t *bytes, unsigned index, size_t len,
                      bool write)
{
    DualTrace *trace = opaque;
    uint32_t id = (trace->unit << 16) | index;
    if (write) {
        dual_transfer(trace, DSP_IO_FIFO, id, 0, true, bytes, len);
    }
    trace->fifo(trace->opaque, bytes, index, len, write);
    if (!write) {
        dual_transfer(trace, DSP_IO_FIFO, id, 0, false, bytes, len);
    }
}

static void dual_setup(MCPXAPUState *apu, uint8_t *ram, bool jit,
                       DspDualFixture fixture, uint32_t value,
                       bool inject_request, bool inject_memory,
                       bool inject_both_skip_dma, DspIoLog *log,
                       DualTrace traces[2])
{
    g_config.audio.use_dsp = true;
    g_config.audio.use_dsp_jit = jit;
    mcpx_apu_dsp_init(apu);
    apu->gp.realtime = apu->ep.realtime = false;
    uint32_t enabled = NV_PAPU_GPRST_GPRST | NV_PAPU_GPRST_GPDSPRST;
    apu->gp.regs[NV_PAPU_GPRST] = fixture == DSP_DUAL_EP_SINK ? 0 : enabled;
    apu->ep.regs[NV_PAPU_EPRST] = fixture == DSP_DUAL_FIFO ? 0 : enabled;
    apu->monitor.point = fixture == DSP_DUAL_EP_SINK ? MCPX_APU_DEBUG_MON_EP :
                                                       MCPX_APU_DEBUG_MON_GP;
    DSPState *units[2] = { apu->gp.dsp, apu->ep.dsp };
    /* Explicitly initialize all guest-readable ordinary memories: backend
     * reset defaults need not be identical, especially the mixbuffer alias. */
    for (unsigned i = 0; i < 2; i++) {
        for (unsigned addr = 0; addr < DSP_XRAM_SIZE; addr++) {
            dsp_write_memory(units[i], 'X', addr, 0);
        }
        for (unsigned addr = 0; addr < DSP_YRAM_SIZE; addr++) {
            dsp_write_memory(units[i], 'Y', addr, 0);
        }
        for (unsigned addr = 0; addr < DSP_PRAM_SIZE; addr++) {
            dsp_write_memory(units[i], 'P', addr, 0);
        }
    }
    if (inject_memory) {
        dsp_write_memory(units[0], 'X', 0x300, 1);
    }
    for (unsigned i = 0; i < 2; i++) {
        if ((fixture == DSP_DUAL_FIFO && i == 1) ||
            (fixture == DSP_DUAL_EP_SINK && i == 0)) {
            continue;
        }
        uint32_t control = fixture == DSP_DUAL_FIFO    ? 0x1802 :
                           fixture == DSP_DUAL_EP_SINK ? 0x402 :
                                                         (i ? 0x19e0 : 0x19e2);
        uint32_t count = fixture == DSP_DUAL_FIFO    ? 2 :
                         fixture == DSP_DUAL_EP_SINK ? 512 :
                                                       1;
        uint32_t node[] = { NODE_POINTER_EOL,
                            control,
                            count,
                            0x200,
                            inject_request && i == 0 ? 4 : 0,
                            0,
                            fixture == DSP_DUAL_SCRATCH ? 31 : 0 };
        for (unsigned j = 0; j < G_N_ELEMENTS(node); j++) {
            dsp_write_memory(units[i], 'X', 0x100 + j, node[j]);
        }
        uint32_t program[] = { 0x08f496, DMA_CONTROL_ACTION_START, 0x08f484, 1,
                               0x0c0002 };
        if (inject_both_skip_dma) {
            program[0] = 0x08f484;
        }
        for (unsigned j = 0; j < G_N_ELEMENTS(program); j++) {
            dsp_write_memory(units[i], 'P', j, program[j]);
        }
        dsp_write_memory(units[i], 'X', 0xffffd4, 0x100);
    }
    if (fixture == DSP_DUAL_EP_SINK) {
        for (unsigned i = 0; i < 512; i++) {
            dsp_write_memory(units[1], 'X', 0x200 + i,
                             i & 1 ? value ^ 0xffffff : value);
        }
        apu->regs[NV_PAPU_EPFADDR] = 0x1000;
        apu->regs[NV_PAPU_EPOFEND0] = 1024;
        stl_le_p(ram + 0x1000, 0x4000);
        memset(ram + 0x4000, 0x77, 1024);
    } else {
        dsp_write_memory(units[0], 'X', 0x200, value);
    }
    if (fixture == DSP_DUAL_FIFO) {
        dsp_write_memory(units[0], 'X', 0x201, value ^ 0xffffff);
        apu->regs[NV_PAPU_GPFADDR] = 0x1000;
        apu->regs[NV_PAPU_GPFMAXSGE] = 1;
        apu->regs[NV_PAPU_GPOFBASE0] = 4092;
        apu->regs[NV_PAPU_GPOFEND0] = 4100;
        apu->regs[NV_PAPU_GPOFCUR0] = 4096;
        stl_le_p(ram + 0x1000, 0x4000);
        stl_le_p(ram + 0x1008, 0x6000);
    } else if (fixture == DSP_DUAL_SCRATCH) {
        apu->regs[NV_PAPU_GPSADDR] = 0x1000;
        apu->regs[NV_PAPU_EPSADDR] = 0x1008;
        stl_le_p(ram + 0x1000, 0x4000);
        stl_le_p(ram + 0x1008, 0x4000);
    }
    for (unsigned i = 0; i < 2; i++) {
        traces[i] = (DualTrace){ .opaque = units[i]->dma.rw_opaque,
                                 .scratch = units[i]->dma.scratch_rw,
                                 .fifo = units[i]->dma.fifo_rw,
                                 .log = log,
                                 .unit = i };
        units[i]->dma.rw_opaque = &traces[i];
        units[i]->dma.scratch_rw = dual_scratch;
        units[i]->dma.fifo_rw = dual_fifo;
    }
}

static bool dual_difference(DspDualResult *result, const char *field,
                            size_t index, uint64_t expected, uint64_t actual)
{
    result->field = field;
    result->index = index;
    result->expected = expected;
    result->actual = actual;
    return false;
}

static bool dual_capture_pc(DSPState *dsp, bool jit, uint32_t *pc)
{
    bool ok;
    if (jit) {
        DspJitCapture *state = g_try_new(DspJitCapture, 1);
        if (!state) {
            return false;
        }
        ok = dsp_jit_capture(dsp, state);
        if (ok) {
            *pc = state->state.pc;
        }
        g_free(state);
    } else {
        DspCCapture *state = g_try_new(DspCCapture, 1);
        if (!state) {
            return false;
        }
        ok = dsp_c_capture(dsp, state);
        if (ok) {
            *pc = state->core.pc;
        }
        g_free(state);
    }
    return ok;
}

typedef struct DualArch {
    uint32_t pc;
    uint32_t registers[DSP_REG_MAX];
    uint32_t stack[2][16];
} DualArch;

static bool dual_capture_arch(DSPState *dsp, bool jit, DualArch *arch)
{
    bool ok;
    if (jit) {
        DspJitCapture *state = g_try_new(DspJitCapture, 1);
        if (!state) {
            return false;
        }
        ok = dsp_jit_capture(dsp, state);
        if (ok) {
            arch->pc = state->state.pc;
            memcpy(arch->registers, state->state.registers,
                   sizeof(arch->registers));
            memcpy(arch->stack, state->state.stack, sizeof(arch->stack));
        }
        g_free(state);
    } else {
        DspCCapture *state = g_try_new(DspCCapture, 1);
        if (!state) {
            return false;
        }
        ok = dsp_c_capture(dsp, state);
        if (ok) {
            arch->pc = state->core.pc;
            memcpy(arch->registers, state->core.registers,
                   sizeof(arch->registers));
            memcpy(arch->stack, state->core.stack, sizeof(arch->stack));
        }
        g_free(state);
    }
    return ok;
}

static bool dual_inject_arch(DSPState *dsp, bool jit, unsigned reg)
{
    bool ok;
    if (jit) {
        DspJitCapture *state = g_try_new(DspJitCapture, 1);
        if (!state) {
            return false;
        }
        ok = dsp_jit_capture(dsp, state);
        if (ok) {
            state->state.registers[reg] ^= 1;
            ok = dsp_jit_restore(dsp, state);
        }
        g_free(state);
    } else {
        DspCCapture *state = g_try_new(DspCCapture, 1);
        if (!state) {
            return false;
        }
        ok = dsp_c_capture(dsp, state);
        if (ok) {
            state->core.registers[reg] ^= 1;
            ok = dsp_c_restore(dsp, state);
        }
        g_free(state);
    }
    return ok;
}

static bool dual_inject_stack(DSPState *dsp, bool jit, unsigned bank)
{
    bool ok;
    if (jit) {
        DspJitCapture *state = g_try_new(DspJitCapture, 1);
        if (!state) {
            return false;
        }
        ok = dsp_jit_capture(dsp, state);
        if (ok) {
            state->state.stack[bank][7] ^= 1;
            ok = dsp_jit_restore(dsp, state);
        }
        g_free(state);
    } else {
        DspCCapture *state = g_try_new(DspCCapture, 1);
        if (!state) {
            return false;
        }
        ok = dsp_c_capture(dsp, state);
        if (ok) {
            state->core.stack[bank][7] ^= 1;
            ok = dsp_c_restore(dsp, state);
        }
        g_free(state);
    }
    return ok;
}

static bool dual_initialize_status(DSPState *dsp)
{
    DspJitCapture *state = g_try_new(DspJitCapture, 1);
    if (!state) {
        return false;
    }
    bool ok = dsp_jit_capture(dsp, state);
    if (ok) {
        state->state.registers[DSP_REG_SR] = 0;
        state->state.registers[DSP_REG_OMR] = 2;
        ok = dsp_jit_restore(dsp, state);
    }
    g_free(state);
    return ok;
}

static bool dual_inject_native_interrupt(DSPState *dsp, bool jit)
{
    bool ok;
    if (jit) {
        DspJitCapture *state = g_try_new(DspJitCapture, 1);
        if (!state) {
            return false;
        }
        ok = dsp_jit_capture(dsp, state);
        if (ok) {
            state->state.interrupts.pending_bits[1] ^= UINT64_C(1) << 63;
            ok = dsp_jit_restore(dsp, state);
        }
        g_free(state);
    } else {
        DspCCapture *state = g_try_new(DspCCapture, 1);
        if (!state) {
            return false;
        }
        ok = dsp_c_capture(dsp, state);
        if (ok) {
            state->core.interrupt_is_pending[0] ^= 1;
            ok = dsp_c_restore(dsp, state);
        }
        g_free(state);
    }
    return ok;
}

static bool dual_compare_native(DspDualResult *result, DSPState *a, DSPState *b,
                                bool jit, unsigned unit)
{
    DspCaptureDifference difference;
    bool captured, equal;
    if (jit) {
        DspJitCapture *x = g_try_new(DspJitCapture, 1);
        DspJitCapture *y = g_try_new(DspJitCapture, 1);
        captured = x && y && dsp_jit_capture(a, x) && dsp_jit_capture(b, y);
        equal = captured && dsp_capture_equal(x, y, &difference);
        g_free(x);
        g_free(y);
    } else {
        DspCCapture *x = g_try_new(DspCCapture, 1);
        DspCCapture *y = g_try_new(DspCCapture, 1);
        captured = x && y && dsp_c_capture(a, x) && dsp_c_capture(b, y);
        equal = captured && dsp_c_capture_equal(x, y, &difference);
        g_free(x);
        g_free(y);
    }
    if (!captured) {
        return false;
    }
    result->native_checkpoint_comparisons++;
    if (!equal) {
        result->component = difference.field;
        return dual_difference(result, unit ? "ep.native" : "gp.native",
                               difference.index, difference.expected,
                               difference.actual);
    }
    return true;
}

static bool dual_compare_events(DspDualResult *result, const DspIoLog *a,
                                const DspIoLog *b)
{
    if (a->count != b->count) {
        return dual_difference(result, "io_event_count",
                               MIN(a->count, b->count), a->count, b->count);
    }
    for (size_t i = 0; i < a->count; i++) {
        const DspIoEvent *x = &a->events[i], *y = &b->events[i];
#define DUAL_EVENT_CHECK(member)                                     \
    do {                                                             \
        if (x->member != y->member) {                                \
            result->component = #member;                             \
            result->sample = x->sample;                              \
            return dual_difference(result, "io_event", i, x->member, \
                                   y->member);                       \
        }                                                            \
    } while (0)
        DUAL_EVENT_CHECK(kind);
        DUAL_EVENT_CHECK(index);
        DUAL_EVENT_CHECK(address);
        DUAL_EVENT_CHECK(write);
        DUAL_EVENT_CHECK(sample);
        DUAL_EVENT_CHECK(length);
#undef DUAL_EVENT_CHECK
        for (size_t j = 0; j < x->length; j++) {
            uint8_t expected = a->bytes[x->offset + j],
                    actual = b->bytes[y->offset + j];
            if (expected != actual) {
                result->component = "data";
                result->sample = x->sample;
                result->byte_offset = j;
                return dual_difference(result, "io_event", i, expected, actual);
            }
        }
    }
    result->io_events = a->count;
    return true;
}

static bool dual_compare_frame(DspDualResult *result, MCPXAPUState *a,
                               MCPXAPUState *b, const uint8_t *ram_a,
                               const uint8_t *ram_b, const DspIoLog *log_a,
                               const DspIoLog *log_b, bool reference_jit,
                               bool candidate_jit)
{
    if (!dual_compare_events(result, log_a, log_b)) {
        return false;
    }
    for (size_t i = 0; i < DUAL_RAM_SIZE; i++) {
        if (ram_a[i] != ram_b[i]) {
            return dual_difference(result, "ram", i, ram_a[i], ram_b[i]);
        }
    }
    for (size_t i = 0; i < G_N_ELEMENTS(a->regs); i++) {
        if (a->regs[i] != b->regs[i]) {
            return dual_difference(result, "apu_register", i, a->regs[i],
                                   b->regs[i]);
        }
    }
    for (size_t i = 0; i < G_N_ELEMENTS(a->gp.regs); i++) {
        if (a->gp.regs[i] != b->gp.regs[i]) {
            return dual_difference(result, "gp.register", i, a->gp.regs[i],
                                   b->gp.regs[i]);
        }
        if (a->ep.regs[i] != b->ep.regs[i]) {
            return dual_difference(result, "ep.register", i, a->ep.regs[i],
                                   b->ep.regs[i]);
        }
    }
    if (a->set_irq != b->set_irq) {
        return dual_difference(result, "apu.irq_pending", 0, a->set_irq,
                               b->set_irq);
    }
    for (size_t i = 0; i < 512; i++) {
        int16_t x = a->monitor.frame_buf[i / 2][i % 2];
        int16_t y = b->monitor.frame_buf[i / 2][i % 2];
        if (x != y) {
            return dual_difference(result, "pcm", i, (uint16_t)x, (uint16_t)y);
        }
        result->pcm_nonzero_comparisons += x != 0;
    }
    DSPState *x[2] = { a->gp.dsp, a->ep.dsp }, *y[2] = { b->gp.dsp, b->ep.dsp };
    for (unsigned i = 0; i < 2; i++) {
        DualArch expected_arch, actual_arch;
        if (!dual_capture_arch(x[i], reference_jit, &expected_arch) ||
            !dual_capture_arch(y[i], candidate_jit, &actual_arch)) {
            return false;
        }
        result->architectural_comparisons++;
        if (expected_arch.pc != actual_arch.pc) {
            return dual_difference(result, i ? "ep.arch.pc" : "gp.arch.pc", 0,
                                   expected_arch.pc, actual_arch.pc);
        }
        for (unsigned reg = DSP_REG_X0; reg <= DSP_REG_LC; reg++) {
            if (reg == DSP_REG_A || reg == DSP_REG_B) {
                continue; /* Accumulators are represented by their components.
                           */
            }
            if (reg > DSP_REG_M7 && reg < DSP_REG_SR) {
                continue; /* Reserved slots and interpreter-only temporaries. */
            }
            uint32_t expected = expected_arch.registers[reg];
            uint32_t actual = actual_arch.registers[reg];
            if (reference_jit != candidate_jit) {
                expected =
                    dsp_replay_normalize_register(reference_jit, reg, expected);
                actual =
                    dsp_replay_normalize_register(candidate_jit, reg, actual);
            }
            result->architectural_comparisons++;
            if (expected != actual) {
                return dual_difference(
                    result, i ? "ep.arch.register" : "gp.arch.register", reg,
                    expected, actual);
            }
        }
        for (unsigned bank = 0; bank < 2; bank++) {
            for (unsigned index = 0; index < 16; index++) {
                result->architectural_comparisons++;
                if (expected_arch.stack[bank][index] !=
                    actual_arch.stack[bank][index]) {
                    return dual_difference(
                        result, i ? "ep.arch.stack" : "gp.arch.stack",
                        bank * 16 + index, expected_arch.stack[bank][index],
                        actual_arch.stack[bank][index]);
                }
            }
        }
        if (reference_jit == candidate_jit &&
            !dual_compare_native(result, x[i], y[i], reference_jit, i)) {
            return false;
        }
        const struct {
            char space;
            unsigned size;
            const char *gp, *ep;
        } spaces[] = {
            { 'X', DSP_XRAM_SIZE, "gp.xram", "ep.xram" },
            { 'Y', DSP_YRAM_SIZE, "gp.yram", "ep.yram" },
            { 'P', DSP_PRAM_SIZE, "gp.pram", "ep.pram" },
        };
        for (unsigned space = 0; space < G_N_ELEMENTS(spaces); space++) {
            for (unsigned address = 0; address < spaces[space].size;
                 address++) {
                uint32_t expected =
                    dsp_read_memory(x[i], spaces[space].space, address);
                uint32_t actual =
                    dsp_read_memory(y[i], spaces[space].space, address);
                result->dsp_memory_word_comparisons++;
                if (expected != actual) {
                    return dual_difference(
                        result, i ? spaces[space].ep : spaces[space].gp,
                        address, expected, actual);
                }
            }
        }
#define DUAL_CHECK(member, label)                                  \
    do {                                                           \
        if (x[i]->member != y[i]->member) {                        \
            return dual_difference(result, label, i, x[i]->member, \
                                   y[i]->member);                  \
        }                                                          \
    } while (0)
        DUAL_CHECK(interrupts, "interrupts");
        DUAL_CHECK(dma.configuration, "dma.configuration");
        DUAL_CHECK(dma.control, "dma.control");
        DUAL_CHECK(dma.start_block, "dma.start_block");
        DUAL_CHECK(dma.next_block, "dma.next_block");
        DUAL_CHECK(dma.dma_read_count, "dma.read_count");
        DUAL_CHECK(dma.error, "dma.error");
        DUAL_CHECK(dma.eol, "dma.eol");
#undef DUAL_CHECK
        if (dsp_get_halt_requested(x[i]) != dsp_get_halt_requested(y[i])) {
            return dual_difference(result, "halt", i,
                                   dsp_get_halt_requested(x[i]),
                                   dsp_get_halt_requested(y[i]));
        }
    }
    return true;
}

/* A small, independently specified contract prevents identical engine faults
 * from turning this narrow synthetic comparison into a false match. */
static int16_t dual_signal_sample(DspDualSignal signal, unsigned frame,
                                  unsigned sample, unsigned channel)
{
    static const int16_t sine[16] = {
        0, 6270,  11585,  15137,  16384,  15137,  11585,  6270,
        0, -6270, -11585, -15137, -16384, -15137, -11585, -6270,
    };
    switch (signal) {
    case DSP_SIGNAL_IMPULSE:
        return sample == 0 ? (channel ? -16384 : 16384) : 0;
    case DSP_SIGNAL_SILENCE:
        return 0;
    case DSP_SIGNAL_ALTERNATING:
        return ((frame + sample + channel) & 1) ? -16384 : 16384;
    case DSP_SIGNAL_RAMP: {
        int value = ((int)sample - 16) * 1024;
        return channel ? -value : value;
    }
    case DSP_SIGNAL_LEFT:
        return channel ? 0 : 16384;
    case DSP_SIGNAL_RIGHT:
        return channel ? -16384 : 0;
    case DSP_SIGNAL_EXTREMES:
        return ((frame + sample + channel) & 1) ? INT16_MIN : INT16_MAX;
    case DSP_SIGNAL_NEAR_ZERO:
        return ((frame + sample + channel) & 1) ? -1 : 1;
    case DSP_SIGNAL_DETERMINISTIC_NOISE: {
        uint32_t value = 0x9e3779b9u ^ frame * 0x85ebca6bu ^
                         sample * 0xc2b2ae35u ^ channel * 0x27d4eb2fu;
        value ^= value << 13;
        value ^= value >> 17;
        value ^= value << 5;
        unsigned word = value >> 16;
        return word < 0x8000 ? word : (int)word - 0x10000;
    }
    case DSP_SIGNAL_SWEPT_SINE: {
        uint64_t index = (uint64_t)frame * NUM_SAMPLES_PER_FRAME + sample;
        unsigned phase = (index * (index + 1) / 2 + channel * 4) & 15;
        return sine[phase];
    }
    }
    g_assert_not_reached();
}

static bool dual_fixture_contract(DspDualResult *result, DspDualFixture fixture,
                                  DspDualSignal signal, uint32_t value,
                                  unsigned frame, MCPXAPUState *apu,
                                  const uint8_t *ram,
                                  const uint8_t *expected_ram,
                                  const DspIoLog *log)
{
    size_t expected_events = fixture == DSP_DUAL_SCRATCH ? 2 : 1;
    if (log->count != expected_events) {
        return dual_difference(result, "fixture.io_event_count", 0,
                               expected_events, log->count);
    }
    for (size_t i = 0; i < expected_events; i++) {
        const DspIoEvent *event = &log->events[i];
        DspIoKind kind =
            fixture == DSP_DUAL_SCRATCH ? DSP_IO_SCRATCH : DSP_IO_FIFO;
        uint32_t index = fixture == DSP_DUAL_EP_SINK ? 0x10000 :
                         fixture == DSP_DUAL_SCRATCH ? i :
                                                       0;
        bool write = fixture != DSP_DUAL_SCRATCH || i == 0;
        size_t length = fixture == DSP_DUAL_EP_SINK ? 1024 :
                        fixture == DSP_DUAL_FIFO    ? 8 :
                                                      4;
        if (event->kind != kind || event->index != index || event->address ||
            event->write != write || event->sample || event->length != length) {
            return dual_difference(result, "fixture.io_event", i, length,
                                   event->length);
        }
        for (size_t j = 0; j < length; j++) {
            uint8_t expected;
            if (fixture == DSP_DUAL_EP_SINK) {
                uint32_t word = ((j / 2) & 1) ? value ^ 0xffffff : value;
                expected = (word >> (8 + 8 * (j % 2))) & 0xff;
            } else {
                uint32_t word = fixture == DSP_DUAL_FIFO && j >= 4 ?
                                    value ^ 0xffffff :
                                    value;
                expected = (word >> (8 * (j % 4))) & 0xff;
            }
            uint8_t actual = log->bytes[event->offset + j];
            if (expected != actual) {
                return dual_difference(result, "fixture.io_event", i, expected,
                                       actual);
            }
        }
    }
    for (size_t i = 0; i < DUAL_RAM_SIZE; i++) {
        if (ram[i] != expected_ram[i]) {
            return dual_difference(result, "fixture.ram", i, expected_ram[i],
                                   ram[i]);
        }
    }
    unsigned offset = fixture == DSP_DUAL_EP_SINK ? 0 : (frame % 8) * 32;
    unsigned count = fixture == DSP_DUAL_EP_SINK ? 256 : 32;
    for (unsigned sample = 0; sample < count; sample++) {
        for (unsigned channel = 0; channel < 2; channel++) {
            uint16_t expected =
                fixture == DSP_DUAL_EP_SINK ?
                    ((channel ? value ^ 0xffffff : value) >> 8) :
                    (uint16_t)dual_signal_sample(signal, frame, sample,
                                                 channel);
            uint16_t actual =
                (uint16_t)apu->monitor.frame_buf[offset + sample][channel];
            if (expected != actual) {
                return dual_difference(result, "fixture.pcm",
                                       (offset + sample) * 2 + channel,
                                       expected, actual);
            }
        }
    }
    return true;
}

DspReplayStatus
dsp_dual_synthetic(MCPXAPUState *memory_owner, DspDualFixture fixture,
                   DspDualSignal signal, unsigned frames, uint32_t value,
                   bool inject, bool inject_pcm, bool inject_request,
                   bool inject_memory, bool inject_register,
                   bool inject_both_skip_dma, bool inject_both_pcm_silence,
                   bool inject_both_corrupt_event, bool inject_both_corrupt_ram,
                   bool inject_architectural, bool inject_initial_state,
                   unsigned inject_address_reg, unsigned inject_stack,
                   unsigned inject_native_interrupt, bool reference_jit,
                   bool candidate_jit, DspDualResult *result)
{
    if (!memory_owner || !memory_owner->ram || !memory_owner->ram_ptr ||
        !result || memory_region_size(memory_owner->ram) != DUAL_RAM_SIZE ||
        !frames || frames > 1024 || value > 0xffffff ||
        fixture > DSP_DUAL_EP_SINK || signal > DSP_SIGNAL_SWEPT_SINE ||
        (fixture == DSP_DUAL_EP_SINK && signal != DSP_SIGNAL_IMPULSE) ||
        inject_stack > 4 || inject_native_interrupt > 2 ||
        (inject_native_interrupt && reference_jit != candidate_jit) ||
        (inject_address_reg && inject_address_reg != DSP_REG_R0 &&
         inject_address_reg != DSP_REG_N0 && inject_address_reg != DSP_REG_M0 &&
         inject_address_reg != DSP_REG_SR)) {
        return DSP_REPLAY_INVALID;
    }
    *result = (DspDualResult){ 0 };
    MCPXAPUState *apu[2] = { g_new0(MCPXAPUState, 1), g_new0(MCPXAPUState, 1) };
    uint8_t *ram[2] = { g_malloc0(DUAL_RAM_SIZE), g_malloc0(DUAL_RAM_SIZE) };
    DspIoEvent *events[2] = { g_new(DspIoEvent, DUAL_MAX_EVENTS),
                              g_new(DspIoEvent, DUAL_MAX_EVENTS) };
    uint8_t *payload[2] = { g_malloc(DUAL_MAX_PAYLOAD),
                            g_malloc(DUAL_MAX_PAYLOAD) };
    DspIoLog logs[2];
    DualTrace traces[2][2];
    for (unsigned i = 0; i < 2; i++) {
        apu[i]->ram = memory_owner->ram;
        apu[i]->ram_ptr = memory_owner->ram_ptr;
        dsp_io_record_init(&logs[i], events[i], DUAL_MAX_EVENTS, payload[i],
                           DUAL_MAX_PAYLOAD);
        bool jit = i == 0 ? reference_jit : candidate_jit;
        dual_setup(apu[i], ram[i], jit, fixture, value ^ (i && inject),
                   i && inject_request, i && inject_memory,
                   inject_both_skip_dma, &logs[i], traces[i]);
    }
    uint8_t *expected_ram = g_memdup2(ram[0], DUAL_RAM_SIZE);
    if (fixture == DSP_DUAL_SCRATCH) {
        stl_le_p(expected_ram + 0x4000, value);
    } else if (fixture == DSP_DUAL_FIFO) {
        stl_le_p(expected_ram + 0x6000, value);
        stl_le_p(expected_ram + 0x4ffc, value ^ 0xffffff);
    } else {
        memset(expected_ram + 0x4000, 0, 1024);
    }
    DspReplayStatus status = DSP_REPLAY_HALTED;
    for (unsigned i = 0; i < 2; i++) {
        bool jit = i == 0 ? reference_jit : candidate_jit;
        if (jit && (!dual_initialize_status(apu[i]->gp.dsp) ||
                    !dual_initialize_status(apu[i]->ep.dsp))) {
            status = DSP_REPLAY_INVALID;
            goto out;
        }
    }
    bool fault_changes_initial =
        inject || inject_request || inject_memory || inject_both_skip_dma;
    if (inject_initial_state &&
        !dual_inject_arch(apu[1]->gp.dsp, candidate_jit, DSP_REG_X0)) {
        status = DSP_REPLAY_INVALID;
        goto out;
    }
    if (!fault_changes_initial || inject_initial_state) {
        if (!dual_compare_frame(result, apu[0], apu[1], ram[0], ram[1],
                                &logs[0], &logs[1], reference_jit,
                                candidate_jit)) {
            result->initial_state_mismatch = result->field != NULL;
            status = result->initial_state_mismatch ? DSP_REPLAY_UNSUPPORTED :
                                                      DSP_REPLAY_INVALID;
            goto out;
        }
        result->initial_subset_checked = true;
    }
    for (unsigned frame = 0; frame < frames; frame++) {
        result->frame = frame;
        for (unsigned i = 0; i < 2; i++) {
            memcpy(memory_owner->ram_ptr, ram[i], DUAL_RAM_SIZE);
            apu[i]->ep_frame_div = frame % 8;
            traces[i][0].sample = traces[i][1].sample =
                frame * NUM_SAMPLES_PER_FRAME;
            float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME] = { 0 };
            for (unsigned channel = 0; channel < 2; channel++) {
                for (unsigned sample = 0; sample < NUM_SAMPLES_PER_FRAME;
                     sample++) {
                    mixbins[channel][sample] =
                        dual_signal_sample(signal, frame, sample, channel) /
                        32768.0f;
                }
            }
            mcpx_apu_dsp_frame(apu[i], mixbins);
            if (inject_both_pcm_silence) {
                memset(apu[i]->monitor.frame_buf, 0,
                       sizeof(apu[i]->monitor.frame_buf));
            }
            if (inject_both_corrupt_event && frame == 0 && logs[i].bytes_used) {
                logs[i].bytes[0] ^= 1;
            }
            if (inject_both_corrupt_ram && frame == 0) {
                memory_owner->ram_ptr[0x8000] ^= 1;
            }
            if (i == 1 && inject_pcm && frame == 0) {
                apu[i]->monitor.frame_buf[0][0] ^= 1;
            }
            if (i == 1 && inject_register && frame == 0) {
                apu[i]->gp.regs[0x80] ^= 1;
            }
            if (i == 1 && inject_architectural && frame == 0 &&
                !dual_inject_arch(apu[i]->gp.dsp, candidate_jit, DSP_REG_X0)) {
                status = DSP_REPLAY_INVALID;
                goto out;
            }
            if (i == 1 && inject_address_reg && frame == 0 &&
                !dual_inject_arch(apu[i]->gp.dsp, candidate_jit,
                                  inject_address_reg)) {
                status = DSP_REPLAY_INVALID;
                goto out;
            }
            if (i == 1 && inject_stack && frame == 0 &&
                !dual_inject_stack(inject_stack > 2 ? apu[i]->ep.dsp :
                                                      apu[i]->gp.dsp,
                                   candidate_jit, (inject_stack - 1) % 2)) {
                status = DSP_REPLAY_INVALID;
                goto out;
            }
            if (i == 1 && inject_native_interrupt && frame == 0 &&
                !dual_inject_native_interrupt(inject_native_interrupt == 2 ?
                                                  apu[i]->ep.dsp :
                                                  apu[i]->gp.dsp,
                                              candidate_jit)) {
                status = DSP_REPLAY_INVALID;
                goto out;
            }
            memcpy(ram[i], memory_owner->ram_ptr, DUAL_RAM_SIZE);
            if (traces[i][0].failed || traces[i][1].failed) {
                status = DSP_REPLAY_INVALID;
                goto out;
            }
            if ((fixture != DSP_DUAL_EP_SINK &&
                 !dsp_get_halt_requested(apu[i]->gp.dsp)) ||
                (fixture != DSP_DUAL_FIFO && frame % 8 == 0 &&
                 !dsp_get_halt_requested(apu[i]->ep.dsp))) {
                status = DSP_REPLAY_TIMEOUT;
                goto out;
            }
        }
        if (!dual_compare_frame(result, apu[0], apu[1], ram[0], ram[1],
                                &logs[0], &logs[1], reference_jit,
                                candidate_jit)) {
            status =
                result->field ? DSP_REPLAY_STATE_MISMATCH : DSP_REPLAY_INVALID;
            goto out;
        }
        if (!dual_fixture_contract(result, fixture, signal, value, frame,
                                   apu[0], ram[0], expected_ram, &logs[0])) {
            status = DSP_REPLAY_STATE_MISMATCH;
            goto out;
        }
    }
out:
    if (status == DSP_REPLAY_STATE_MISMATCH) {
        result->post_subframe_pc_valid =
            dual_capture_pc(apu[0]->gp.dsp, reference_jit,
                            &result->post_subframe_pc[0]) &&
            dual_capture_pc(apu[0]->ep.dsp, reference_jit,
                            &result->post_subframe_pc[1]) &&
            dual_capture_pc(apu[1]->gp.dsp, candidate_jit,
                            &result->post_subframe_pc[2]) &&
            dual_capture_pc(apu[1]->ep.dsp, candidate_jit,
                            &result->post_subframe_pc[3]);
    }
    g_free(expected_ram);
    for (unsigned i = 0; i < 2; i++) {
        dsp_destroy(apu[i]->gp.dsp);
        dsp_destroy(apu[i]->ep.dsp);
        g_free(apu[i]);
        g_free(ram[i]);
        g_free(events[i]);
        g_free(payload[i]);
    }
    return status;
}

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "capture_wire.h"

/* Version 1 is a sequence of fixed-width little-endian values. It contains
 * no C padding, pointers, or implicit host-endian fields. The source ABI and
 * external memory identity must still be bound in the case manifest. */
#define WIRE_SIZE                             \
    (8 + 16 + 4 + 155 + 64 * 4 + 2 * 16 * 4 + \
     (DSP_XRAM_SIZE + DSP_YRAM_SIZE + DSP_PRAM_SIZE) * 4 + 6 * 4 + 3)

typedef struct Wire {
    uint8_t *bytes;
    size_t pos;
    bool decode;
    bool valid;
} Wire;

static uint8_t byte(Wire *wire, uint8_t value)
{
    if (wire->decode) {
        return wire->bytes[wire->pos++];
    }
    wire->bytes[wire->pos++] = value;
    return value;
}

static uint32_t word(Wire *wire, uint32_t value)
{
    uint32_t result = 0;
    for (unsigned i = 0; i < 4; i++) {
        result |= (uint32_t)byte(wire, value >> (8 * i)) << (8 * i);
    }
    return result;
}

static uint64_t wide(Wire *wire, uint64_t value)
{
    uint64_t result = 0;
    for (unsigned i = 0; i < 8; i++) {
        result |= (uint64_t)byte(wire, value >> (8 * i)) << (8 * i);
    }
    return result;
}

static bool boolean(Wire *wire, bool value)
{
    uint8_t result = byte(wire, value ? 1 : 0);
    if (result > 1) {
        wire->valid = false;
    }
    return result == 1;
}

static void transform(Wire *wire, DspJitCapture *s)
{
    static const uint8_t magic[] = { 'X', 'D', 'J', 'T' };
    for (size_t i = 0; i < sizeof(magic); i++) {
        if (byte(wire, magic[i]) != magic[i]) {
            wire->valid = false;
        }
    }
    if (word(wire, 1) != 1) {
        wire->valid = false;
    }
    s->state.pc = word(wire, s->state.pc);
    s->state.pc_advance = word(wire, s->state.pc_advance);
    s->state.cycle_count = word(wire, s->state.cycle_count);
    uint32_t budget = word(wire, (uint32_t)s->state.cycle_budget);
    s->state.cycle_budget = budget <= INT32_MAX ?
                                (int32_t)budget :
                                (int32_t)((int64_t)budget - (INT64_C(1) << 32));
    s->state.pc_on_rep = boolean(wire, s->state.pc_on_rep);
    s->state.loop_rep = boolean(wire, s->state.loop_rep);
    s->state.halt_requested = boolean(wire, s->state.halt_requested);
    s->state.power_state = byte(wire, s->state.power_state);
    if (s->state.power_state > 2) {
        wire->valid = false;
    }
    for (size_t i = 0; i < 2; i++) {
        s->state.interrupts.pending_bits[i] =
            wide(wire, s->state.interrupts.pending_bits[i]);
    }
    s->state.interrupts.pipeline_stage =
        byte(wire, s->state.interrupts.pipeline_stage);
    s->state.interrupts.vector_addr =
        word(wire, s->state.interrupts.vector_addr);
    s->state.interrupts.saved_pc = word(wire, s->state.interrupts.saved_pc);
    s->state.interrupts.state = byte(wire, s->state.interrupts.state);
    if (s->state.interrupts.state > 2) {
        wire->valid = false;
    }
    for (size_t i = 0; i < 128; i++) {
        uint8_t ipl = byte(wire, (uint8_t)s->state.interrupts.ipl[i]);
        s->state.interrupts.ipl[i] = ipl < 128 ? ipl : (int)ipl - 256;
    }
    s->state.interrupts.ipl_to_raise =
        byte(wire, s->state.interrupts.ipl_to_raise);
    for (size_t i = 0; i < 64; i++) {
        s->state.registers[i] = word(wire, s->state.registers[i]);
    }
    for (size_t bank = 0; bank < 2; bank++) {
        for (size_t i = 0; i < 16; i++) {
            s->state.stack[bank][i] = word(wire, s->state.stack[bank][i]);
        }
    }
    for (size_t i = 0; i < DSP_XRAM_SIZE; i++) {
        s->xram[i] = word(wire, s->xram[i]);
    }
    for (size_t i = 0; i < DSP_YRAM_SIZE; i++) {
        s->yram[i] = word(wire, s->yram[i]);
    }
    for (size_t i = 0; i < DSP_PRAM_SIZE; i++) {
        s->pram[i] = word(wire, s->pram[i]);
    }
    s->configuration = word(wire, s->configuration);
    s->control = word(wire, s->control);
    s->start_block = word(wire, s->start_block);
    s->next_block = word(wire, s->next_block);
    s->dma_read_count = word(wire, s->dma_read_count);
    s->interrupts = word(wire, s->interrupts);
    s->error = boolean(wire, s->error);
    s->eol = boolean(wire, s->eol);
    s->is_gp = boolean(wire, s->is_gp);
}

size_t dsp_jit_wire_size(void)
{
    return WIRE_SIZE;
}

bool dsp_jit_wire_encode(const DspJitCapture *capture, uint8_t *buffer,
                         size_t length)
{
    if (!capture || !buffer || length != WIRE_SIZE) {
        return false;
    }
    DspJitCapture *copy = g_try_malloc(sizeof(*copy));
    if (!copy) {
        return false;
    }
    *copy = *capture;
    Wire wire = { .bytes = buffer, .valid = true };
    transform(&wire, copy);
    g_free(copy);
    return wire.valid && wire.pos == WIRE_SIZE;
}

bool dsp_jit_wire_decode(const uint8_t *buffer, size_t length,
                         DspJitCapture *capture)
{
    if (!capture || !buffer || length != WIRE_SIZE) {
        return false;
    }
    DspJitCapture *copy = g_try_malloc0(sizeof(*copy));
    if (!copy) {
        return false;
    }
    Wire wire = { .bytes = (uint8_t *)buffer, .decode = true, .valid = true };
    transform(&wire, copy);
    bool valid = wire.valid && wire.pos == WIRE_SIZE;
    if (valid) {
        *capture = *copy;
    }
    g_free(copy);
    return valid;
}

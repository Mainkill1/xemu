/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "dsp.h"
#include "gp-filter-hle.h"

/* Reserved REP save slot in dsp56300 0.1.3 core::reg::TEMP, not Xbox MMIO. */
#define NATIVE_REP_TEMP 0x2b
#define FILTER_BODY_CYCLES 103
#define FILTER_TAIL_CYCLES 4

struct GPFilterHLE {
    uint32_t program[DSP_PRAM_SIZE];
    bool valid, recognized;
};

GPFilterHLE *gp_filter_hle_new(void)
{
    return g_new0(GPFilterHLE, 1);
}

void gp_filter_hle_free(GPFilterHLE *hle)
{
    g_free(hle);
}

void gp_filter_hle_reset(GPFilterHLE *hle)
{
    if (hle) {
        hle->valid = false;
        hle->recognized = false;
    }
}

bool gp_filter_hle_try_run(GPFilterHLE *hle, Dsp56300State *cpu, uint32_t *xram,
                           uint32_t *yram, const uint32_t *pram, int cycles)
{
    if (!hle || cycles != 1000 || cpu->pc < 0x185 || cpu->pc > 0x413) {
        return false;
    }
    unsigned phase = (cpu->pc - 0x185) % 0x55;
    if (phase != 0 && phase != 0x3b) {
        return false;
    }
    /* Full comparison also detects writes performed by the DSP itself. */
    if (!hle->valid || memcmp(hle->program, pram, sizeof(hle->program))) {
        uint32_t canonical[DSP_PRAM_SIZE];
        for (unsigned i = 0; i < DSP_PRAM_SIZE; i++) {
            canonical[i] = cpu_to_le32(pram[i] & 0xffffff);
        }
        g_autofree char *digest = g_compute_checksum_for_data(
            G_CHECKSUM_SHA256, (const uint8_t *)canonical, sizeof(canonical));
        hle->recognized = strcmp(digest, "f7c1b74d7ff1493a3c52fd2ca032fe744d96f"
                                         "abf56b6dd93de22114c793e525b") == 0;
        memcpy(hle->program, pram, sizeof(hle->program));
        hle->valid = true;
    }
    return hle->recognized &&
           gp_filter_hle_run_verified(cpu, xram, yram, cycles);
}

static int32_t signed24(uint32_t v)
{
    if (v & 0x800000) {
        return (int32_t)(v & 0x7fffff) - 0x800000;
    }
    return (int32_t)v;
}

static int64_t floor_shift(int64_t v, unsigned bits)
{
    return v >= 0 ? v >> bits : -((-v + (INT64_C(1) << bits) - 1) >> bits);
}

static bool fits24(int64_t v)
{
    return v >= -0x800000 && v <= 0x7fffff;
}

static int32_t limit24(int64_t v)
{
    return v < -0x800000 ? -0x800000 : v > 0x7fffff ? 0x7fffff : v;
}

static uint32_t word24(int64_t v)
{
    return (uint32_t)v & 0xffffff;
}

static int64_t section_sum(const int32_t *c, int32_t input, const int32_t *h)
{
    int64_t sum = (int64_t)c[0] * input;
    for (unsigned i = 0; i < 4; i++) {
        sum += (int64_t)c[i + 1] * h[i];
    }
    return sum;
}

bool gp_filter_hle_run_verified(Dsp56300State *cpu, uint32_t *xram,
                                uint32_t *yram, int cycles)
{
    static const uint32_t offsets[8] = { 16, 16, 16, 2, 2, 0, 0, 0 };
    unsigned entry = 0;
    bool tail = false;
    for (unsigned i = 0; i < 8; i++) {
        unsigned pc = 0x185 + i * 0x55;
        if (cpu->pc == pc || cpu->pc == pc + 0x3b) {
            entry = pc;
            tail = cpu->pc != pc;
            break;
        }
    }
    if (!entry || cycles != 1000 || cpu->cycle_budget > 0 ||
        cpu->cycle_budget < -106 || cpu->pc_advance != (tail ? 0 : 1) ||
        cpu->pc_on_rep || cpu->loop_rep || cpu->halt_requested ||
        cpu->power_state || cpu->interrupts.pending_bits[0] ||
        cpu->interrupts.pending_bits[1] || cpu->interrupts.pipeline_stage ||
        cpu->interrupts.state || cpu->interrupts.ipl_to_raise ||
        cpu->registers[DSP_REG_OMR] != 0x300 ||
        (cpu->registers[DSP_REG_SR] & ~0x3f) != 0xc083c0 ||
        (cpu->registers[DSP_REG_SR] & 0x23) != 0 ||
        cpu->registers[DSP_REG_SSH] != entry ||
        cpu->registers[DSP_REG_LA] != entry + 0x3e) {
        return false;
    }
    bool matched_steps =
        (cpu->registers[DSP_REG_N0 + 3] == 1 ||
         cpu->registers[DSP_REG_N0 + 3] == 4) &&
        cpu->registers[DSP_REG_N0 + 4] == cpu->registers[DSP_REG_N0 + 3];
    for (unsigned i = 0; i < 8; i++) {
        if (cpu->registers[DSP_REG_M0 + i] != 0xffffff ||
            (cpu->registers[DSP_REG_N0 + i] != offsets[i] &&
             !(matched_steps && (i == 3 || i == 4)))) {
            return false;
        }
    }
    unsigned count = cpu->registers[DSP_REG_LC];
    unsigned base = cpu->registers[DSP_REG_R5];
    unsigned state = cpu->registers[DSP_REG_R4];
    if (count < 2 || count > 32 || base > DSP_XRAM_SIZE - 19 ||
        state > DSP_YRAM_SIZE - 8 || cpu->registers[DSP_REG_R0] != base + 18 ||
        xram[base] != 0xc000 || xram[base + 2] != state ||
        xram[base + 3] != 8 || xram[base + 4] != 1) {
        return false;
    }
    unsigned input = xram[base + 5], output = xram[base + 6];
    if (input < 0x1400 || input > 0x17e0 || (input & 31) ||
        output != input - 0x800 || (base < output + 32 && output < base + 19) ||
        cpu->registers[DSP_REG_R2] != input + 32 - count + tail ||
        cpu->registers[DSP_REG_R3] != output + 32 - count) {
        return false;
    }

    /* Leave the final hardware DO iteration and stack restoration to the JIT.
     */
    int budget = cpu->cycle_budget + cycles;
    unsigned available =
        tail ? FILTER_TAIL_CYCLES +
                   (count - 2) * (FILTER_BODY_CYCLES + FILTER_TAIL_CYCLES) :
               (count - 1) * (FILTER_BODY_CYCLES + FILTER_TAIL_CYCLES);
    if (budget > available) {
        return false;
    }
    if (tail && ((cpu->registers[DSP_REG_A0] & 0xff000000) ||
                 (cpu->registers[DSP_REG_A1] & 0xff000000) ||
                 cpu->registers[DSP_REG_A2] !=
                     (cpu->registers[DSP_REG_A1] & 0x800000 ? 0xff : 0))) {
        return false;
    }
    int32_t coefficients[11], history[8];
    uint32_t channel[32];
    for (unsigned i = 0; i < 11; i++) {
        if (xram[base + 7 + i] & 0xff000000) {
            return false;
        }
        coefficients[i] = signed24(xram[base + 7 + i]);
    }
    for (unsigned i = 0; i < 8; i++) {
        if (yram[state + i] & 0xff000000) {
            return false;
        }
        history[i] = signed24(yram[state + i]);
    }
    for (unsigned i = 0; i < 32; i++) {
        if (xram[output + i] & 0xff000000) {
            return false;
        }
        channel[i] = xram[output + i];
    }
    Dsp56300State next = *cpu;
    next.cycle_budget = budget;
    uint32_t temporary = xram[base + 18];
    while (next.cycle_budget > 0) {
        unsigned sample = 32 - count;
        if (tail) {
            channel[sample] = next.registers[DSP_REG_A1];
            next.registers[DSP_REG_R3]++;
            count--;
            next.registers[DSP_REG_LC] = count;
            next.pc = entry;
            next.pc_advance = 1;
            next.cycle_count += FILTER_TAIL_CYCLES;
            next.cycle_budget -= FILTER_TAIL_CYCLES;
        } else {
            int32_t x = signed24(channel[sample]);
            int64_t first = section_sum(coefficients + 1, x, history);
            int64_t q1 = floor_shift(first, 22);
            if (!fits24(q1)) {
                return false;
            }
            int32_t v =
                floor_shift((int64_t)limit24(q1 * 16) * coefficients[0], 23);
            if (!fits24(v)) {
                return false;
            }
            int64_t second = section_sum(coefficients + 6, v, history + 4);
            int64_t q2 = floor_shift(second, 22);
            if (!fits24(q2)) {
                return false;
            }
            int64_t accumulator = second * 2;
            uint32_t *r = next.registers;
            r[DSP_REG_X0] = word24(q2);
            r[DSP_REG_X1] = 1;
            r[DSP_REG_Y0] = word24(history[7]);
            r[DSP_REG_Y1] = word24(v);
            r[DSP_REG_A0] = word24(accumulator);
            r[DSP_REG_A1] = word24(floor_shift(accumulator, 24));
            r[DSP_REG_A2] = (uint32_t)floor_shift(accumulator, 48) & 0xff;
            /* U describes the doubled history value, before output scaling. */
            uint32_t q2_word = word24(q2);
            bool unnormalized = !(((q2_word >> 23) ^ (q2_word >> 22)) & 1);
            r[DSP_REG_SR] = (r[DSP_REG_SR] & ~0x3f) |
                            (unnormalized ? 1 << DSP_SR_U : 0) |
                            (second < 0 ? 1 << DSP_SR_N : 0) |
                            (second == 0 ? 1 << DSP_SR_Z : 0);
            r[NATIVE_REP_TEMP] = count;
            r[DSP_REG_R2]++;
            history[1] = history[0];
            history[0] = x;
            history[3] = history[2];
            history[2] = q1;
            history[5] = history[4];
            history[4] = v;
            history[7] = history[6];
            history[6] = q2;
            temporary = word24(x);
            channel[sample] = word24(v);
            next.pc = entry + 0x3b;
            next.pc_advance = 0;
            next.cycle_count += FILTER_BODY_CYCLES;
            next.cycle_budget -= FILTER_BODY_CYCLES;
        }
        tail = !tail;
    }
    /* Commit only after every guard and arithmetic-domain check succeeds. */
    memcpy(xram + output, channel, sizeof(channel));
    for (unsigned i = 0; i < 8; i++) {
        yram[state + i] = word24(history[i]);
    }
    xram[base + 18] = temporary;
    *cpu = next;
    return true;
}

/*
 * QEMU MCPX Audio Processing Unit implementation
 *
 * Copyright (c) 2012 espes
 * Copyright (c) 2018-2019 Jannik Vogel
 * Copyright (c) 2019-2025 Matt Borgerson
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw/xbox/mcpx/apu/apu_int.h"
#include "hw/xbox/mcpx/apu/audio-engine-policy.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "dsp-replay.h"
#include "dsp-jit-state.h"
#include "gp-decimate-shadow.h"
#include "gp-filter-frame-shadow.h"
#include "ep-mix-shadow.h"
#include "ep-mix-identity.h"
#include "ep-native-input.h"
#include "ep-native-project.h"
#include "ep-native-ram.h"
#include "ep-native-sge.h"
#include "ep-native-fifo.h"
#include "ep-native-guard.h"
#include "trace.h"

static const int16_t ep_silence[256][2] = { 0 };

typedef struct EPMixObserver {
    EPMixShadow shadow;
    DSPReplaySnapshot before, after, projected;
    uint8_t projected_pcm[1024];
    uint32_t projected_calls;
    bool projection_ready;
    uint64_t projection_matched, projection_mismatched;
    bool fifo_prepared;
    uint32_t fifo_next;
    uint64_t fifo_preflight_passed, fifo_preflight_skipped;
    uint64_t fifo_cursor_matched, fifo_cursor_mismatched;
    uint64_t entry_guard_passed, entry_guard_rejected;
    char projection_field[96];
    uint64_t frames, backend_skips, unknown_program, bad_overlay;
    uint64_t unexpected_scratch, unexpected_fifo, incomplete;
    uint64_t status_matched, status_mismatched;
    uint64_t stack_matched, stack_mismatched;
    uint64_t registers_matched, registers_mismatched;
    uint32_t preinput[5][256];
    bool preinput_ready;
    uint64_t preinput_prepared, preinput_skipped;
    uint64_t preinput_matched, preinput_mismatched;
    char last_program_digest[65];
} EPMixObserver;

typedef struct EPNativeScratchView {
    EPNativeRAMView ram;
    uint64_t ram_size, sge_base;
    uint32_t max_sge;
} EPNativeScratchView;

static bool ep_native_shadow_scratch_read(void *opaque, uint32_t address,
                                          uint8_t *output, size_t length)
{
    EPNativeScratchView *view = opaque;
    return ep_native_sge_read(&view->ram, ep_native_ram_read, view->ram_size,
                              view->sge_base, view->max_sge, address, output,
                              length);
}

static bool ep_mix_overlay_known(void *opaque, const uint8_t *bytes,
                                 size_t length)
{
    (void)opaque;
    const char *expected = length == 96 ?
        "9494761d6c24f15823888f78fe0e25f081005183f5d492b626003724d916b582" :
        length == 804 ?
        "68da34164ce85698200097ee113446181ee6381aacc3a499ea1ca5c5d058a9d3" :
        NULL;
    if (!expected) {
        return false;
    }
    g_autofree char *digest =
        g_compute_checksum_for_data(G_CHECKSUM_SHA256, bytes, length);
    return !strcmp(digest, expected);
}

static bool ep_mix_program_known(const uint32_t *pram, char *digest_out)
{
    uint32_t canonical[DSP_PRAM_SIZE];
    for (unsigned i = 0; i < DSP_PRAM_SIZE; i++) {
        canonical[i] = cpu_to_le32(ep_mix_identity_word(i, pram[i]));
    }
    g_autofree char *digest = g_compute_checksum_for_data(
        G_CHECKSUM_SHA256, (const uint8_t *)canonical, sizeof(canonical));
    if (digest_out) {
        g_strlcpy(digest_out, digest, 65);
    }
    return ep_mix_digest_known(digest);
}

static void ep_mix_observer_report(const EPMixObserver *o)
{
    const EPMixShadow *s = &o->shadow;
    fprintf(stderr, "EP mix shadow: frames=%" PRIu64 " attempted=%" PRIu64
            " eligible=%" PRIu64 " skipped=%" PRIu64
            " invalidated=%" PRIu64 " matched_frames=%" PRIu64
            " nonzero_matched_frames=%" PRIu64 " mismatched_frames=%" PRIu64
            " matched_samples=%" PRIu64 " mismatched_samples=%" PRIu64
            " matched_words=%" PRIu64 " mismatched_words=%" PRIu64
            " matched_pram_words=%" PRIu64 " mismatched_pram_words=%" PRIu64
            " status_matched=%" PRIu64 " status_mismatched=%" PRIu64
            " stack_matched=%" PRIu64 " stack_mismatched=%" PRIu64
            " registers_matched=%" PRIu64 " registers_mismatched=%" PRIu64
            " preinput_prepared=%" PRIu64 " preinput_skipped=%" PRIu64
            " preinput_matched=%" PRIu64 " preinput_mismatched=%" PRIu64
            " projection_matched=%" PRIu64 " projection_mismatched=%" PRIu64
            " fifo_preflight_passed=%" PRIu64 " fifo_preflight_skipped=%" PRIu64
            " fifo_cursor_matched=%" PRIu64 " fifo_cursor_mismatched=%" PRIu64
            " entry_guard_passed=%" PRIu64 " entry_guard_rejected=%" PRIu64
            " last_projection_field=%s"
            " backend_skips=%" PRIu64 " unknown_program=%" PRIu64
            " bad_overlay=%" PRIu64 " unexpected_scratch=%" PRIu64
            " unexpected_fifo=%" PRIu64 " incomplete=%" PRIu64
            " last_masked_program_sha256=%s\n", o->frames, s->attempted,
            s->eligible, s->skipped, s->invalidated, s->matched,
            s->nonzero_matched, s->mismatched, s->matched_samples,
            s->mismatched_samples, s->matched_words, s->mismatched_words,
            s->matched_pram_words, s->mismatched_pram_words,
            o->status_matched, o->status_mismatched,
            o->stack_matched, o->stack_mismatched,
            o->registers_matched, o->registers_mismatched,
            o->preinput_prepared, o->preinput_skipped,
            o->preinput_matched, o->preinput_mismatched,
            o->projection_matched, o->projection_mismatched,
            o->fifo_preflight_passed, o->fifo_preflight_skipped,
            o->fifo_cursor_matched, o->fifo_cursor_mismatched,
            o->entry_guard_passed, o->entry_guard_rejected,
            o->projection_field[0] ? o->projection_field : "none",
            o->backend_skips, o->unknown_program, o->bad_overlay,
            o->unexpected_scratch, o->unexpected_fifo, o->incomplete,
            o->last_program_digest);
}

static void ep_mix_observer_begin(EPMixObserver *o, MCPXAPUState *d,
                                  DSPState *dsp, bool realtime)
{
    if (!o) {
        return;
    }
    o->preinput_ready = false;
    o->projection_ready = false;
    o->fifo_prepared = false;
    if (++o->frames % 10000 == 0) {
        ep_mix_observer_report(o);
    }
    if (!realtime || !dsp_replay_take_snapshot(dsp, &o->before)) {
        o->backend_skips++;
        return;
    }
    bool known = ep_mix_program_known(o->before.jit.pram,
                                      o->last_program_digest);
    o->unknown_program += !known;
    ep_mix_shadow_begin(&o->shadow, known, o->before.jit.pram);
    if (o->shadow.armed) {
        bool accepted = ep_native_entry_guard(&o->before);
        o->entry_guard_passed += accepted;
        o->entry_guard_rejected += !accepted;
    }
    if (o->shadow.armed && d->ram && d->ram_ptr) {
        EPNativeScratchView view = {
            .ram = { .as = &address_space_memory, .ram = d->ram },
            .ram_size = memory_region_size(d->ram),
            .sge_base = d->regs[NV_PAPU_EPSADDR],
            .max_sge = d->regs[NV_PAPU_EPSMAXSGE],
        };
        o->preinput_ready = ep_native_prepare_input(
            &view, ep_native_shadow_scratch_read, ep_mix_overlay_known,
            o->shadow.before_phase, o->shadow.before_slot, o->preinput);
        o->preinput_prepared += o->preinput_ready;
        o->preinput_skipped += !o->preinput_ready;
        if (o->preinput_ready) {
            o->projection_ready = ep_native_project_frame(
                &o->before, o->preinput, &o->projected, o->projected_pcm,
                &o->projected_calls);
            if (o->projection_ready) {
                uint32_t base = GET_MASK(d->regs[NV_PAPU_EPOFBASE0],
                                         NV_PAPU_GPOFBASE0_VALUE);
                uint32_t end = GET_MASK(d->regs[NV_PAPU_EPOFEND0],
                                        NV_PAPU_GPOFEND0_VALUE);
                uint32_t current = GET_MASK(d->regs[NV_PAPU_EPOFCUR0],
                                            NV_PAPU_GPOFCUR0_VALUE);
                o->fifo_prepared = ep_native_fifo_preflight(
                    &view.ram, ep_native_ram_write_probe, view.ram_size,
                    d->regs[NV_PAPU_EPFADDR], d->regs[NV_PAPU_EPFMAXSGE],
                    base, end, current, sizeof(o->projected_pcm), &o->fifo_next);
                o->fifo_preflight_passed += o->fifo_prepared;
                o->fifo_preflight_skipped += !o->fifo_prepared;
            }
        }
    }
}

static void ep_mix_observer_end(EPMixObserver *o, MCPXAPUState *d, DSPState *dsp,
                                uint64_t calls)
{
    if (!o || !o->shadow.armed) {
        return;
    }
    bool valid = dsp_replay_take_snapshot(dsp, &o->after);
    uint64_t skipped_before = o->shadow.skipped;
    uint64_t eligible_before = o->shadow.eligible;
    ep_mix_shadow_finish(&o->shadow, valid ? o->after.jit.xram : NULL,
                         valid ? o->after.jit.pram : NULL,
                         valid && ep_mix_program_known(o->after.jit.pram, NULL));
    if (o->shadow.eligible != eligible_before) {
        if (o->fifo_prepared) {
            uint32_t current = GET_MASK(d->regs[NV_PAPU_EPOFCUR0],
                                        NV_PAPU_GPOFCUR0_VALUE);
            if (current == o->fifo_next) {
                o->fifo_cursor_matched++;
            } else {
                o->fifo_cursor_mismatched++;
            }
        }
        if (o->projection_ready) {
            const char *field = NULL;
            bool same = dsp_replay_snapshot_equal(&o->projected, &o->after,
                                                   &field);
            if (same && o->projected_calls != calls) {
                same = false;
                field = "calls";
            }
            if (same && memcmp(o->projected_pcm, o->shadow.actual,
                               sizeof(o->projected_pcm))) {
                same = false;
                field = "fifo";
            }
            if (same) {
                o->projection_matched++;
            } else {
                o->projection_mismatched++;
                g_strlcpy(o->projection_field, field ? field : "unknown",
                          sizeof(o->projection_field));
            }
        }
        if (o->preinput_ready) {
            if (!memcmp(o->preinput, o->shadow.input, sizeof(o->preinput))) {
                o->preinput_matched++;
            } else {
                o->preinput_mismatched++;
            }
        }
        EPMixFrame prediction;
        if (ep_mix_frame_hle(o->shadow.input, &prediction)) {
            uint32_t expected = ep_mix_shadow_status(
                o->before.jit.cpu.registers[DSP_REG_SR], &prediction);
            if (expected == o->after.jit.cpu.registers[DSP_REG_SR]) {
                o->status_matched++;
            } else {
                o->status_mismatched++;
            }
            uint32_t expected_stack[2][16];
            ep_mix_shadow_stack(o->before.jit.cpu.stack, o->shadow.before_phase,
                                expected, expected_stack);
            if (!memcmp(expected_stack, o->after.jit.cpu.stack,
                        sizeof(expected_stack))) {
                o->stack_matched++;
            } else {
                o->stack_mismatched++;
            }
            uint32_t expected_regs[64];
            ep_mix_shadow_registers(o->before.jit.cpu.registers,
                                    o->shadow.before_counter,
                                    o->shadow.before_slot,
                                    o->before.jit.cpu.stack[0][0],
                                    o->before.jit.cpu.stack[1][0], &prediction,
                                    expected_regs);
            if (!memcmp(expected_regs, o->after.jit.cpu.registers,
                        sizeof(expected_regs))) {
                o->registers_matched++;
            } else {
                o->registers_mismatched++;
            }
        }
    }
    o->incomplete += o->shadow.skipped - skipped_before;
}

static void ep_mix_observer_scratch(EPMixObserver *o, uint32_t addr,
                                    const uint8_t *ptr, size_t len, bool dir)
{
    if (!o || !o->shadow.armed) {
        return;
    }
    if (dir) {
        o->unexpected_scratch++;
        ep_mix_shadow_reject(&o->shadow);
        return;
    }
    bool known = false;
    bool checking_overlay = !o->shadow.stage && (len == 804 || len == 96);
    if (checking_overlay) {
        g_autofree char *digest = g_compute_checksum_for_data(
            G_CHECKSUM_SHA256, ptr, len);
        const char *expected = len == 96 ?
            "9494761d6c24f15823888f78fe0e25f081005183f5d492b626003724d916b582" :
            "68da34164ce85698200097ee113446181ee6381aacc3a499ea1ca5c5d058a9d3";
        known = !strcmp(digest, expected);
    }
    ep_mix_shadow_read(&o->shadow, addr, ptr, len, known);
    if (!o->shadow.armed) {
        if (checking_overlay && !known) {
            o->bad_overlay++;
        } else {
            o->unexpected_scratch++;
        }
    }
}

typedef struct GPProgramObserver {
    GPDecimateShadow shadow;
    GPFilterFrameShadow filter_shadow;
    DSPJitState before, after;
    uint32_t program[DSP_PRAM_SIZE];
    uint64_t frames, backend_skips, program_skips;
    bool program_valid, recognized, decimate_enabled, filter_enabled;
} GPProgramObserver;

static void gp_program_observer_report(const GPProgramObserver *o)
{
    if (o->decimate_enabled) {
        fprintf(stderr, "GP decimate shadow: frames=%" PRIu64
                " attempted=%" PRIu64 " eligible=%" PRIu64
                " skipped=%" PRIu64 " invalidated=%" PRIu64
                " matched_routes=%" PRIu64 " nonzero_matched_routes=%" PRIu64
                " mismatched_routes=%" PRIu64
                " writes=%" PRIu64 " backend_skips=%" PRIu64
                " program_skips=%" PRIu64 "\n", o->frames,
                o->shadow.attempted, o->shadow.eligible, o->shadow.skipped,
                o->shadow.invalidated, o->shadow.matched,
                o->shadow.nonzero_matched, o->shadow.mismatched,
                o->shadow.writes, o->backend_skips, o->program_skips);
    }
    if (o->filter_enabled) {
        const GPFilterFrameShadow *s = &o->filter_shadow;
        fprintf(stderr, "GP filter-frame shadow: frames=%" PRIu64
                " attempted=%" PRIu64 " eligible=%" PRIu64
                " skipped=%" PRIu64 " invalidated=%" PRIu64
                " matched_channels=%" PRIu64 " nonzero_matched_channels=%" PRIu64
                " mismatched_channels=%" PRIu64 " backend_skips=%" PRIu64
                " program_skips=%" PRIu64 "\n", o->frames, s->attempted,
                s->eligible, s->skipped, s->invalidated, s->matched,
                s->nonzero_matched, s->mismatched, o->backend_skips,
                o->program_skips);
    }
}

static void gp_program_observer_begin(GPProgramObserver *o, DSPState *dsp,
                                      bool realtime)
{
    if (!o) {
        return;
    }
    if (++o->frames % 10000 == 0) {
        gp_program_observer_report(o);
    }
    if (!realtime || !dsp_jit_get_native_state(dsp, &o->before)) {
        o->backend_skips++;
        return;
    }
    if (!o->program_valid ||
        memcmp(o->program, o->before.pram, sizeof(o->program))) {
        uint32_t canonical[DSP_PRAM_SIZE];
        for (unsigned i = 0; i < DSP_PRAM_SIZE; i++) {
            canonical[i] = cpu_to_le32(o->before.pram[i] & 0xffffff);
        }
        g_autofree char *digest = g_compute_checksum_for_data(
            G_CHECKSUM_SHA256, (const uint8_t *)canonical, sizeof(canonical));
        o->recognized = !strcmp(digest,
            "f7c1b74d7ff1493a3c52fd2ca032fe744d96fabf56b6dd93de22114c793e525b");
        memcpy(o->program, o->before.pram, sizeof(o->program));
        o->program_valid = true;
    }
    if (!o->recognized) {
        o->program_skips++;
        return;
    }
    if (o->decimate_enabled) {
        gp_decimate_shadow_prepare(&o->shadow, o->before.xram);
    }
    if (o->filter_enabled) {
        gp_filter_frame_shadow_prepare(&o->filter_shadow, o->before.xram,
                                       o->before.yram);
    }
}

static void gp_program_observer_end(GPProgramObserver *o, DSPState *dsp)
{
    if (!o || (!o->shadow.armed && !o->filter_shadow.armed)) {
        return;
    }
    bool valid = dsp_jit_get_native_state(dsp, &o->after);
    bool stable = valid && !memcmp(o->before.pram, o->after.pram,
                                   sizeof(o->before.pram));
    gp_decimate_shadow_finish(&o->shadow, valid ? o->after.xram : NULL, stable);
    gp_filter_frame_shadow_finish(&o->filter_shadow,
                                  valid ? o->after.xram : NULL,
                                  valid ? o->after.yram : NULL, stable);
}

void mcpx_apu_update_dsp_preference(MCPXAPUState *d)
{
    AudioEnginePolicy policy = audio_engine_policy(d->audio_engine,
                                                   g_config.audio.use_dsp,
                                                   g_config.audio.use_dsp_jit);

    if (d->last_dsp_preference != (int)policy.realtime_dsp) {
        if (policy.realtime_dsp) {
            d->monitor.point = MCPX_APU_DEBUG_MON_GP_OR_EP;
            d->gp.realtime = true;
            d->ep.realtime = true;
        } else {
            d->monitor.point = MCPX_APU_DEBUG_MON_VP;
            d->gp.realtime = false;
            d->ep.realtime = false;
        }
        d->last_dsp_preference = policy.realtime_dsp;
    }

    if (d->last_jit_preference != (int)policy.jit) {
        dsp_set_engine(d->gp.dsp, policy.jit);
        dsp_set_engine(d->ep.dsp, policy.jit);
        d->last_jit_preference = policy.jit;
    }
}

static void scatter_gather_rw(MCPXAPUState *d, hwaddr sge_base,
                              unsigned int max_sge, uint8_t *ptr, uint32_t addr,
                              size_t len, bool dir)
{
    unsigned int page_entry = addr / TARGET_PAGE_SIZE;
    unsigned int offset_in_page = addr % TARGET_PAGE_SIZE;
    unsigned int bytes_to_copy = TARGET_PAGE_SIZE - offset_in_page;

    while (len > 0) {
        assert(page_entry <= max_sge);

        uint32_t prd_address = ldl_le_phys(&address_space_memory,
                                           sge_base + page_entry * 8 + 0);
        // uint32_t prd_control = ldl_le_phys(&address_space_memory,
        //                                     sge_base + page_entry * 8 + 4);

        hwaddr paddr = prd_address + offset_in_page;

        if (bytes_to_copy > len) {
            bytes_to_copy = len;
        }

        assert(paddr + bytes_to_copy < memory_region_size(d->ram));

        if (dir) {
            memcpy(&d->ram_ptr[paddr], ptr, bytes_to_copy);
            memory_region_set_dirty(d->ram, paddr, bytes_to_copy);
        } else {
            memcpy(ptr, &d->ram_ptr[paddr], bytes_to_copy);
        }

        ptr += bytes_to_copy;
        len -= bytes_to_copy;

        /* After the first iteration, we are page aligned */
        page_entry += 1;
        bytes_to_copy = TARGET_PAGE_SIZE;
        offset_in_page = 0;
    }
}

static void gp_scratch_rw(void *opaque, uint8_t *ptr, uint32_t addr, size_t len,
                          bool dir)
{
    MCPXAPUState *d = opaque;
    if (dir && d->gp.program_observer) {
        gp_decimate_shadow_write(&d->gp.program_observer->shadow, addr, ptr,
                                 len);
    }
    // fprintf(stderr, "GP %s scratch 0x%x bytes (0x%x words) at %x (0x%x words)\n", dir ? "writing to" : "reading from", len, len/4, addr, addr/4);
    scatter_gather_rw(d, d->regs[NV_PAPU_GPSADDR], d->regs[NV_PAPU_GPSMAXSGE],
                      ptr, addr, len, dir);
}

static void ep_scratch_rw(void *opaque, uint8_t *ptr, uint32_t addr, size_t len,
                          bool dir)
{
    MCPXAPUState *d = opaque;
    // fprintf(stderr, "EP %s scratch 0x%x bytes (0x%x words) at %x (0x%x words)\n", dir ? "writing to" : "reading from", len, len/4, addr, addr/4);
    scatter_gather_rw(d, d->regs[NV_PAPU_EPSADDR], d->regs[NV_PAPU_EPSMAXSGE],
                      ptr, addr, len, dir);
    ep_mix_observer_scratch(d->ep.mix_observer, addr, ptr, len, dir);
}

static uint32_t circular_scatter_gather_rw(MCPXAPUState *d, hwaddr sge_base,
                                           unsigned int max_sge, uint8_t *ptr,
                                           uint32_t base, uint32_t end,
                                           uint32_t cur, size_t len, bool dir)
{
    while (len > 0) {
        unsigned int bytes_to_copy = end - cur;

        if (bytes_to_copy > len) {
            bytes_to_copy = len;
        }

        DPRINTF("circular scatter gather %s in range 0x%x - 0x%x at 0x%x of "
                "length 0x%x / 0x%lx bytes\n",
                dir ? "write" : "read", base, end, cur, bytes_to_copy, len);

        assert((cur >= base) && ((cur + bytes_to_copy) <= end));
        scatter_gather_rw(d, sge_base, max_sge, ptr, cur, bytes_to_copy, dir);

        ptr += bytes_to_copy;
        len -= bytes_to_copy;

        /* After the first iteration we might have to wrap */
        cur += bytes_to_copy;
        if (cur >= end) {
            assert(cur == end);
            cur = base;
        }
    }

    return cur;
}

static void gp_fifo_rw(void *opaque, uint8_t *ptr, unsigned int index,
                       size_t len, bool dir)
{
    MCPXAPUState *d = opaque;
    uint32_t base;
    uint32_t end;
    hwaddr cur_reg;
    if (dir) {
        assert(index < GP_OUTPUT_FIFO_COUNT);
        base = GET_MASK(d->regs[NV_PAPU_GPOFBASE0 + 0x10 * index],
                        NV_PAPU_GPOFBASE0_VALUE);
        end = GET_MASK(d->regs[NV_PAPU_GPOFEND0 + 0x10 * index],
                       NV_PAPU_GPOFEND0_VALUE);
        cur_reg = NV_PAPU_GPOFCUR0 + 0x10 * index;
    } else {
        assert(index < GP_INPUT_FIFO_COUNT);
        base = GET_MASK(d->regs[NV_PAPU_GPIFBASE0 + 0x10 * index],
                        NV_PAPU_GPOFBASE0_VALUE);
        end = GET_MASK(d->regs[NV_PAPU_GPIFEND0 + 0x10 * index],
                       NV_PAPU_GPOFEND0_VALUE);
        cur_reg = NV_PAPU_GPIFCUR0 + 0x10 * index;
    }

    uint32_t cur = GET_MASK(d->regs[cur_reg], NV_PAPU_GPOFCUR0_VALUE);

    // fprintf(stderr, "GP %s fifo #%d, base = %x, end = %x, cur = %x, len = %x\n",
    //     dir ? "writing to" : "reading from", index,
    //     base, end, cur, len);

    /* DSP hangs if current >= end; but forces current >= base */
    assert(cur < end);
    if (cur < base) {
        cur = base;
    }

    cur = circular_scatter_gather_rw(d,
        d->regs[NV_PAPU_GPFADDR], d->regs[NV_PAPU_GPFMAXSGE],
        ptr, base, end, cur, len, dir);

    SET_MASK(d->regs[cur_reg], NV_PAPU_GPOFCUR0_VALUE, cur);
}

static bool ep_sink_samples(MCPXAPUState *d, uint8_t *ptr, size_t len)
{
    if (d->monitor.point == MCPX_APU_DEBUG_MON_AC97) {
        return false;
    } else if ((d->monitor.point == MCPX_APU_DEBUG_MON_EP) ||
        (d->monitor.point == MCPX_APU_DEBUG_MON_GP_OR_EP)) {
        assert(len == sizeof(d->monitor.frame_buf));
        memcpy(d->monitor.frame_buf, ptr, len);
    }

    return true;
}

static void ep_fifo_rw(void *opaque, uint8_t *ptr, unsigned int index,
                       size_t len, bool dir)
{
    MCPXAPUState *d = opaque;
    if (d->ep.mix_observer && d->ep.mix_observer->shadow.armed) {
        if (dir) {
            ep_mix_shadow_write(&d->ep.mix_observer->shadow, index, ptr, len);
        } else {
            ep_mix_shadow_reject(&d->ep.mix_observer->shadow);
        }
        if (!d->ep.mix_observer->shadow.armed) {
            d->ep.mix_observer->unexpected_fifo++;
        }
    }
    uint32_t base;
    uint32_t end;
    hwaddr cur_reg;
    if (dir) {
        assert(index < EP_OUTPUT_FIFO_COUNT);
        base = GET_MASK(d->regs[NV_PAPU_EPOFBASE0 + 0x10 * index],
                        NV_PAPU_GPOFBASE0_VALUE);
        end = GET_MASK(d->regs[NV_PAPU_EPOFEND0 + 0x10 * index],
                       NV_PAPU_GPOFEND0_VALUE);
        cur_reg = NV_PAPU_EPOFCUR0 + 0x10 * index;
    } else {
        assert(index < EP_INPUT_FIFO_COUNT);
        base = GET_MASK(d->regs[NV_PAPU_EPIFBASE0 + 0x10 * index],
                        NV_PAPU_GPOFBASE0_VALUE);
        end = GET_MASK(d->regs[NV_PAPU_EPIFEND0 + 0x10 * index],
                       NV_PAPU_GPOFEND0_VALUE);
        cur_reg = NV_PAPU_EPIFCUR0 + 0x10 * index;
    }

    uint32_t cur = GET_MASK(d->regs[cur_reg], NV_PAPU_GPOFCUR0_VALUE);

    // fprintf(stderr, "EP %s fifo #%d, base = %x, end = %x, cur = %x, len = %x\n",
    //     dir ? "writing to" : "reading from", index,
    //     base, end, cur, len);

    if (dir && index == 0) {
        bool did_sink = ep_sink_samples(d, ptr, len);
        if (did_sink) {
            /* Since we are sinking, push silence out */
            assert(len <= sizeof(ep_silence));
            ptr = (uint8_t*)ep_silence;
        }
    }

    /* DSP hangs if current >= end; but forces current >= base */
    if (cur >= end) {
        cur = cur % (end - base);
    }
    if (cur < base) {
        cur = base;
    }

    cur = circular_scatter_gather_rw(d,
        d->regs[NV_PAPU_EPFADDR], d->regs[NV_PAPU_EPFMAXSGE],
        ptr, base, end, cur, len, dir);

    SET_MASK(d->regs[cur_reg], NV_PAPU_GPOFCUR0_VALUE, cur);
}

static void proc_rst_write(DSPState *dsp, uint32_t oldval, uint32_t val)
{
    if (!(val & NV_PAPU_GPRST_GPRST) || !(val & NV_PAPU_GPRST_GPDSPRST)) {
        dsp_reset(dsp);
    } else if (
        (!(oldval & NV_PAPU_GPRST_GPRST) || !(oldval & NV_PAPU_GPRST_GPDSPRST))
        && ((val & NV_PAPU_GPRST_GPRST) && (val & NV_PAPU_GPRST_GPDSPRST))) {
        dsp_bootstrap(dsp);
    }
}

/* Global Processor - programmable DSP */
static uint64_t gp_read(void *opaque, hwaddr addr, unsigned int size)
{
    MCPXAPUState *d = opaque;

    assert(size == 4);
    assert(addr % 4 == 0);

    uint64_t r = 0;
    switch (addr) {
    case NV_PAPU_GPXMEM ... NV_PAPU_GPXMEM + 0x1000 * 4 - 1: {
        uint32_t xaddr = (addr - NV_PAPU_GPXMEM) / 4;
        r = dsp_read_memory(d->gp.dsp, 'X', xaddr);
        // fprintf(stderr, "read GP NV_PAPU_GPXMEM [%x] -> %x\n", xaddr, r);
        break;
    }
    case NV_PAPU_GPMIXBUF ... NV_PAPU_GPMIXBUF + 0x400 * 4 - 1: {
        uint32_t xaddr = (addr - NV_PAPU_GPMIXBUF) / 4;
        r = dsp_read_memory(d->gp.dsp, 'X', GP_DSP_MIXBUF_BASE + xaddr);
        // fprintf(stderr, "read GP NV_PAPU_GPMIXBUF [%x] -> %x\n", xaddr, r);
        break;
    }
    case NV_PAPU_GPYMEM ... NV_PAPU_GPYMEM + 0x800 * 4 - 1: {
        uint32_t yaddr = (addr - NV_PAPU_GPYMEM) / 4;
        r = dsp_read_memory(d->gp.dsp, 'Y', yaddr);
        // fprintf(stderr, "read GP NV_PAPU_GPYMEM [%x] -> %x\n", yaddr, r);
        break;
    }
    case NV_PAPU_GPPMEM ... NV_PAPU_GPPMEM + 0x1000 * 4 - 1: {
        uint32_t paddr = (addr - NV_PAPU_GPPMEM) / 4;
        r = dsp_read_memory(d->gp.dsp, 'P', paddr);
        // fprintf(stderr, "read GP NV_PAPU_GPPMEM [%x] -> %x\n", paddr, r);
        break;
    }
    default:
        r = d->gp.regs[addr];
        break;
    }
    DPRINTF("mcpx apu GP: read [0x%" HWADDR_PRIx "] -> 0x%lx\n", addr, r);

    return r;
}

static void gp_write(void *opaque, hwaddr addr, uint64_t val, unsigned int size)
{
    MCPXAPUState *d = opaque;

    qemu_mutex_lock(&d->lock);

    assert(size == 4);
    assert(addr % 4 == 0);

    DPRINTF("mcpx apu GP: [0x%" HWADDR_PRIx "] = 0x%lx\n", addr, val);

    switch (addr) {
    case NV_PAPU_GPXMEM ... NV_PAPU_GPXMEM + 0x1000 * 4 - 1: {
        uint32_t xaddr = (addr - NV_PAPU_GPXMEM) / 4;
        // fprintf(stderr, "gp write xmem %x = %x\n", xaddr, val);
        dsp_write_memory(d->gp.dsp, 'X', xaddr, val);
        break;
    }
    case NV_PAPU_GPMIXBUF ... NV_PAPU_GPMIXBUF + 0x400 * 4 - 1: {
        uint32_t xaddr = (addr - NV_PAPU_GPMIXBUF) / 4;
        // fprintf(stderr, "gp write xmixbuf %x = %x\n", xaddr, val);
        dsp_write_memory(d->gp.dsp, 'X', GP_DSP_MIXBUF_BASE + xaddr, val);
        break;
    }
    case NV_PAPU_GPYMEM ... NV_PAPU_GPYMEM + 0x800 * 4 - 1: {
        uint32_t yaddr = (addr - NV_PAPU_GPYMEM) / 4;
        // fprintf(stderr, "gp write ymem %x = %x\n", yaddr, val);
        dsp_write_memory(d->gp.dsp, 'Y', yaddr, val);
        break;
    }
    case NV_PAPU_GPPMEM ... NV_PAPU_GPPMEM + 0x1000 * 4 - 1: {
        uint32_t paddr = (addr - NV_PAPU_GPPMEM) / 4;
        // fprintf(stderr, "gp write pmem %x = %x\n", paddr, val);
        dsp_write_memory(d->gp.dsp, 'P', paddr, val);
        break;
    }
    case NV_PAPU_GPRST:
        proc_rst_write(d->gp.dsp, d->gp.regs[NV_PAPU_GPRST], val);
        d->gp.regs[NV_PAPU_GPRST] = val;
        break;
    default:
        d->gp.regs[addr] = val;
        break;
    }

    qemu_mutex_unlock(&d->lock);
}

const MemoryRegionOps gp_ops = {
    .read = gp_read,
    .write = gp_write,
};

/* Encode Processor - encoding DSP */
static uint64_t ep_read(void *opaque, hwaddr addr, unsigned int size)
{
    MCPXAPUState *d = opaque;

    assert(size == 4);
    assert(addr % 4 == 0);

    uint64_t r = 0;
    switch (addr) {
    case NV_PAPU_EPXMEM ... NV_PAPU_EPXMEM + 0xC00 * 4 - 1: {
        uint32_t xaddr = (addr - NV_PAPU_EPXMEM) / 4;
        r = dsp_read_memory(d->ep.dsp, 'X', xaddr);
        // fprintf(stderr, "read EP  NV_PAPU_EPXMEM [%x] -> %x\n", xaddr, r);
        break;
    }
    case NV_PAPU_EPYMEM ... NV_PAPU_EPYMEM + 0x100 * 4 - 1: {
        uint32_t yaddr = (addr - NV_PAPU_EPYMEM) / 4;
        r = dsp_read_memory(d->ep.dsp, 'Y', yaddr);
        // fprintf(stderr, "read EP  NV_PAPU_EPYMEM [%x] -> %x\n", yaddr, r);
        break;
    }
    case NV_PAPU_EPPMEM ... NV_PAPU_EPPMEM + 0x1000 * 4 - 1: {
        uint32_t paddr = (addr - NV_PAPU_EPPMEM) / 4;
        r = dsp_read_memory(d->ep.dsp, 'P', paddr);
        // fprintf(stderr, "read EP  NV_PAPU_EPPMEM [%x] -> %x\n", paddr, r);
        break;
    }
    default:
        r = d->ep.regs[addr];
        break;
    }
    DPRINTF("mcpx apu EP: read [0x%" HWADDR_PRIx "] -> 0x%lx\n", addr, r);

    return r;
}

static void ep_write(void *opaque, hwaddr addr, uint64_t val, unsigned int size)
{
    MCPXAPUState *d = opaque;

    qemu_mutex_lock(&d->lock);

    assert(size == 4);
    assert(addr % 4 == 0);

    DPRINTF("mcpx apu EP: [0x%" HWADDR_PRIx "] = 0x%lx\n", addr, val);

    switch (addr) {
    case NV_PAPU_EPXMEM ... NV_PAPU_EPXMEM + 0xC00 * 4 - 1: {
        uint32_t xaddr = (addr - NV_PAPU_EPXMEM) / 4;
        dsp_write_memory(d->ep.dsp, 'X', xaddr, val);
        // fprintf(stderr, "ep write xmem %x = %x\n", xaddr, val);
        break;
    }
    case NV_PAPU_EPYMEM ... NV_PAPU_EPYMEM + 0x100 * 4 - 1: {
        uint32_t yaddr = (addr - NV_PAPU_EPYMEM) / 4;
        dsp_write_memory(d->ep.dsp, 'Y', yaddr, val);
        // fprintf(stderr, "ep write ymem %x = %x\n", yaddr, val);
        break;
    }
    case NV_PAPU_EPPMEM ... NV_PAPU_EPPMEM + 0x1000 * 4 - 1: {
        uint32_t paddr = (addr - NV_PAPU_EPPMEM) / 4;
        // fprintf(stderr, "ep write pmem %x = %x\n", paddr, val);
        dsp_write_memory(d->ep.dsp, 'P', paddr, val);
        break;
    }
    case NV_PAPU_EPRST:
        proc_rst_write(d->ep.dsp, d->ep.regs[NV_PAPU_EPRST], val);
        d->ep.regs[NV_PAPU_EPRST] = val;
        d->ep_frame_div = 0; /* FIXME: Still unsure about frame sync */
        break;
    default:
        d->ep.regs[addr] = val;
        break;
    }

    qemu_mutex_unlock(&d->lock);
}

const MemoryRegionOps ep_ops = {
    .read = ep_read,
    .write = ep_write,
};

void mcpx_apu_dsp_frame(MCPXAPUState *d, float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME])
{
    bool profile = trace_event_get_state_backends(TRACE_MCPX_APU_DSP_FRAME);

    /* Write VP results to the GP DSP MIXBUF */
    for (int mixbin = 0; mixbin < NUM_MIXBINS; mixbin++) {
        uint32_t base = GP_DSP_MIXBUF_BASE + mixbin * NUM_SAMPLES_PER_FRAME;
        for (int sample = 0; sample < NUM_SAMPLES_PER_FRAME; sample++) {
            dsp_write_memory(d->gp.dsp, 'X', base + sample,
                             float_to_24b(mixbins[mixbin][sample]));
        }
    }

    bool ep_enabled = (d->ep.regs[NV_PAPU_EPRST] & NV_PAPU_GPRST_GPRST) &&
                      (d->ep.regs[NV_PAPU_EPRST] & NV_PAPU_GPRST_GPDSPRST);

    /* Run GP */
    if ((d->gp.regs[NV_PAPU_GPRST] & NV_PAPU_GPRST_GPRST) &&
        (d->gp.regs[NV_PAPU_GPRST] & NV_PAPU_GPRST_GPDSPRST)) {
        int64_t start_ns = profile ?
            qemu_clock_get_ns(QEMU_CLOCK_REALTIME) : 0;

        dsp_start_frame(d->gp.dsp);
        dsp_set_halt_requested(d->gp.dsp, false);
        dsp_set_cycle_count(d->gp.dsp, 0);
        DSPReplay *capture = dsp_replay_begin(d->gp.dsp, d->gp.realtime);
        uint64_t calls = 0;

        gp_program_observer_begin(d->gp.program_observer, d->gp.dsp,
                                  d->gp.realtime);

        do {
            dsp_run(d->gp.dsp, 1000);
            if (capture) {
                calls++;
            }
        } while (!dsp_get_halt_requested(d->gp.dsp) && d->gp.realtime);
        gp_program_observer_end(d->gp.program_observer, d->gp.dsp);
        g_dbg.gp.cycles = dsp_get_cycle_count(d->gp.dsp);
        if (profile) {
            int64_t elapsed_ns =
                qemu_clock_get_ns(QEMU_CLOCK_REALTIME) - start_ns;

            trace_mcpx_apu_dsp_frame(true, d->gp.realtime, g_dbg.gp.cycles,
                                    elapsed_ns);
        }
        if (capture) {
            Error *err = NULL;

            if (!dsp_replay_end(capture, calls, &err)) {
                error_prepend(&err, "GP replay capture: ");
                error_report_err(err);
            }
        }

        if ((d->monitor.point == MCPX_APU_DEBUG_MON_GP) ||
            (d->monitor.point == MCPX_APU_DEBUG_MON_GP_OR_EP && !ep_enabled)) {
            int off = (d->ep_frame_div % 8) * NUM_SAMPLES_PER_FRAME;
            for (int i = 0; i < NUM_SAMPLES_PER_FRAME; i++) {
                uint32_t l = dsp_read_memory(d->gp.dsp, 'X', 0x1400 + i);
                d->monitor.frame_buf[off + i][0] = l >> 8;
                uint32_t r =
                    dsp_read_memory(d->gp.dsp, 'X', 0x1400 + 1 * 0x20 + i);
                d->monitor.frame_buf[off + i][1] = r >> 8;
            }
        }
    }

    /* Run EP */
    if ((d->ep.regs[NV_PAPU_EPRST] & NV_PAPU_GPRST_GPRST) &&
        (d->ep.regs[NV_PAPU_EPRST] & NV_PAPU_GPRST_GPDSPRST)) {
        if (d->ep_frame_div % 8 == 0) {
            int64_t start_ns = profile ?
                qemu_clock_get_ns(QEMU_CLOCK_REALTIME) : 0;

            dsp_start_frame(d->ep.dsp);
            dsp_set_halt_requested(d->ep.dsp, false);
            dsp_set_cycle_count(d->ep.dsp, 0);
            DSPReplay *capture = dsp_replay_begin(d->ep.dsp, d->ep.realtime);
            uint64_t calls = 0;
            ep_mix_observer_begin(d->ep.mix_observer, d, d->ep.dsp,
                                  d->ep.realtime);
            do {
                dsp_run(d->ep.dsp, 1000);
                if (capture || d->ep.mix_observer) {
                    calls++;
                }
            } while (!dsp_get_halt_requested(d->ep.dsp) && d->ep.realtime);
            ep_mix_observer_end(d->ep.mix_observer, d, d->ep.dsp, calls);
            if (capture) {
                Error *err = NULL;
                if (!dsp_replay_end(capture, calls, &err)) {
                    error_prepend(&err, "EP replay capture: ");
                    error_report_err(err);
                }
            }
            g_dbg.ep.cycles = dsp_get_cycle_count(d->ep.dsp);
            if (profile) {
                int64_t elapsed_ns =
                    qemu_clock_get_ns(QEMU_CLOCK_REALTIME) - start_ns;

                trace_mcpx_apu_dsp_frame(false, d->ep.realtime, g_dbg.ep.cycles,
                                        elapsed_ns);
            }
        }
    }
}

void mcpx_apu_dsp_init(MCPXAPUState *d)
{
    const char *shadow = g_getenv("XEMU_GP_DECIMATE_SHADOW");
    const char *filter_shadow = g_getenv("XEMU_GP_FILTER_FRAME_SHADOW");
    const char *ep_shadow = g_getenv("XEMU_EP_MIX_SHADOW");
    bool decimate_enabled = shadow && !strcmp(shadow, "1");
    bool filter_enabled = filter_shadow && !strcmp(filter_shadow, "1");
    if (decimate_enabled || filter_enabled) {
        d->gp.program_observer = g_new0(GPProgramObserver, 1);
        d->gp.program_observer->decimate_enabled = decimate_enabled;
        d->gp.program_observer->filter_enabled = filter_enabled;
    }
    if (ep_shadow && !strcmp(ep_shadow, "1")) {
        d->ep.mix_observer = g_new0(EPMixObserver, 1);
    }
    d->last_dsp_preference = -1;
    d->last_jit_preference = -1;
    d->gp.dsp = dsp_init(d, gp_scratch_rw, gp_fifo_rw, true);
    dsp_set_halt_requested(d->gp.dsp, false);
    dsp_set_cycle_count(d->gp.dsp, 0);

    d->ep.dsp = dsp_init(d, ep_scratch_rw, ep_fifo_rw, false);
    dsp_set_halt_requested(d->ep.dsp, false);
    dsp_set_cycle_count(d->ep.dsp, 0);

    /* Until DSP is more performant, a switch to decide whether or not we should
     * use the full audio pipeline or not.
     */
    mcpx_apu_update_dsp_preference(d);
    dsp_replay_init(d->gp.dsp);
    dsp_replay_init(d->ep.dsp);
}

void mcpx_apu_dsp_finalize(MCPXAPUState *d)
{
    GPProgramObserver *o = d->gp.program_observer;
    if (o) {
        gp_program_observer_report(o);
        g_free(o);
        d->gp.program_observer = NULL;
    }
    EPMixObserver *ep = d->ep.mix_observer;
    if (ep) {
        ep_mix_observer_report(ep);
        g_free(ep);
        d->ep.mix_observer = NULL;
    }
}

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "hw/xbox/mcpx/apu/apu_int.h"
#include "dsp-replay-frame.h"

#define MAX_FRAME_MEMORY (16 * 1024 * 1024)
#define MAX_MAPPED_MEMORY (128 * 1024 * 1024)

typedef struct MappedTrace {
    void *original_opaque;
    dsp_scratch_rw_func scratch;
    dsp_fifo_rw_func fifo;
    DspIoLog *io;
    uint64_t sample;
    unsigned unit;
    bool failed;
} MappedTrace;

static void mapped_trace_transfer(MappedTrace *trace, DspIoKind kind,
                                  uint32_t index, uint32_t address, bool write,
                                  const uint8_t *data, size_t length)
{
    if (trace->failed) {
        return;
    }
    bool ok = trace->io->recording ?
                  dsp_io_record(trace->io, kind, index, address, write,
                                trace->sample, data, length) :
                  dsp_io_expect(trace->io, kind, index, address, write,
                                trace->sample, data, length);
    trace->failed = !ok;
}

static void mapped_trace_scratch(void *opaque, uint8_t *data, uint32_t address,
                                 size_t length, bool write)
{
    MappedTrace *trace = opaque;
    if (write) {
        mapped_trace_transfer(trace, DSP_IO_SCRATCH, trace->unit, address, true,
                              data, length);
    }
    trace->scratch(trace->original_opaque, data, address, length, write);
    if (!write) {
        mapped_trace_transfer(trace, DSP_IO_SCRATCH, trace->unit, address,
                              false, data, length);
    }
}

static void mapped_trace_fifo(void *opaque, uint8_t *data, unsigned index,
                              size_t length, bool write)
{
    MappedTrace *trace = opaque;
    uint32_t event_index = (trace->unit << 16) | index;
    if (write) {
        mapped_trace_transfer(trace, DSP_IO_FIFO, event_index, 0, true, data,
                              length);
    }
    trace->fifo(trace->original_opaque, data, index, length, write);
    if (!write) {
        mapped_trace_transfer(trace, DSP_IO_FIFO, event_index, 0, false, data,
                              length);
    }
}

static DspReplayStatus
mapped_sequence(MCPXAPUState *apu, const DspMappedFrame *initial, size_t count,
                const float (*mixbins)[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME],
                const uint8_t *ram, size_t ram_size, DspMappedFrame *result,
                uint8_t *result_ram, int16_t (*pcm)[256][2],
                size_t *failed_frame, bool advance_phase, DspIoLog *io,
                DspFrameCheck check, void *opaque)
{
    *failed_frame = SIZE_MAX;
    if (!apu || !initial || !mixbins || !ram || !result || !result_ram ||
        !count || count > 1024 || (advance_phase && !pcm) || !apu->ram ||
        !apu->ram_ptr || !apu->gp.dsp || !apu->ep.dsp || !ram_size ||
        ram_size > MAX_MAPPED_MEMORY ||
        memory_region_size(apu->ram) != ram_size ||
        memory_region_get_ram_ptr(apu->ram) != apu->ram_ptr ||
        !initial->dsp.gp.is_gp || initial->dsp.ep.is_gp || initial->phase > 7 ||
        initial->monitor_point < MCPX_APU_DEBUG_MON_AC97 ||
        initial->monitor_point > MCPX_APU_DEBUG_MON_GP_OR_EP ||
        (io && (!io->valid || io->mismatch || io->cursor ||
                (io->recording && (io->count || io->bytes_used))))) {
        return DSP_REPLAY_INVALID;
    }
    for (size_t frame = 0; frame < count; frame++) {
        for (size_t i = 0; i < NUM_MIXBINS; i++) {
            for (size_t j = 0; j < NUM_SAMPLES_PER_FRAME; j++) {
                if (!isfinite(mixbins[frame][i][j])) {
                    *failed_frame = frame;
                    return DSP_REPLAY_INVALID;
                }
            }
        }
    }
    DspMappedFrame *temporary = g_try_new(DspMappedFrame, 1);
    int16_t (*staged_pcm)[256][2] =
        pcm ? g_try_new(typeof(*staged_pcm), count) : NULL;
    if (!temporary || (pcm && !staged_pcm)) {
        g_free(temporary);
        g_free(staged_pcm);
        return DSP_REPLAY_INVALID;
    }
    DspReplayStatus status = DSP_REPLAY_INVALID;
    MappedTrace traces[2] = { 0 };
    bool trace_installed = false;
    if (!dsp_jit_restore(apu->gp.dsp, &initial->dsp.gp) ||
        !dsp_jit_restore(apu->ep.dsp, &initial->dsp.ep)) {
        goto out;
    }
    if (io) {
        DSPState *units[2] = { apu->gp.dsp, apu->ep.dsp };
        for (unsigned i = 0; i < 2; i++) {
            traces[i] =
                (MappedTrace){ .original_opaque = units[i]->dma.rw_opaque,
                               .scratch = units[i]->dma.scratch_rw,
                               .fifo = units[i]->dma.fifo_rw,
                               .io = io,
                               .unit = i };
            units[i]->dma.rw_opaque = &traces[i];
            units[i]->dma.scratch_rw = mapped_trace_scratch;
            units[i]->dma.fifo_rw = mapped_trace_fifo;
        }
        trace_installed = true;
    }
    memmove(apu->ram_ptr, ram, ram_size);
    memcpy(apu->regs, initial->apu_regs, sizeof(apu->regs));
    apu->gp.regs[NV_PAPU_GPRST] = initial->gp_reset;
    apu->ep.regs[NV_PAPU_EPRST] = initial->ep_reset;
    apu->gp.realtime = initial->gp_realtime;
    apu->ep.realtime = initial->ep_realtime;
    apu->monitor.point = initial->monitor_point;
    memcpy(apu->monitor.frame_buf, initial->dsp.pcm,
           sizeof(apu->monitor.frame_buf));
    uint32_t enabled = NV_PAPU_GPRST_GPRST | NV_PAPU_GPRST_GPDSPRST;
    bool gp_enabled = (initial->gp_reset & enabled) == enabled;
    bool ep_enabled = (initial->ep_reset & enabled) == enabled;
    for (size_t frame = 0; frame < count; frame++) {
        apu->ep_frame_div = (initial->phase + frame) % 8;
        traces[0].sample = traces[1].sample = frame * NUM_SAMPLES_PER_FRAME;
        float inputs[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME];
        memcpy(inputs, mixbins[frame], sizeof(inputs));
        mcpx_apu_dsp_frame(apu, inputs);
        if (io && (traces[0].failed || traces[1].failed)) {
            *failed_frame = frame;
            status = DSP_REPLAY_IO_MISMATCH;
            goto out;
        }
        if ((gp_enabled && !dsp_get_halt_requested(apu->gp.dsp)) ||
            (ep_enabled && !apu->ep_frame_div &&
             !dsp_get_halt_requested(apu->ep.dsp))) {
            *failed_frame = frame;
            status = DSP_REPLAY_TIMEOUT;
            goto out;
        }
        if (check) {
            if (!dsp_jit_capture(apu->gp.dsp, &temporary->dsp.gp) ||
                !dsp_jit_capture(apu->ep.dsp, &temporary->dsp.ep)) {
                *failed_frame = frame;
                goto out;
            }
            if (!check(opaque, frame, &temporary->dsp.gp, &temporary->dsp.ep,
                       apu->monitor.frame_buf)) {
                *failed_frame = frame;
                status = DSP_REPLAY_STATE_MISMATCH;
                goto out;
            }
        }
        if (staged_pcm) {
            memcpy(staged_pcm[frame], apu->monitor.frame_buf,
                   sizeof(*staged_pcm));
        }
    }
    if (io && !dsp_io_complete(io)) {
        status = DSP_REPLAY_IO_MISMATCH;
        goto out;
    }
    if (!dsp_jit_capture(apu->gp.dsp, &temporary->dsp.gp) ||
        !dsp_jit_capture(apu->ep.dsp, &temporary->dsp.ep)) {
        goto out;
    }
    memcpy(temporary->dsp.pcm, apu->monitor.frame_buf,
           sizeof(temporary->dsp.pcm));
    memcpy(temporary->apu_regs, apu->regs, sizeof(temporary->apu_regs));
    temporary->gp_reset = apu->gp.regs[NV_PAPU_GPRST];
    temporary->ep_reset = apu->ep.regs[NV_PAPU_EPRST];
    temporary->gp_realtime = apu->gp.realtime;
    temporary->ep_realtime = apu->ep.realtime;
    temporary->monitor_point = apu->monitor.point;
    temporary->phase =
        advance_phase ? (initial->phase + count) % 8 : initial->phase;
    *result = *temporary;
    memmove(result_ram, apu->ram_ptr, ram_size);
    if (pcm) {
        memcpy(pcm, staged_pcm, count * sizeof(*pcm));
    }
    status = DSP_REPLAY_HALTED;
out:
    if (trace_installed) {
        DSPState *units[2] = { apu->gp.dsp, apu->ep.dsp };
        for (unsigned i = 0; i < 2; i++) {
            units[i]->dma.rw_opaque = traces[i].original_opaque;
            units[i]->dma.scratch_rw = traces[i].scratch;
            units[i]->dma.fifo_rw = traces[i].fifo;
        }
    }
    g_free(temporary);
    g_free(staged_pcm);
    return status;
}

DspReplayStatus
dsp_replay_mapped_frame(MCPXAPUState *apu, const DspMappedFrame *initial,
                        const float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME],
                        const uint8_t *ram, size_t ram_size,
                        DspMappedFrame *result, uint8_t *result_ram)
{
    size_t failed_frame;
    return mapped_sequence(
        apu, initial, 1,
        (const float (*)[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME])mixbins, ram,
        ram_size, result, result_ram, NULL, &failed_frame, false, NULL, NULL,
        NULL);
}

DspReplayStatus dsp_replay_mapped_sequence(
    MCPXAPUState *apu, const DspMappedFrame *initial, size_t count,
    const float (*mixbins)[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME],
    const uint8_t *ram, size_t ram_size, DspMappedFrame *result,
    uint8_t *result_ram, int16_t (*pcm)[256][2], size_t *failed_frame)
{
    if (!failed_frame) {
        return DSP_REPLAY_INVALID;
    }
    return mapped_sequence(apu, initial, count, mixbins, ram, ram_size, result,
                           result_ram, pcm, failed_frame, true, NULL, NULL,
                           NULL);
}

DspReplayStatus dsp_replay_mapped_trace(
    MCPXAPUState *apu, const DspMappedFrame *initial, size_t count,
    const float (*mixbins)[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME],
    const uint8_t *ram, size_t ram_size, DspMappedFrame *result,
    uint8_t *result_ram, int16_t (*pcm)[256][2], size_t *failed_frame,
    DspIoLog *io)
{
    if (!failed_frame || !io) {
        return DSP_REPLAY_INVALID;
    }
    return mapped_sequence(apu, initial, count, mixbins, ram, ram_size, result,
                           result_ram, pcm, failed_frame, true, io, NULL, NULL);
}

DspReplayStatus dsp_replay_mapped_trace_checked(
    MCPXAPUState *apu, const DspMappedFrame *initial, size_t count,
    const float (*mixbins)[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME],
    const uint8_t *ram, size_t ram_size, DspMappedFrame *result,
    uint8_t *result_ram, int16_t (*pcm)[256][2], size_t *failed_frame,
    DspIoLog *io, DspFrameCheck check, void *opaque)
{
    if (!failed_frame || !io || !check) {
        return DSP_REPLAY_INVALID;
    }
    return mapped_sequence(apu, initial, count, mixbins, ram, ram_size, result,
                           result_ram, pcm, failed_frame, true, io, check,
                           opaque);
}

DspReplayStatus dsp_replay_gp_ep_frame(
    const DspJitCapture *gp, const DspJitCapture *ep, unsigned phase,
    const float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME],
    const int16_t prior_pcm[256][2], const DspFrameMemory *memory, DspIoLog *io,
    DspFrameResult *result)
{
    if (!gp || !ep || !gp->is_gp || ep->is_gp || phase > 7 || !mixbins ||
        !prior_pcm || !memory || !io || !result ||
        memory->gp_size > MAX_FRAME_MEMORY ||
        memory->ep_size > MAX_FRAME_MEMORY ||
        (memory->gp_size && (!memory->gp || !memory->gp_result)) ||
        (memory->ep_size && (!memory->ep || !memory->ep_result)) ||
        io->recording || !io->valid || io->mismatch || io->cursor != 0) {
        return DSP_REPLAY_INVALID;
    }
    for (size_t i = 0; i < NUM_MIXBINS; i++) {
        for (size_t j = 0; j < NUM_SAMPLES_PER_FRAME; j++) {
            if (!isfinite(mixbins[i][j])) {
                return DSP_REPLAY_INVALID;
            }
        }
    }
    MCPXAPUState *apu = g_try_new0(MCPXAPUState, 1);
    DspFrameResult *temporary = g_try_new(DspFrameResult, 1);
    DspReplayMemory gp_context = { .size = memory->gp_size,
                                   .io = io,
                                   .error = DSP_REPLAY_HALTED,
                                   .scratch_index = 0 };
    DspReplayMemory ep_context = { .size = memory->ep_size,
                                   .io = io,
                                   .error = DSP_REPLAY_HALTED,
                                   .scratch_index = 1 };
    gp_context.memory = g_try_malloc(MAX(memory->gp_size, 1));
    ep_context.memory = g_try_malloc(MAX(memory->ep_size, 1));
    if (!apu || !temporary || !gp_context.memory || !ep_context.memory) {
        g_free(apu);
        g_free(temporary);
        g_free(gp_context.memory);
        g_free(ep_context.memory);
        return DSP_REPLAY_INVALID;
    }
    if (memory->gp_size) {
        memcpy(gp_context.memory, memory->gp, memory->gp_size);
    }
    if (memory->ep_size) {
        memcpy(ep_context.memory, memory->ep, memory->ep_size);
    }
    bool old_jit = g_config.audio.use_dsp_jit;
    g_config.audio.use_dsp_jit = true;
    apu->gp.dsp =
        dsp_init(&gp_context, dsp_replay_scratch, dsp_replay_fifo, true);
    apu->ep.dsp =
        dsp_init(&ep_context, dsp_replay_scratch, dsp_replay_fifo, false);
    g_config.audio.use_dsp_jit = old_jit;
    DspReplayStatus status = DSP_REPLAY_HALTED;
    if (!dsp_jit_restore(apu->gp.dsp, gp) ||
        !dsp_jit_restore(apu->ep.dsp, ep)) {
        status = DSP_REPLAY_INVALID;
    } else {
        apu->gp.regs[NV_PAPU_GPRST] =
            NV_PAPU_GPRST_GPRST | NV_PAPU_GPRST_GPDSPRST;
        apu->ep.regs[NV_PAPU_EPRST] =
            NV_PAPU_GPRST_GPRST | NV_PAPU_GPRST_GPDSPRST;
        apu->ep_frame_div = phase;
        apu->monitor.point = MCPX_APU_DEBUG_MON_GP;
        memcpy(apu->monitor.frame_buf, prior_pcm,
               sizeof(apu->monitor.frame_buf));
        /* The production function takes a mutable pointer but only reads it. */
        float inputs[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME];
        memcpy(inputs, mixbins, sizeof(inputs));
        mcpx_apu_dsp_frame(apu, inputs);
        if (gp_context.error != DSP_REPLAY_HALTED) {
            status = gp_context.error;
        } else if (ep_context.error != DSP_REPLAY_HALTED) {
            status = ep_context.error;
        }
        if (status == DSP_REPLAY_HALTED &&
            (!dsp_get_halt_requested(apu->gp.dsp) ||
             (phase == 0 && !dsp_get_halt_requested(apu->ep.dsp)))) {
            status = DSP_REPLAY_TIMEOUT;
        } else if (status == DSP_REPLAY_HALTED && !dsp_io_complete(io)) {
            status = DSP_REPLAY_IO_MISMATCH;
        }
        if (status == DSP_REPLAY_HALTED) {
            if (!dsp_jit_capture(apu->gp.dsp, &temporary->gp) ||
                !dsp_jit_capture(apu->ep.dsp, &temporary->ep)) {
                status = DSP_REPLAY_INVALID;
            } else {
                memcpy(temporary->pcm, apu->monitor.frame_buf,
                       sizeof(temporary->pcm));
                *result = *temporary;
                if (memory->gp_size) {
                    memcpy(memory->gp_result, gp_context.memory,
                           memory->gp_size);
                }
                if (memory->ep_size) {
                    memcpy(memory->ep_result, ep_context.memory,
                           memory->ep_size);
                }
            }
        }
    }
    dsp_destroy(apu->gp.dsp);
    dsp_destroy(apu->ep.dsp);
    g_free(gp_context.memory);
    g_free(ep_context.memory);
    g_free(temporary);
    g_free(apu);
    return status;
}

DspReplayStatus dsp_replay_gp_ep_sequence(
    const DspJitCapture *gp, const DspJitCapture *ep, unsigned phase,
    size_t count, const float (*mixbins)[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME],
    const int16_t prior_pcm[256][2], const DspFrameMemory *memory,
    DspIoLog *logs, DspFrameResult *result,
    int16_t (*pcm)[NUM_SAMPLES_PER_FRAME][2], size_t *failed_frame)
{
    if (failed_frame) {
        *failed_frame = SIZE_MAX;
    }
    if (!gp || !ep || phase > 7 || !count || count > 1024 || !mixbins ||
        !prior_pcm || !memory || !logs || !result || !pcm || !failed_frame ||
        memory->gp_size > MAX_FRAME_MEMORY ||
        memory->ep_size > MAX_FRAME_MEMORY ||
        (memory->gp_size && (!memory->gp || !memory->gp_result)) ||
        (memory->ep_size && (!memory->ep || !memory->ep_result))) {
        return DSP_REPLAY_INVALID;
    }

    DspFrameResult *current = g_try_new(DspFrameResult, 1);
    DspFrameResult *next = g_try_new(DspFrameResult, 1);
    int16_t (*staged_pcm)[NUM_SAMPLES_PER_FRAME][2] =
        g_try_new(typeof(*staged_pcm), count);
    uint8_t *gp_a = g_try_malloc(MAX(memory->gp_size, 1));
    uint8_t *gp_b = g_try_malloc(MAX(memory->gp_size, 1));
    uint8_t *ep_a = g_try_malloc(MAX(memory->ep_size, 1));
    uint8_t *ep_b = g_try_malloc(MAX(memory->ep_size, 1));
    DspReplayStatus status = DSP_REPLAY_INVALID;
    if (!current || !next || !staged_pcm || !gp_a || !gp_b || !ep_a || !ep_b) {
        goto out;
    }
    current->gp = *gp;
    current->ep = *ep;
    memcpy(current->pcm, prior_pcm, sizeof(current->pcm));
    if (memory->gp_size) {
        memcpy(gp_a, memory->gp, memory->gp_size);
    }
    if (memory->ep_size) {
        memcpy(ep_a, memory->ep, memory->ep_size);
    }
    for (size_t i = 0; i < count; i++) {
        unsigned frame_phase = (phase + i) % 8;
        DspFrameMemory frame_memory = {
            .gp = gp_a,
            .ep = ep_a,
            .gp_size = memory->gp_size,
            .ep_size = memory->ep_size,
            .gp_result = gp_b,
            .ep_result = ep_b,
        };
        status = dsp_replay_gp_ep_frame(&current->gp, &current->ep, frame_phase,
                                        mixbins[i], current->pcm, &frame_memory,
                                        &logs[i], next);
        if (status != DSP_REPLAY_HALTED) {
            *failed_frame = i;
            goto out;
        }
        memcpy(staged_pcm[i], &next->pcm[frame_phase * NUM_SAMPLES_PER_FRAME],
               sizeof(staged_pcm[i]));
        DspFrameResult *swap_state = current;
        current = next;
        next = swap_state;
        uint8_t *swap = gp_a;
        gp_a = gp_b;
        gp_b = swap;
        swap = ep_a;
        ep_a = ep_b;
        ep_b = swap;
    }
    *result = *current;
    memcpy(pcm, staged_pcm, count * sizeof(*pcm));
    if (memory->gp_size) {
        memcpy(memory->gp_result, gp_a, memory->gp_size);
    }
    if (memory->ep_size) {
        memcpy(memory->ep_result, ep_a, memory->ep_size);
    }
out:
    g_free(current);
    g_free(next);
    g_free(staged_pcm);
    g_free(gp_a);
    g_free(gp_b);
    g_free(ep_a);
    g_free(ep_b);
    return status;
}

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "dsp-replay-core.h"
#include "ui/xemu-settings.h"

#define MAX_REPLAY_BYTES (16 * 1024 * 1024)
#define MAX_REPLAY_STEPS 1000000

void dsp_replay_scratch(void *opaque, uint8_t *data, uint32_t addr,
                        size_t length, bool write)
{
    DspReplayMemory *context = opaque;
    if (context->error != DSP_REPLAY_HALTED || addr > context->size ||
        length > context->size - addr) {
        if (context->error == DSP_REPLAY_HALTED) {
            context->error = DSP_REPLAY_UNSUPPORTED;
        }
        if (!write && length) {
            memset(data, 0, length);
        }
        return;
    }
    if (write) {
        if (!dsp_io_replay(context->io, DSP_IO_SCRATCH, context->scratch_index,
                           addr, true, 0, data, length)) {
            context->error = DSP_REPLAY_IO_MISMATCH;
            return;
        }
        if (length) {
            memcpy(context->memory + addr, data, length);
        }
    } else {
        /* Recorded bytes are admitted only if the independent model agrees. */
        if (length) {
            memcpy(data, context->memory + addr, length);
        }
        bool matched =
            dsp_io_replay(context->io, DSP_IO_SCRATCH, context->scratch_index,
                          addr, false, 0, data, length);
        if (matched && length && memcmp(data, context->memory + addr, length)) {
            context->io->cursor--;
            matched = false;
        }
        if (!matched) {
            /* A divergent model is never accepted as a successful replay. */
            context->error = DSP_REPLAY_IO_MISMATCH;
            context->io->mismatch = true;
            if (length) {
                memset(data, 0, length);
            }
        }
    }
}

void dsp_replay_fifo(void *opaque, uint8_t *data, unsigned index, size_t length,
                     bool write)
{
    DspReplayMemory *context = opaque;
    if (context->error == DSP_REPLAY_HALTED) {
        context->error = DSP_REPLAY_UNSUPPORTED;
    }
    if (!write && length) {
        memset(data, 0, length);
    }
}

DspReplayStatus dsp_replay_execute(const DspJitCapture *initial,
                                   const uint8_t *memory, size_t memory_size,
                                   DspIoLog *io, unsigned max_steps,
                                   DspJitCapture *result,
                                   uint8_t *result_memory)
{
    if (!initial || !io || !result || !max_steps ||
        max_steps > MAX_REPLAY_STEPS || memory_size > MAX_REPLAY_BYTES ||
        (memory_size && (!memory || !result_memory)) || io->recording ||
        !io->valid || io->mismatch || io->cursor != 0) {
        return DSP_REPLAY_INVALID;
    }
    DspReplayMemory context = { .size = memory_size,
                                .io = io,
                                .error = DSP_REPLAY_HALTED };
    context.memory = g_try_malloc(MAX(memory_size, 1));
    DspJitCapture *temporary = g_try_new(DspJitCapture, 1);
    if (!context.memory || !temporary) {
        g_free(context.memory);
        g_free(temporary);
        return DSP_REPLAY_INVALID;
    }
    if (memory_size) {
        memcpy(context.memory, memory, memory_size);
    }
    bool old_jit = g_config.audio.use_dsp_jit;
    g_config.audio.use_dsp_jit = true;
    DSPState *dsp =
        dsp_init(&context, dsp_replay_scratch, dsp_replay_fifo, initial->is_gp);
    g_config.audio.use_dsp_jit = old_jit;
    if (!dsp_jit_restore(dsp, initial)) {
        context.error = DSP_REPLAY_INVALID;
    } else {
        unsigned steps = 0;
        while (context.error == DSP_REPLAY_HALTED &&
               !dsp_get_halt_requested(dsp) && steps < max_steps) {
            dsp_step(dsp);
            steps++;
        }
        if (context.error == DSP_REPLAY_HALTED &&
            !dsp_get_halt_requested(dsp)) {
            context.error = DSP_REPLAY_TIMEOUT;
        } else if (context.error == DSP_REPLAY_HALTED && !dsp_io_complete(io)) {
            context.error = DSP_REPLAY_IO_MISMATCH;
        }
        if (context.error == DSP_REPLAY_HALTED) {
            if (!dsp_jit_capture(dsp, temporary)) {
                context.error = DSP_REPLAY_INVALID;
            } else {
                *result = *temporary;
                if (memory_size) {
                    memcpy(result_memory, context.memory, memory_size);
                }
            }
        }
    }
    dsp_destroy(dsp);
    g_free(context.memory);
    g_free(temporary);
    return context.error;
}

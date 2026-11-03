/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_GP_FILTER_FRAME_SHADOW_H
#define XEMU_GP_FILTER_FRAME_SHADOW_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "gp-filter-frame-hle.h"

typedef struct GPFilterFrameShadow {
    GPFilterFrame prediction;
    uint64_t attempted, eligible, skipped, invalidated;
    uint64_t matched, nonzero_matched, mismatched;
    bool armed;
} GPFilterFrameShadow;

/* Caller must establish the exact GP program identity. */
static inline bool gp_filter_frame_shadow_prepare(GPFilterFrameShadow *s,
                                                  const uint32_t xram[4096],
                                                  const uint32_t yram[2048])
{
    if (!s) {
        return false;
    }
    s->attempted++;
    s->armed = false;
    if (!gp_filter_frame_hle(xram, yram, &s->prediction)) {
        s->skipped++;
        return false;
    }
    s->eligible++;
    s->armed = true;
    return true;
}

static inline void gp_filter_frame_shadow_finish(GPFilterFrameShadow *s,
                                                 const uint32_t xram[4096],
                                                 const uint32_t yram[2048],
                                                 bool program_stable)
{
    if (!s || !s->armed) {
        return;
    }
    s->armed = false;
    if (!program_stable || !xram || !yram) {
        s->invalidated++;
        return;
    }
    for (unsigned ch = 0; ch < 8; ch++) {
        unsigned base = 0x80 + 39 * ch, dest = 0xd60 + 32 * ch;
        bool matches = memcmp(s->prediction.samples[ch], xram + dest,
                              sizeof(s->prediction.samples[ch])) == 0 &&
                       memcmp(s->prediction.history[ch], yram + 8 * ch,
                              sizeof(s->prediction.history[ch])) == 0 &&
                       s->prediction.last_input[ch] == xram[base + 18];
        if (!matches) {
            s->mismatched++;
            continue;
        }
        s->matched++;
        for (unsigned i = 0; i < 32; i++) {
            if (s->prediction.samples[ch][i] != 0) {
                s->nonzero_matched++;
                break;
            }
        }
    }
}
#endif

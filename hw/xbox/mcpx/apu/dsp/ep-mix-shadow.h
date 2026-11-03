/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_EP_MIX_SHADOW_H
#define XEMU_EP_MIX_SHADOW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ep-mix-hle.h"

typedef struct EPMixShadow {
    uint32_t input[5][256];
    uint8_t actual[1024];
    uint32_t first_address;
    unsigned stage;
    bool armed;
    uint64_t attempted, eligible, skipped, invalidated;
    uint64_t matched, nonzero_matched, mismatched;
    uint64_t matched_samples, mismatched_samples;
    uint64_t matched_words, mismatched_words;
} EPMixShadow;

/* The caller checks the program identity before starting a comparison. */
static inline void ep_mix_shadow_begin(EPMixShadow *s, bool program_known)
{
    if (!s)
        return;
    s->attempted++;
    s->stage = 0;
    s->armed = program_known;
    if (!program_known)
        s->skipped++;
}

static inline void ep_mix_shadow_reject(EPMixShadow *s)
{
    s->armed = false;
    s->skipped++;
}

/* The first read is the verified DSP program overlay; the following reads
 * must be five complete channels in the observed scratch-ring order. */
static inline void ep_mix_shadow_read(EPMixShadow *s, uint32_t address,
                                      const uint8_t *bytes, size_t length,
                                      bool overlay_known)
{
    if (!s || !s->armed)
        return;
    if (!bytes) {
        ep_mix_shadow_reject(s);
        return;
    }
    if (!s->stage) {
        if (length != 804 || !overlay_known) {
            ep_mix_shadow_reject(s);
            return;
        }
        s->stage = 1;
        return;
    }
    if (s->stage > 5 || length != 1024) {
        ep_mix_shadow_reject(s);
        return;
    }
    unsigned ch = s->stage - 1;
    if (!ch)
        s->first_address = address;
    if ((uint64_t)s->first_address + (uint64_t)ch * 2048 != address) {
        ep_mix_shadow_reject(s);
        return;
    }
    for (unsigned i = 0; i < 256; i++) {
        const uint8_t *p = bytes + 4 * i;
        s->input[ch][i] = (uint32_t)p[0] | (uint32_t)p[1] << 8 |
                          (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
    }
    s->stage++;
}

static inline void ep_mix_shadow_write(EPMixShadow *s, unsigned fifo_index,
                                       const uint8_t *bytes, size_t length)
{
    if (!s || !s->armed)
        return;
    if (s->stage != 6 || fifo_index != 0 || length != 1024 || !bytes) {
        ep_mix_shadow_reject(s);
        return;
    }
    memcpy(s->actual, bytes, sizeof(s->actual));
    s->stage = 7;
}

static inline void ep_mix_shadow_finish(EPMixShadow *s, const uint32_t *xram,
                                        bool program_after_known)
{
    if (!s || !s->armed)
        return;
    s->armed = false;
    if (!program_after_known || !xram) {
        s->invalidated++;
        return;
    }
    EPMixFrame expected;
    if (s->stage != 7 || !ep_mix_frame_hle(s->input, &expected)) {
        s->skipped++;
        return;
    }
    s->eligible++;
    bool nonzero = false, matches = true;
    for (unsigned ch = 0; ch < 7; ch++) {
        for (unsigned i = 0; i < 256; i++) {
            uint32_t word =
                ch < 5 ? s->input[ch][i] : expected.mixed[ch - 5][i];
            if (word == xram[ch * 256 + i]) {
                s->matched_words++;
            } else {
                s->mismatched_words++;
                matches = false;
            }
        }
    }
    for (unsigned i = 0; i < 512; i++) {
        bool same = expected.pcm[2 * i] == s->actual[2 * i] &&
                    expected.pcm[2 * i + 1] == s->actual[2 * i + 1];
        nonzero |= expected.pcm[2 * i] != 0 || expected.pcm[2 * i + 1] != 0;
        if (same)
            s->matched_samples++;
        else {
            s->mismatched_samples++;
            matches = false;
        }
    }
    if (matches) {
        s->matched++;
        s->nonzero_matched += nonzero;
    } else {
        s->mismatched++;
    }
}
#endif

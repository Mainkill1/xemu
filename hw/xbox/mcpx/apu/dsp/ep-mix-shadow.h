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
    uint32_t before_counter, before_phase, before_slot;
    unsigned stage;
    bool armed, extra_seen;
    uint64_t attempted, eligible, skipped, invalidated;
    uint64_t matched, nonzero_matched, mismatched;
    uint64_t matched_samples, mismatched_samples;
    uint64_t matched_words, mismatched_words;
    uint64_t matched_pram_words, mismatched_pram_words;
} EPMixShadow;

/* The caller checks the program identity before starting a comparison. */
static inline void ep_mix_shadow_begin(EPMixShadow *s, bool program_known,
                                       const uint32_t *pram)
{
    if (!s)
        return;
    s->attempted++;
    s->stage = 0;
    s->extra_seen = false;
    s->armed = program_known && pram && pram[1] < 6 &&
               (pram[0x116] == 0 || pram[0x116] == 0x100);
    if (!s->armed)
        s->skipped++;
    else {
        s->before_counter = pram[7] & 0xffffff;
        s->before_phase = pram[1];
        s->before_slot = pram[0x116];
    }
}

static inline void ep_mix_shadow_reject(EPMixShadow *s)
{
    s->armed = false;
    s->skipped++;
}

/* An optional periodic overlay precedes the verified mixer overlay. Five
 * complete channels must then follow in the observed scratch-ring order. */
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
        if (s->before_phase == 0 && !s->extra_seen) {
            if (length != 96 || !overlay_known) {
                ep_mix_shadow_reject(s);
                return;
            }
            s->extra_seen = true;
            return;
        }
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
                                        const uint32_t *pram,
                                        bool program_after_known)
{
    if (!s || !s->armed)
        return;
    s->armed = false;
    if (!program_after_known || !xram || !pram) {
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
    for (unsigned ch = 0; ch < 2; ch++) {
        uint32_t words[2] = { expected.peak[ch], expected.rms[ch] };
        unsigned addresses[2] = { 0x701 + ch, 0x703 + ch };
        for (unsigned i = 0; i < 2; i++) {
            if (words[i] == xram[addresses[i]]) {
                s->matched_words++;
            } else {
                s->mismatched_words++;
                matches = false;
            }
        }
    }
    const unsigned pram_addresses[8] = { 1,     7,     0x116, 0x1a6,
                                         0x1ab, 0x1ac, 0x1ad, 0x1ae };
    const uint32_t pram_values[8] = {
        (s->before_phase + 1) % 6, (s->before_counter + 1) & 0xffffff,
        s->before_slot ^ 0x100,    s->first_address + 4 * 2048,
        expected.peak[0],          expected.peak[1],
        expected.rms[0],           expected.rms[1],
    };
    for (unsigned i = 0; i < 8; i++) {
        if (pram[pram_addresses[i]] == pram_values[i]) {
            s->matched_pram_words++;
        } else {
            s->mismatched_pram_words++;
            matches = false;
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

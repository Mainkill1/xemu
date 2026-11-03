/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_GP_DECIMATE_SHADOW_H
#define XEMU_GP_DECIMATE_SHADOW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "gp-decimate-hle.h"

typedef struct GPDecimateShadowRoute {
    uint32_t address[2], length[2], destination, base, next_offset;
    uint32_t expected[6];
    unsigned count, next_phase, segments, seen;
    bool bad;
} GPDecimateShadowRoute;

typedef struct GPDecimateShadow {
    GPDecimateShadowRoute route[4];
    uint64_t attempted, eligible, skipped, invalidated;
    uint64_t matched, nonzero_matched, wrapped_matched, mismatched, writes;
    bool armed;
} GPDecimateShadow;

/* Caller must first establish the exact GP program identity. This merely
 * prepares private predictions; it does not alter the DSP or guest RAM. */
static inline bool gp_decimate_shadow_prepare(GPDecimateShadow *s,
                                              const uint32_t xram[4096])
{
    GPDecimateShadowRoute candidate[4] = { 0 };
    if (!s || !xram) {
        return false;
    }
    s->attempted++;
    s->armed = false;
    for (unsigned route = 0; route < 4; route++) {
        unsigned base = 1122 + route * 39;
        unsigned destination = 0xf60 + route * 32;
        uint32_t scratch_base = xram[base];
        uint32_t scratch_size = xram[base + 1];
        uint32_t offset = xram[base + 13];
        uint64_t address = (uint64_t)scratch_base + offset;
        unsigned count, next;

        if (scratch_size < 24 || offset >= scratch_size ||
            xram[base + 2] != 64 || xram[base + 3] != 0 ||
            xram[base + 4] != 1 || xram[base + 5] != 0x1760 + route * 32 ||
            xram[base + 6] != destination || xram[base + 7] != 6 ||
            xram[base + 9] != 0x4000 || xram[base + 10] != 0x4bd2 ||
            xram[base + 11] > 6 || xram[base + 12] != destination ||
            xram[base + 14] != scratch_base ||
            xram[base + 15] != scratch_size - 1 ||
            !gp_decimate_hle(xram + destination, xram[base + 8],
                             xram[base + 16], candidate[route].expected, &count,
                             &next)) {
            s->skipped++;
            return false;
        }
        unsigned length = count * 4;
        unsigned first =
            scratch_size - offset < length ? scratch_size - offset : length;
        unsigned second = length - first;
        if (address + first > UINT32_MAX ||
            (uint64_t)scratch_base + second > UINT32_MAX) {
            s->skipped++;
            return false;
        }
        candidate[route].address[0] = address;
        candidate[route].length[0] = first;
        candidate[route].address[1] = scratch_base;
        candidate[route].length[1] = second;
        candidate[route].segments = second ? 2 : 1;
        candidate[route].destination = destination;
        candidate[route].base = base;
        candidate[route].next_offset =
            second ? second :
                     (offset + first == scratch_size ? 0 : offset + first);
        candidate[route].count = count;
        candidate[route].next_phase = next;
        for (unsigned prior = 0; prior < route; prior++) {
            for (unsigned i = 0; i < candidate[route].segments; i++) {
                uint64_t a = candidate[route].address[i];
                for (unsigned j = 0; j < candidate[prior].segments; j++) {
                    uint64_t p = candidate[prior].address[j];
                    if (a < p + candidate[prior].length[j] &&
                        p < a + candidate[route].length[i]) {
                        s->skipped++;
                        return false;
                    }
                }
            }
        }
    }
    memcpy(s->route, candidate, sizeof(candidate));
    s->eligible++;
    s->armed = true;
    return true;
}

static inline void gp_decimate_shadow_write(GPDecimateShadow *s, uint32_t addr,
                                            const uint8_t *bytes, size_t len)
{
    if (!s || !s->armed || !bytes) {
        return;
    }
    uint64_t end = (uint64_t)addr + len;
    for (unsigned route = 0; route < 4; route++) {
        GPDecimateShadowRoute *r = &s->route[route];
        for (unsigned segment = 0; segment < r->segments; segment++) {
            uint64_t start = r->address[segment];
            if ((uint64_t)addr >= start + r->length[segment] || end <= start) {
                continue;
            }
            if (addr != start || len != r->length[segment] ||
                r->seen != segment) {
                r->bad = true;
                continue;
            }
            r->seen++;
            s->writes++;
            unsigned offset = segment ? r->length[0] : 0;
            for (unsigned i = 0; i < len; i++) {
                unsigned pos = offset + i;
                uint8_t expected = r->expected[pos / 4] >> (8 * (pos % 4));
                r->bad |= bytes[i] != expected;
            }
        }
    }
}

static inline void gp_decimate_shadow_finish(GPDecimateShadow *s,
                                             const uint32_t xram[4096],
                                             bool program_stable)
{
    if (!s || !s->armed) {
        return;
    }
    s->armed = false;
    if (!program_stable || !xram) {
        s->invalidated++;
        return;
    }
    for (unsigned route = 0; route < 4; route++) {
        GPDecimateShadowRoute *r = &s->route[route];
        bool ok = r->seen == r->segments && !r->bad &&
                  xram[r->base + 11] == r->count &&
                  xram[r->base + 13] == r->next_offset &&
                  xram[r->base + 16] == r->next_phase &&
                  memcmp(xram + r->destination, r->expected,
                         r->count * sizeof(*r->expected)) == 0;
        if (ok) {
            s->matched++;
            s->wrapped_matched += r->segments > 1;
            for (unsigned i = 0; i < r->count; i++) {
                if (r->expected[i] != 0) {
                    s->nonzero_matched++;
                    break;
                }
            }
        } else {
            s->mismatched++;
        }
    }
}

#endif

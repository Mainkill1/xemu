/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_GP_DECIMATE_SHADOW_H
#define XEMU_GP_DECIMATE_SHADOW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "gp-decimate-hle.h"

typedef struct GPDecimateShadowRoute {
    uint32_t address, destination, base, next_offset;
    uint32_t expected[6];
    unsigned next_phase;
    bool seen, bad;
} GPDecimateShadowRoute;

typedef struct GPDecimateShadow {
    GPDecimateShadowRoute route[4];
    uint64_t attempted, eligible, skipped, invalidated;
    uint64_t matched, mismatched, writes;
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
            scratch_size - offset < 24 || address + 24 > UINT32_MAX ||
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
        candidate[route].address = address;
        candidate[route].destination = destination;
        candidate[route].base = base;
        candidate[route].next_offset =
            offset + 24 == scratch_size ? 0 : offset + 24;
        candidate[route].next_phase = next;
        for (unsigned prior = 0; prior < route; prior++) {
            uint64_t p = candidate[prior].address;
            if (address < p + 24 && p < address + 24) {
                s->skipped++;
                return false;
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
        if ((uint64_t)addr >= (uint64_t)r->address + 24 || end <= r->address) {
            continue;
        }
        if (addr != r->address || len != 24 || r->seen) {
            r->bad = true;
            continue;
        }
        r->seen = true;
        s->writes++;
        for (unsigned i = 0; i < 6; i++) {
            const uint8_t *p = bytes + i * 4;
            uint32_t actual = (uint32_t)p[0] | (uint32_t)p[1] << 8 |
                              (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
            r->bad |= actual != r->expected[i];
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
        bool ok = r->seen && !r->bad && xram[r->base + 11] == 6 &&
                  xram[r->base + 13] == r->next_offset &&
                  xram[r->base + 16] == r->next_phase &&
                  memcmp(xram + r->destination, r->expected,
                         sizeof(r->expected)) == 0;
        if (ok) {
            s->matched++;
        } else {
            s->mismatched++;
        }
    }
}

#endif

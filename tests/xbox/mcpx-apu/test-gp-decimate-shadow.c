/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hw/xbox/mcpx/apu/dsp/gp-decimate-shadow.h"

static int check(bool condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "%s\n", message);
        return 1;
    }
    return 0;
}

static void prepare_xram(uint32_t xram[4096])
{
    memset(xram, 0, 4096 * sizeof(*xram));
    uint32_t seed = 12345;
    uint32_t samples[32];
    for (unsigned i = 0; i < 32; i++) {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        samples[i] = ((seed % 1000001) - 500000) & 0xffffff;
    }
    for (unsigned route = 0; route < 4; route++) {
        unsigned base = 1122 + route * 39;
        unsigned dest = 0xf60 + route * 32;
        uint32_t scratch = 0x10000 + route * 0x1000;
        xram[base] = scratch;
        xram[base + 1] = 2560;
        xram[base + 2] = 64;
        xram[base + 4] = 1;
        xram[base + 5] = 0x1760 + route * 32;
        xram[base + 6] = dest;
        xram[base + 7] = 6;
        xram[base + 8] = 4100000;
        xram[base + 9] = 0x4000;
        xram[base + 10] = 0x4bd2;
        xram[base + 11] = 5;
        xram[base + 12] = dest;
        xram[base + 13] = 2112;
        xram[base + 14] = scratch;
        xram[base + 15] = 2559;
        xram[base + 16] = 6;
        memcpy(xram + dest, samples, sizeof(samples));
    }
}

static void write_word(uint8_t *bytes, uint32_t value)
{
    for (unsigned i = 0; i < 4; i++) {
        bytes[i] = value >> (8 * i);
    }
}

static void feed_writes(GPDecimateShadow *s, int wrong, int omit, int duplicate)
{
    static const uint32_t expected[6] = {
        0x1c643d, 0x0c3540, 0x20bb12, 0x0b7c15, 0x079f8f, 0x0a4022,
    };
    for (unsigned route = 0; route < 4; route++) {
        uint8_t bytes[24];
        if ((int)route == omit) {
            continue;
        }
        for (unsigned i = 0; i < 6; i++) {
            write_word(bytes + i * 4,
                       expected[i] ^ (route == (unsigned)wrong && i == 1));
        }
        gp_decimate_shadow_write(s, 0x10000 + route * 0x1000 + 2112, bytes,
                                 sizeof(bytes));
        if ((int)route == duplicate) {
            gp_decimate_shadow_write(s, 0x10000 + route * 0x1000 + 2112, bytes,
                                     sizeof(bytes));
        }
    }
}

static void complete_xram(uint32_t xram[4096])
{
    static const uint32_t expected[6] = {
        0x1c643d, 0x0c3540, 0x20bb12, 0x0b7c15, 0x079f8f, 0x0a4022,
    };
    for (unsigned route = 0; route < 4; route++) {
        unsigned base = 1122 + route * 39;
        memcpy(xram + 0xf60 + route * 32, expected, sizeof(expected));
        xram[base + 11] = 6;
        xram[base + 13] = 2136;
        xram[base + 16] = 2;
    }
}

int main(void)
{
    GPDecimateShadow shadow = { 0 };
    uint32_t xram[4096], original[4096];
    int failed = 0;

    prepare_xram(xram);
    memcpy(original, xram, sizeof(xram));
    failed |= check(gp_decimate_shadow_prepare(&shadow, xram),
                    "valid state rejected");
    feed_writes(&shadow, -1, -1, -1);
    failed |= check(memcmp(xram, original, sizeof(xram)) == 0,
                    "shadow changed guest data");
    complete_xram(xram);
    gp_decimate_shadow_finish(&shadow, xram, true);
    failed |= check(shadow.matched == 4 && shadow.mismatched == 0,
                    "matching routes were not admitted");

    prepare_xram(xram);
    failed |= check(gp_decimate_shadow_prepare(&shadow, xram),
                    "second state rejected");
    feed_writes(&shadow, 0, 1, 2);
    complete_xram(xram);
    gp_decimate_shadow_finish(&shadow, xram, true);
    failed |= check(shadow.matched == 5 && shadow.mismatched == 3,
                    "corrupt, missing or duplicate writes were not rejected");

    prepare_xram(xram);
    xram[1122 + 13] = 2550;
    failed |= check(!gp_decimate_shadow_prepare(&shadow, xram),
                    "wrapped write was admitted");
    prepare_xram(xram);
    failed |= check(gp_decimate_shadow_prepare(&shadow, xram),
                    "third state rejected");
    feed_writes(&shadow, -1, -1, -1);
    complete_xram(xram);
    gp_decimate_shadow_finish(&shadow, xram, false);
    failed |= check(shadow.invalidated == 1 && shadow.matched == 5,
                    "changed program was incorrectly admitted");

    prepare_xram(xram);
    xram[1122 + 10] ^= 1;
    failed |= check(!gp_decimate_shadow_prepare(&shadow, xram),
                    "unknown DMA descriptor was admitted");
    prepare_xram(xram);
    failed |= check(gp_decimate_shadow_prepare(&shadow, xram),
                    "fourth state rejected");
    feed_writes(&shadow, -1, -1, -1);
    complete_xram(xram);
    xram[1122 + 13]++;
    xram[1122 + 39 + 16]++;
    gp_decimate_shadow_finish(&shadow, xram, true);
    failed |= check(shadow.matched == 7 && shadow.mismatched == 5,
                    "incorrect phase or DMA offset was admitted");
    return failed ? 1 : 0;
}

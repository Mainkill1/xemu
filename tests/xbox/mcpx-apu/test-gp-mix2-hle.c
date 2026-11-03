/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hw/xbox/mcpx/apu/dsp/gp-mix2-hle.h"

static int check(bool condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "%s\n", message);
        return 1;
    }
    return 0;
}

int main(void)
{
    /* Synthetic coefficients and input, evaluated by the DSP JIT. */
    static const uint32_t expected[32] = {
        0xffd380, 0x07bf00, 0x09ab00, 0x119680, 0x038180, 0x0b6d00, 0x0d5900,
        0x154400, 0xff2f80, 0x071b80, 0x090680, 0x10f200, 0x02dd80, 0x0ac980,
        0x0cb480, 0x14a000, 0xff0500, 0x02f500, 0x08e600, 0x0cd600, 0x02c680,
        0x06b700, 0x0ca780, 0x109800, 0xfe8880, 0x027900, 0x086980, 0x0c5980,
        0x024a00, 0x063b00, 0x0c2b00, 0x101b80,
    };
    uint32_t x[32], y[32], z[16], output[32], untouched[32];
    for (unsigned i = 0; i < 32; i++) {
        x[i] = ((i * 317 + 107 + 15 * 11) & 0xffff) ^ ((i & 1) ? 0xff0000 : 0);
        y[i] = ((i * 211 + 431 + 15 * 17) & 0xffff) ^ ((i & 2) ? 0xff0000 : 0);
        if (i < 16) {
            z[i] =
                ((i * 151 + 229 + 15 * 23) & 0xffff) ^ ((i & 4) ? 0xff0000 : 0);
        }
    }
    int failed = 0;
    failed |= check(
        gp_mix2_hle(x, y, z, 0xf80000, 0xfc0000, 0xf60000, 0xf80000, output),
        "valid mixer case rejected");
    failed |= check(!memcmp(output, expected, sizeof(expected)),
                    "mixer output differs from JIT oracle");
    memset(output, 0x5a, sizeof(output));
    memcpy(untouched, output, sizeof(output));
    y[31] = 0x1000000;
    failed |= check(!gp_mix2_hle(x, y, z, 0, 0, 0, 0, output) &&
                        !memcmp(output, untouched, sizeof(output)),
                    "invalid input changed output");
    for (unsigned i = 0; i < 32; i++) {
        x[i] = y[i] = 0x7fffff;
        if (i < 16)
            z[i] = 0x7fffff;
    }
    failed |= check(
        !gp_mix2_hle(x, y, z, 0x7fffff, 0x7fffff, 0x7fffff, 0x7fffff, output) &&
            !memcmp(output, untouched, sizeof(output)),
        "unqualified overflow changed output");
    return failed ? 1 : 0;
}

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hw/xbox/mcpx/apu/dsp/gp-decimate-hle.h"

static int check(bool condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "%s\n", message);
        return 1;
    }
    return 0;
}

static void make_channel(uint32_t seed, uint32_t output[32])
{
    for (unsigned i = 0; i < 32; i++) {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        output[i] = ((seed % 1000001) - 500000) & 0xffffff;
    }
}

int main(void)
{
    static const uint32_t expected_phase6[6] = {
        0x1c643d, 0x0c3540, 0x20bb12, 0x0b7c15, 0x079f8f, 0x0a4022,
    };
    static const uint32_t expected_phase5[6] = {
        0xef5c81, 0x1b59af, 0xf0f379, 0xe4e266, 0xfb8333, 0x1cd9d6,
    };
    static const uint32_t expected_negative_gain[6] = {
        0xeaa34e, 0x13577a, 0xe66a1c, 0xe9926c, 0x1fee6d, 0xf86113,
    };
    static const uint32_t expected_phase2[5] = {
        0x19a862, 0xc7a55a, 0xf15d15, 0x25d073, 0x19b066,
    };
    static const uint32_t expected_short[3][5] = {
        { 0xffa5ee, 0xe16c49, 0xeaf898, 0xf8bf54, 0x0a5a77 },
        { 0xf92cf2, 0x1bbd48, 0x1b7014, 0x0cd43a, 0xf29584 },
        { 0, 0, 0, 0, 0 },
    };
    static const unsigned short_phases[3] = { 1, 3, 4 };
    static const uint32_t short_gains[3] = { 4100000, 12582912, 0 };
    uint32_t input[32], output[6] = { 0 }, before[6];
    unsigned count = 0, next = 0;
    int failed = 0;

    make_channel(12345, input);
    failed |= check(gp_decimate_hle(input, 4100000, 6, output, &count, &next),
                    "phase-6 route was rejected");
    failed |= check(count == 6 && next == 2 &&
                        memcmp(output, expected_phase6, sizeof(output)) == 0,
                    "phase-6 output or continuation differs from DSP oracle");
    failed |= check(gp_decimate_hle(input, 4100000, 5, output, &count, &next),
                    "phase-5 route was rejected");
    failed |= check(count == 6 && next == 1 &&
                        memcmp(output, expected_phase5, sizeof(output)) == 0,
                    "phase-5 output or continuation differs from DSP oracle");

    make_channel(3, input);
    failed |= check(gp_decimate_hle(input, 0xc00000, 6, output, &count, &next),
                    "negative gain route was rejected");
    failed |=
        check(count == 6 && next == 2 &&
                  memcmp(output, expected_negative_gain, sizeof(output)) == 0,
              "negative gain output differs from DSP oracle");

    make_channel(1, input);
    output[5] = 0x123456;
    failed |= check(gp_decimate_hle(input, 8388607, 2, output, &count, &next),
                    "phase-2 route was rejected");
    failed |=
        check(count == 5 && next == 4 && output[5] == 0x123456 &&
                  memcmp(output, expected_phase2, sizeof(expected_phase2)) == 0,
              "phase-2 output or continuation differs from DSP oracle");

    for (unsigned i = 0; i < 3; i++) {
        output[5] = 0x123456;
        failed |= check(gp_decimate_hle(input, short_gains[i], short_phases[i],
                                        output, &count, &next),
                        "short route was rejected");
        failed |= check(count == 5 && next == short_phases[i] + 2 &&
                            output[5] == 0x123456 &&
                            memcmp(output, expected_short[i],
                                   sizeof(expected_short[i])) == 0,
                        "short route differs from DSP oracle");
    }

    for (unsigned i = 0; i < 32; i++) {
        input[i] = 0x7fffff;
    }
    memset(output, 0x5a, sizeof(output));
    memcpy(before, output, sizeof(output));
    count = 77;
    next = 88;
    failed |= check(!gp_decimate_hle(input, 0x7fffff, 6, output, &count, &next),
                    "unsupported overflow must be rejected");
    failed |= check(count == 77 && next == 88 &&
                        memcmp(output, before, sizeof(output)) == 0,
                    "rejected route changed its output");
    failed |= check(!gp_decimate_hle(input, 1, 0, output, &count, &next),
                    "unqualified phase must be rejected");
    return failed ? 1 : 0;
}

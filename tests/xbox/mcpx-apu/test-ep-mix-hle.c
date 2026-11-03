/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hw/xbox/mcpx/apu/dsp/ep-mix-hle.h"

static int check(bool ok, const char *message)
{
    if (!ok) {
        fprintf(stderr, "%s\n", message);
        return 1;
    }
    return 0;
}

int main(void)
{
    uint32_t input[5][256] = { 0 };
    uint8_t output[1024], unchanged[1024];
    int failed = 0;
    /* Synthetic inputs were also fed to the captured EP JIT program. */
    for (unsigned ch = 0; ch < 5; ch++) {
        uint32_t seed = 1 ^ ((ch + 2) * UINT32_C(0x9e3779b9));
        for (unsigned i = 0; i < 256; i++) {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            input[ch][i] = ((int32_t)(seed % 4000001) - 2000000) & 0xffffff;
        }
    }
    failed |= check(ep_mix_hle(input, output), "valid EP mix rejected");
    uint64_t hash = UINT64_C(14695981039346656037);
    for (unsigned i = 0; i < sizeof(output); i++) {
        hash = (hash ^ output[i]) * UINT64_C(1099511628211);
    }
    failed |= check(hash == UINT64_C(0xaca2d1270610fbb9),
                    "EP mix differs from synthetic JIT output");
    EPMixFrame frame;
    failed |= check(ep_mix_frame_hle(input, &frame), "EP frame mix rejected");
    hash = UINT64_C(14695981039346656037);
    for (unsigned ch = 0; ch < 2; ch++) {
        for (unsigned i = 0; i < 256; i++) {
            uint32_t word = frame.mixed[ch][i];
            for (unsigned b = 0; b < 4; b++) {
                hash = (hash ^ ((word >> (8 * b)) & 0xff)) *
                       UINT64_C(1099511628211);
            }
        }
    }
    failed |= check(hash == UINT64_C(0xf1289de92c1dc857),
                    "EP mixed XRAM differs from synthetic JIT output");
    failed |= check(frame.peak[0] == 0x40dc3f && frame.peak[1] == 0x3f2f19 &&
                        frame.rms[0] == 0x18b6eb && frame.rms[1] == 0x18b536,
                    "EP signal meters differ from synthetic JIT output");

    /* JIT-derived status observations for this program, not a hardware spec. */
    memset(input, 0, sizeof(input));
    failed |= check(ep_mix_frame_hle(input, &frame), "silent frame rejected");
    failed |= check(frame.right_energy == 0, "silent energy differs from JIT");
    failed |= check(ep_mix_shadow_status(0xc003bf, &frame) == 0xc00390,
                    "silent status prediction differs from JIT");
    failed |= check(ep_mix_shadow_status(0xc003ff, &frame) == 0xc003d0,
                    "sticky limit flag was lost");
    for (unsigned ch = 0; ch < 5; ch++) {
        for (unsigned i = 0; i < 256; i++) {
            input[ch][i] = 0x7fffff;
        }
    }
    failed |= check(ep_mix_frame_hle(input, &frame), "positive frame rejected");
    failed |= check(ep_mix_shadow_status(0xc00390, &frame) == 0xc00390,
                    "positive clipping status differs from JIT");
    for (unsigned ch = 0; ch < 5; ch++) {
        for (unsigned i = 0; i < 256; i++) {
            input[ch][i] = 0x800000;
        }
    }
    failed |= check(ep_mix_frame_hle(input, &frame), "negative frame rejected");
    failed |= check(frame.right_energy == (UINT64_C(1) << 47),
                    "saturated energy differs from JIT");
    failed |= check(frame.rms[0] == 0 && frame.rms[1] == 0,
                    "full negative saturation RMS differs from JIT");
    uint32_t before_regs[64], expected_regs[64];
    for (unsigned i = 0; i < 64; i++) {
        before_regs[i] = i;
    }
    before_regs[57] = 0xc00390;
    ep_mix_shadow_registers(before_regs, 0xffffff, 0x100, 0x510000, 0x510100,
                            &frame, expected_regs);
    failed |= check(expected_regs[6] == 0 && expected_regs[7] == 0x800000 &&
                        expected_regs[12] == 0 && expected_regs[17] == 0x100 &&
                        expected_regs[57] == 0xc003d0 &&
                        expected_regs[60] == 0x510000 &&
                        expected_regs[61] == 0x510100 &&
                        expected_regs[18] == before_regs[18],
                    "EP register prediction differs from JIT");
    failed |= check(ep_mix_shadow_status(0xc00390, &frame) == 0xc003d0,
                    "negative clipping status differs from JIT");

    uint32_t before_stack[2][16], predicted_stack[2][16];
    for (unsigned bank = 0; bank < 2; bank++) {
        for (unsigned i = 0; i < 16; i++) {
            before_stack[bank][i] = 0x510000 + bank * 0x100 + i;
        }
    }
    ep_mix_shadow_stack(before_stack, 5, 0xc003d0, predicted_stack);
    failed |=
        check(predicted_stack[0][1] == 182 && predicted_stack[0][5] == 750 &&
                  predicted_stack[1][1] == 0xc003d4 &&
                  predicted_stack[1][5] == 0xc003d0 &&
                  predicted_stack[1][6] == before_stack[1][6],
              "phase 5 stack prediction differs from JIT");
    ep_mix_shadow_stack(before_stack, 0, 0xc00390, predicted_stack);
    failed |= check(predicted_stack[1][1] == 0xc00399 &&
                        predicted_stack[1][2] == 0xc00394 &&
                        predicted_stack[1][3] == 0,
                    "phase 0 stack prediction differs from JIT");
    for (unsigned phase = 0; phase < 6; phase++) {
        ep_mix_shadow_stack(before_stack, phase, 0xc00390, predicted_stack);
        for (unsigned bank = 0; bank < 2; bank++) {
            for (unsigned i = 0; i < 16; i++) {
                if (i == 0 || i >= 6) {
                    failed |=
                        check(predicted_stack[bank][i] == before_stack[bank][i],
                              "unwritten stack entry changed");
                }
            }
        }
        failed |=
            check(predicted_stack[1][1] == (phase == 5 ? 0xc00394 : 0xc00399),
                  "stack phase prediction differs from JIT");
    }

    /* Each channel sum lands exactly halfway between two 24-bit values. */
    for (unsigned i = 0; i < 256; i++) {
        uint32_t seed = 1 ^ (i * UINT32_C(0x9e3779b9));
        int32_t sources[3];
        for (unsigned j = 0; j < 3; j++) {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            sources[j] = (int32_t)(seed % 500001) - 250000;
        }
        int32_t fronts[2];
        for (unsigned j = 0; j < 2; j++) {
            int64_t t = (int64_t)0x5a8279 * (sources[0] + sources[j + 1]) -
                        (INT64_C(1) << 22);
            fronts[j] = (uint64_t)t & 0x7fffff;
            if ((seed >> j) & 1) {
                fronts[j] -= 0x800000;
            }
        }
        int32_t words[5] = { fronts[0], sources[0], fronts[1], sources[1],
                             sources[2] };
        for (unsigned ch = 0; ch < 5; ch++) {
            input[ch][i] = (uint32_t)words[ch] & 0xffffff;
        }
    }
    failed |= check(ep_mix_frame_hle(input, &frame), "EP tie frame rejected");
    hash = UINT64_C(14695981039346656037);
    for (unsigned ch = 0; ch < 2; ch++) {
        for (unsigned i = 0; i < 256; i++) {
            for (unsigned b = 0; b < 4; b++) {
                hash = (hash ^ ((frame.mixed[ch][i] >> (8 * b)) & 0xff)) *
                       UINT64_C(1099511628211);
            }
        }
    }
    failed |= check(hash == UINT64_C(0x2bb9e797bc27af01),
                    "EP rounding ties differ from JIT");
    failed |= check(frame.peak[0] == 0x7fffff && frame.peak[1] == 0x7fffff &&
                        frame.rms[0] == 0x472bf7 && frame.rms[1] == 0x45c113,
                    "EP saturated signal meters differ from JIT");

    memset(output, 0x5a, sizeof(output));
    memcpy(unchanged, output, sizeof(output));
    input[4][255] = 0x1000000;
    failed |= check(!ep_mix_hle(input, output), "invalid word accepted");
    failed |= check(memcmp(output, unchanged, sizeof(output)) == 0,
                    "failed EP mix modified output");
    for (unsigned ch = 0; ch < 5; ch++) {
        for (unsigned i = 0; i < 256; i++) {
            input[ch][i] = i & 1 ? 0x800000 : 0x7fffff;
        }
    }
    failed |= check(ep_mix_hle(input, output), "saturating mix rejected");
    for (unsigned i = 0; i < 256; i++) {
        uint16_t expected = i & 1 ? 0x8000 : 0x7fff;
        for (unsigned ch = 0; ch < 2; ch++) {
            unsigned j = 4 * i + 2 * ch;
            failed |= check(output[j] == (expected & 0xff) &&
                                output[j + 1] == (expected >> 8),
                            "saturated sample mismatch");
        }
    }
    return failed ? 1 : 0;
}

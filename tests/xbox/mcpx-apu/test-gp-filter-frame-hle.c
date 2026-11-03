/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hw/xbox/mcpx/apu/dsp/gp-filter-frame-hle.h"
#include "hw/xbox/mcpx/apu/dsp/gp-filter-frame-shadow.h"

static int check(bool ok, const char *message)
{
    if (!ok) {
        fprintf(stderr, "%s\n", message);
        return 1;
    }
    return 0;
}

static uint32_t word24(int32_t x)
{
    return (uint32_t)x & 0xffffff;
}

int main(void)
{
    uint32_t xram[4096] = { 0 }, yram[2048] = { 0 };
    GPFilterFrame output, unchanged;
    int failed = 0;

    for (unsigned ch = 0; ch < 8; ch++) {
        unsigned base = 0x80 + 39 * ch, dest = 0xd60 + 32 * ch;
        xram[base] = 0xc000;
        xram[base + 2] = 8 * ch;
        xram[base + 3] = 8;
        xram[base + 4] = 1;
        xram[base + 5] = 0x1560 + 32 * ch;
        xram[base + 6] = dest;
        xram[base + 7] = 0x400000;
        xram[base + 8] = 0x200000;
        xram[base + 13] = 0x200000;
        for (unsigned i = 0; i < 32; i++) {
            xram[dest + i] = word24((int32_t)(2 * i) - 30 + 2 * ch);
        }
    }
    memset(&output, 0xa5, sizeof(output));
    failed |= check(gp_filter_frame_hle(xram, yram, &output),
                    "valid filter frame rejected");
    for (unsigned ch = 0; ch < 8; ch++) {
        int32_t last = 32 + 2 * ch, prev = last - 2;
        for (unsigned i = 0; i < 32; i++) {
            failed |= check(output.samples[ch][i] == xram[0xd60 + 32 * ch + i],
                            "filter output mismatch");
        }
        uint32_t expected[8] = { word24(last),     word24(prev),
                                 word24(last / 2), word24(prev / 2),
                                 word24(4 * last), word24(4 * prev),
                                 word24(2 * last), word24(2 * prev) };
        failed |=
            check(memcmp(output.history[ch], expected, sizeof(expected)) == 0,
                  "filter history mismatch");
        failed |= check(output.last_input[ch] == word24(last),
                        "filter temporary mismatch");
    }

    GPFilterFrameShadow shadow = { 0 };
    uint32_t after_x[4096], after_y[2048];
    memcpy(after_x, xram, sizeof(after_x));
    memcpy(after_y, yram, sizeof(after_y));
    for (unsigned ch = 0; ch < 8; ch++) {
        int32_t last = 32 + 2 * ch, prev = last - 2;
        uint32_t expected[8] = { word24(last),     word24(prev),
                                 word24(last / 2), word24(prev / 2),
                                 word24(4 * last), word24(4 * prev),
                                 word24(2 * last), word24(2 * prev) };
        memcpy(after_y + 8 * ch, expected, sizeof(expected));
        after_x[0x80 + 39 * ch + 18] = word24(last);
    }
    failed |= check(gp_filter_frame_shadow_prepare(&shadow, xram, yram),
                    "valid filter shadow frame rejected");
    gp_filter_frame_shadow_finish(&shadow, after_x, after_y, true);
    failed |= check(shadow.matched == 8 && shadow.nonzero_matched == 8 &&
                        shadow.mismatched == 0,
                    "matching filter routes rejected");
    failed |= check(gp_filter_frame_shadow_prepare(&shadow, xram, yram),
                    "second filter shadow frame rejected");
    after_x[0xd60 + 32] ^= 1;
    after_y[2 * 8] ^= 1;
    after_x[0x80 + 39 * 3 + 18] ^= 1;
    gp_filter_frame_shadow_finish(&shadow, after_x, after_y, true);
    failed |= check(shadow.matched == 13 && shadow.mismatched == 3,
                    "corrupt sample, history or temporary was admitted");
    failed |= check(gp_filter_frame_shadow_prepare(&shadow, xram, yram),
                    "third filter shadow frame rejected");
    gp_filter_frame_shadow_finish(&shadow, after_x, after_y, false);
    failed |= check(shadow.invalidated == 1 && shadow.matched == 13,
                    "changed filter program was admitted");

    memset(&output, 0x5a, sizeof(output));
    unchanged = output;
    xram[0x80 + 39 * 7 + 3] = 9;
    failed |= check(!gp_filter_frame_hle(xram, yram, &output),
                    "invalid late descriptor accepted");
    failed |= check(memcmp(&output, &unchanged, sizeof(output)) == 0,
                    "failed frame changed output");
    xram[0x80 + 39 * 7 + 3] = 8;
    xram[0xd60] = 0x7fffff;
    xram[0x80 + 8] = 0x7fffff;
    failed |= check(!gp_filter_frame_hle(xram, yram, &output),
                    "overflow domain accepted");
    failed |= check(memcmp(&output, &unchanged, sizeof(output)) == 0,
                    "overflow frame changed output");
    return failed ? 1 : 0;
}

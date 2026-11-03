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

    /* Synthetic coefficients, input and history; expected words came from
     * a JIT replay, with no captured game samples in this vector. */
    static const uint32_t coeff[11] = {
        0x400000, 0x032ae8, 0x0437c7, 0x043213, 0xf33423, 0xfee681,
        0xf50fbe, 0x0d73f2, 0xfda23c, 0xf93731, 0x01986d,
    };
    static const uint32_t history[8] = {
        0x007881, 0x00e534, 0x00357b, 0x009954,
        0xff1b75, 0xff0923, 0x001c6a, 0x0117bd,
    };
    static const uint32_t samples[32] = {
        0xfc7f01, 0x020dde, 0xfef98e, 0x01843c, 0x02d9d3, 0xffeb87, 0xff0182,
        0x03e71f, 0xfde8f0, 0xf9d5f0, 0xf9bd0a, 0xf90c80, 0x073ad3, 0xf91ebc,
        0xff8abe, 0xf9e718, 0xfe5fad, 0xfb382d, 0xf87ef0, 0x06077e, 0x00de04,
        0xfd262b, 0x04339c, 0xfe59eb, 0x0206a6, 0xfa586f, 0x007afd, 0x02fb37,
        0x02dab7, 0x025a8b, 0x0065be, 0x017ca0,
    };
    static const uint32_t expected_samples[32] = {
        0x000649, 0xfff3c9, 0x000cd8, 0xffc816, 0x001690, 0xffe854, 0x0025ff,
        0xfff117, 0x0009cf, 0x002c78, 0x005bf3, 0xfffde3, 0xff7f63, 0x001da7,
        0xffc793, 0x00a523, 0xff7f7c, 0x00566a, 0xffe5f3, 0xffd117, 0xffc84f,
        0xffe34f, 0x0040de, 0xffefa4, 0xffd42d, 0x007383, 0xffc229, 0x000ddb,
        0xff9b47, 0x001db2, 0x00169a, 0x0006df,
    };
    static const uint32_t expected_history[8] = {
        0x017ca0, 0x0065be, 0x003156, 0x004544,
        0x018ab0, 0x022a20, 0x000dbe, 0x002d34,
    };
    memcpy(xram + 0x87, coeff, sizeof(coeff));
    memcpy(yram, history, sizeof(history));
    memcpy(xram + 0xd60, samples, sizeof(samples));
    failed |= check(gp_filter_frame_hle(xram, yram, &output),
                    "synthetic nontrivial filter rejected");
    failed |= check(memcmp(output.samples[0], expected_samples,
                           sizeof(expected_samples)) == 0 &&
                        memcmp(output.history[0], expected_history,
                               sizeof(expected_history)) == 0,
                    "nontrivial filter differs from JIT vector");

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

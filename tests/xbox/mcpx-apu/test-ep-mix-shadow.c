/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hw/xbox/mcpx/apu/dsp/ep-mix-shadow.h"

static int check(bool ok, const char *message)
{
    if (!ok) {
        fprintf(stderr, "%s\n", message);
        return 1;
    }
    return 0;
}

static void feed(EPMixShadow *s, const uint32_t input[5][256],
                 const uint8_t actual[1024], bool overlay_known, bool corrupt)
{
    uint8_t overlay[804] = { 0 };
    ep_mix_shadow_read(s, 0x70d4, overlay, sizeof(overlay), overlay_known);
    for (unsigned ch = 0; ch < 5; ch++) {
        uint8_t bytes[1024];
        for (unsigned i = 0; i < 256; i++) {
            for (unsigned j = 0; j < 4; j++) {
                bytes[4 * i + j] = input[ch][i] >> (8 * j);
            }
        }
        ep_mix_shadow_read(s, 0x10000 + 2048 * ch, bytes, sizeof(bytes), false);
    }
    uint8_t bytes[1024];
    memcpy(bytes, actual, sizeof(bytes));
    bytes[0] ^= corrupt;
    ep_mix_shadow_write(s, 0, bytes, sizeof(bytes));
}

int main(void)
{
    uint32_t input[5][256] = { 0 };
    uint8_t actual[1024];
    uint32_t after_xram[4096] = { 0 };
    EPMixFrame frame;
    EPMixShadow shadow = { 0 };
    int failed = 0;
    for (unsigned ch = 0; ch < 5; ch++) {
        for (unsigned i = 0; i < 256; i++) {
            input[ch][i] = (i * (ch + 1) * 101) & 0xffffff;
        }
    }
    failed |= check(ep_mix_hle(input, actual), "native prediction failed");
    failed |= check(ep_mix_frame_hle(input, &frame), "frame prediction failed");
    memcpy(after_xram, input, sizeof(input));
    memcpy(after_xram + 0x500, frame.mixed, sizeof(frame.mixed));
    ep_mix_shadow_begin(&shadow, true);
    feed(&shadow, input, actual, true, false);
    ep_mix_shadow_finish(&shadow, after_xram, true);
    failed |=
        check(shadow.matched == 1 && shadow.nonzero_matched == 1 &&
                  shadow.mismatched == 0 && shadow.matched_samples == 512 &&
                  shadow.matched_words == 1792,
              "matching EP output rejected");
    ep_mix_shadow_begin(&shadow, true);
    feed(&shadow, input, actual, true, true);
    after_xram[0x500] ^= 1;
    ep_mix_shadow_finish(&shadow, after_xram, true);
    after_xram[0x500] ^= 1;
    failed |= check(shadow.matched == 1 && shadow.mismatched == 1 &&
                        shadow.mismatched_samples == 1 &&
                        shadow.mismatched_words == 1,
                    "corrupted EP output admitted");
    ep_mix_shadow_begin(&shadow, true);
    feed(&shadow, input, actual, false, false);
    ep_mix_shadow_finish(&shadow, after_xram, true);
    failed |= check(shadow.skipped == 1, "unknown overlay admitted");
    ep_mix_shadow_begin(&shadow, false);
    failed |= check(shadow.skipped == 2, "unknown program admitted");
    ep_mix_shadow_begin(&shadow, true);
    feed(&shadow, input, actual, true, false);
    ep_mix_shadow_finish(&shadow, after_xram, false);
    failed |= check(shadow.invalidated == 1 && shadow.matched == 1,
                    "changed program admitted");
    ep_mix_shadow_begin(&shadow, true);
    uint8_t overlay[804] = { 0 };
    ep_mix_shadow_read(&shadow, 0, overlay, sizeof(overlay), true);
    uint8_t bad_input[1024] = { 0 };
    ep_mix_shadow_read(&shadow, 0x10000, bad_input, sizeof(bad_input), false);
    ep_mix_shadow_read(&shadow, 0x10001, bad_input, sizeof(bad_input), false);
    ep_mix_shadow_finish(&shadow, after_xram, true);
    failed |=
        check(shadow.skipped == 3, "unexpected scratch ordering admitted");
    memset(input, 0, sizeof(input));
    memset(actual, 0, sizeof(actual));
    memset(after_xram, 0, sizeof(after_xram));
    ep_mix_shadow_begin(&shadow, true);
    feed(&shadow, input, actual, true, false);
    ep_mix_shadow_finish(&shadow, after_xram, true);
    failed |= check(shadow.matched == 2 && shadow.nonzero_matched == 1,
                    "silent EP frame counted as nonzero");
    return failed ? 1 : 0;
}

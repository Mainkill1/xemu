/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hw/xbox/mcpx/apu/dsp/ep-mix-shadow.h"
#include "hw/xbox/mcpx/apu/dsp/ep-mix-identity.h"

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
    uint32_t before_pram[4096] = { 0 }, after_pram[4096] = { 0 };
    before_pram[1] = 5;
    before_pram[0x116] = 0x100;
    EPMixFrame frame;
    EPMixShadow shadow = { 0 };
    int failed = 0;
    static const unsigned changed_words[] = {
        1, 7, 278, 422, 427, 428, 429, 430
    };
    for (unsigned i = 0; i < sizeof(changed_words) / sizeof(changed_words[0]);
         i++) {
        failed |= check(ep_mix_identity_word(changed_words[i], 0x123456) == 0,
                        "known mutable program word was not masked");
    }
    failed |= check(ep_mix_identity_word(2, 0xff123456) == 0x123456,
                    "stable program word was not retained as 24-bit data");
    failed |= check(
        ep_mix_digest_known(
            "5471f7867c55afea5765c5c3e21065a6f456dc5aab2103a146f06b1d07b1a118"),
        "original EP digest rejected");
    failed |= check(
        ep_mix_digest_known(
            "b4e7bcc65856da0dcbffe792ed3965c74f50ebb549e359622bdf911428f40c36"),
        "captured alternate EP digest rejected");
    failed |= check(!ep_mix_digest_known(NULL) && !ep_mix_digest_known("") &&
                        !ep_mix_digest_known("b4e7bcc65856da0dcbffe792ed3965c74"
                                             "f50ebb549e359622bdf911428f40c37"),
                    "unrecognized EP digest accepted");
    for (unsigned ch = 0; ch < 5; ch++) {
        for (unsigned i = 0; i < 256; i++) {
            input[ch][i] = (i * (ch + 1) * 101) & 0xffffff;
        }
    }
    failed |= check(ep_mix_hle(input, actual), "native prediction failed");
    failed |= check(ep_mix_frame_hle(input, &frame), "frame prediction failed");
    memcpy(after_xram, input, sizeof(input));
    memcpy(after_xram + 0x500, frame.mixed, sizeof(frame.mixed));
    memcpy(after_xram + 0x701, frame.peak, sizeof(frame.peak));
    memcpy(after_xram + 0x703, frame.rms, sizeof(frame.rms));
    after_pram[7] = 1;
    after_pram[0x1a6] = 0x12000;
    memcpy(after_pram + 0x1ab, frame.peak, sizeof(frame.peak));
    memcpy(after_pram + 0x1ad, frame.rms, sizeof(frame.rms));
    ep_mix_shadow_begin(&shadow, true, before_pram);
    feed(&shadow, input, actual, true, false);
    ep_mix_shadow_finish(&shadow, after_xram, after_pram, true);
    failed |= check(
        shadow.matched == 1 && shadow.nonzero_matched == 1 &&
            shadow.mismatched == 0 && shadow.matched_samples == 512 &&
            shadow.matched_words == 1796 && shadow.matched_pram_words == 8,
        "matching EP output rejected");
    ep_mix_shadow_begin(&shadow, true, before_pram);
    feed(&shadow, input, actual, true, true);
    after_xram[0x500] ^= 1;
    after_xram[0x703] ^= 1;
    after_pram[7] ^= 1;
    ep_mix_shadow_finish(&shadow, after_xram, after_pram, true);
    after_xram[0x500] ^= 1;
    after_xram[0x703] ^= 1;
    after_pram[7] ^= 1;
    failed |= check(shadow.matched == 1 && shadow.mismatched == 1 &&
                        shadow.mismatched_samples == 1 &&
                        shadow.mismatched_words == 2 &&
                        shadow.mismatched_pram_words == 1,
                    "corrupted EP output admitted");
    ep_mix_shadow_begin(&shadow, true, before_pram);
    feed(&shadow, input, actual, false, false);
    ep_mix_shadow_finish(&shadow, after_xram, after_pram, true);
    failed |= check(shadow.skipped == 1, "unknown overlay admitted");
    ep_mix_shadow_begin(&shadow, false, before_pram);
    failed |= check(shadow.skipped == 2, "unknown program admitted");
    ep_mix_shadow_begin(&shadow, true, before_pram);
    feed(&shadow, input, actual, true, false);
    ep_mix_shadow_finish(&shadow, after_xram, after_pram, false);
    failed |= check(shadow.invalidated == 1 && shadow.matched == 1,
                    "changed program admitted");
    ep_mix_shadow_begin(&shadow, true, before_pram);
    uint8_t overlay[804] = { 0 };
    ep_mix_shadow_read(&shadow, 0, overlay, sizeof(overlay), true);
    uint8_t bad_input[1024] = { 0 };
    ep_mix_shadow_read(&shadow, 0x10000, bad_input, sizeof(bad_input), false);
    ep_mix_shadow_read(&shadow, 0x10001, bad_input, sizeof(bad_input), false);
    ep_mix_shadow_finish(&shadow, after_xram, after_pram, true);
    failed |=
        check(shadow.skipped == 3, "unexpected scratch ordering admitted");

    before_pram[1] = 0;
    before_pram[0x116] = 0;
    after_pram[1] = 1;
    after_pram[0x116] = 0x100;
    ep_mix_shadow_begin(&shadow, true, before_pram);
    uint8_t extra_overlay[96] = { 0 };
    ep_mix_shadow_read(&shadow, 0x7000, extra_overlay, sizeof(extra_overlay),
                       true);
    feed(&shadow, input, actual, true, false);
    ep_mix_shadow_finish(&shadow, after_xram, after_pram, true);
    failed |= check(shadow.matched == 2 && shadow.nonzero_matched == 2,
                    "periodic EP overlay or program state did not match");

    before_pram[1] = 5;
    before_pram[0x116] = 0x100;
    after_pram[1] = 0;
    after_pram[0x116] = 0;
    memset(input, 0, sizeof(input));
    memset(actual, 0, sizeof(actual));
    memset(after_xram, 0, sizeof(after_xram));
    memset(after_pram + 0x1ab, 0, 4 * sizeof(*after_pram));
    ep_mix_shadow_begin(&shadow, true, before_pram);
    feed(&shadow, input, actual, true, false);
    ep_mix_shadow_finish(&shadow, after_xram, after_pram, true);
    failed |= check(shadow.matched == 3 && shadow.nonzero_matched == 2,
                    "silent EP frame counted as nonzero");

    EPMixShadow rejected = { 0 };
    before_pram[1] = 6;
    ep_mix_shadow_begin(&rejected, true, before_pram);
    before_pram[1] = 0;
    before_pram[0x116] = 0x80;
    ep_mix_shadow_begin(&rejected, true, before_pram);
    before_pram[0x116] = 0;
    ep_mix_shadow_begin(&rejected, true, before_pram);
    ep_mix_shadow_read(&rejected, 0x7000, extra_overlay, sizeof(extra_overlay),
                       false);
    failed |= check(rejected.skipped == 3 && !rejected.armed,
                    "invalid EP phase, slot, or extra overlay admitted");
    return failed ? 1 : 0;
}

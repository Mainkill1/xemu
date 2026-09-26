/*
 * MCPX APU voice mix field tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hw/xbox/mcpx/apu/vp/voice_mix.h"

static void fail_mismatch(const char *field, size_t index, unsigned int actual,
                          unsigned int expected)
{
    fprintf(stderr, "%s[%zu]: got 0x%x, expected 0x%x\n", field, index,
            actual, expected);
    exit(EXIT_FAILURE);
}

static void assert_bins(uint32_t cfg_fmt, uint32_t cfg_vbin,
                        const uint8_t expected[8])
{
    uint8_t actual[8];

    mcpx_apu_voice_decode_mix_bins(cfg_fmt, cfg_vbin, actual);
    for (size_t i = 0; i < sizeof(actual) / sizeof(actual[0]); i++) {
        if (actual[i] != expected[i]) {
            fail_mismatch("bin", i, actual[i], expected[i]);
        }
    }
}

static void assert_volumes(uint32_t vola, uint32_t volb, uint32_t volc,
                           const uint16_t expected[8])
{
    uint16_t actual[8];

    mcpx_apu_voice_decode_mix_volumes(vola, volb, volc, actual);
    for (size_t i = 0; i < sizeof(actual) / sizeof(actual[0]); i++) {
        if (actual[i] != expected[i]) {
            fail_mismatch("volume", i, actual[i], expected[i]);
        }
    }
}

static void test_mix_bins_boundaries(void)
{
    static const uint8_t zero[8] = { 0 };
    static const uint8_t maximum[8] = {
        31, 31, 31, 31, 31, 31, 31, 31,
    };
    static const uint8_t distinct[8] = {
        1, 2, 3, 4, 5, 6, 7, 8,
    };

    assert_bins(0, 0, zero);
    assert_bins(0x000003ff, 0x7fffffff, maximum);
    assert_bins(0x00000107, 0x18a40c41, distinct);
}

static void test_mix_volumes_boundaries(void)
{
    static const uint16_t zero[8] = { 0 };
    static const uint16_t maximum[8] = {
        0xfff, 0xfff, 0xfff, 0xfff, 0xfff, 0xfff, 0xfff, 0xfff,
    };
    static const uint16_t distinct[8] = {
        0x123, 0x456, 0x789, 0xabc, 0xdef, 0xfed, 0x135, 0xeca,
    };

    assert_volumes(0, 0, 0, zero);
    assert_volumes(UINT32_MAX, UINT32_MAX, UINT32_MAX, maximum);
    assert_volumes(0x456a1235, 0xabcc7893, 0xfededef1, distinct);
}

static void test_split_volume_nibbles(void)
{
    static const uint16_t low[8] = { 0, 0, 0, 0, 0, 0, 0x00a, 0x005 };
    static const uint16_t middle[8] = { 0, 0, 0, 0, 0, 0, 0x0b0, 0x0c0 };
    static const uint16_t high[8] = { 0, 0, 0, 0, 0, 0, 0xd00, 0xe00 };

    assert_volumes(0x0005000a, 0, 0, low);
    assert_volumes(0, 0x000c000b, 0, middle);
    assert_volumes(0, 0, 0x000e000d, high);
}

static void decode_bins_legacy(uint32_t cfg_fmt, uint32_t cfg_vbin,
                               uint8_t bins[8])
{
    bins[0] = (cfg_vbin >> 0) & 0x1f;
    bins[1] = (cfg_vbin >> 5) & 0x1f;
    bins[2] = (cfg_vbin >> 10) & 0x1f;
    bins[3] = (cfg_vbin >> 16) & 0x1f;
    bins[4] = (cfg_vbin >> 21) & 0x1f;
    bins[5] = (cfg_vbin >> 26) & 0x1f;
    bins[6] = (cfg_fmt >> 0) & 0x1f;
    bins[7] = (cfg_fmt >> 5) & 0x1f;
}

static void decode_volumes_legacy(uint32_t vola, uint32_t volb,
                                  uint32_t volc, uint16_t volumes[8])
{
    volumes[0] = (vola >> 4) & 0xfff;
    volumes[1] = (vola >> 20) & 0xfff;
    volumes[2] = (volb >> 4) & 0xfff;
    volumes[3] = (volb >> 20) & 0xfff;
    volumes[4] = (volc >> 4) & 0xfff;
    volumes[5] = (volc >> 20) & 0xfff;
    volumes[6] = ((volc >> 0) & 0xf) << 8 |
                 ((volb >> 0) & 0xf) << 4 |
                 ((vola >> 0) & 0xf);
    volumes[7] = ((volc >> 16) & 0xf) << 8 |
                 ((volb >> 16) & 0xf) << 4 |
                 ((vola >> 16) & 0xf);
}

static uint32_t next_word(uint32_t *state)
{
    *state = *state * 1664525U + 1013904223U;
    return *state;
}

static void test_legacy_equivalence(void)
{
    uint32_t state = 0x4d435058;

    for (unsigned int iteration = 0; iteration < 4096; iteration++) {
        uint32_t cfg_fmt = next_word(&state);
        uint32_t cfg_vbin = next_word(&state);
        uint32_t vola = next_word(&state);
        uint32_t volb = next_word(&state);
        uint32_t volc = next_word(&state);
        uint8_t expected_bins[8];
        uint8_t actual_bins[8];
        uint16_t expected_volumes[8];
        uint16_t actual_volumes[8];

        decode_bins_legacy(cfg_fmt, cfg_vbin, expected_bins);
        mcpx_apu_voice_decode_mix_bins(cfg_fmt, cfg_vbin, actual_bins);
        decode_volumes_legacy(vola, volb, volc, expected_volumes);
        mcpx_apu_voice_decode_mix_volumes(vola, volb, volc, actual_volumes);

        if (memcmp(actual_bins, expected_bins, sizeof(actual_bins)) != 0) {
            fprintf(stderr, "bin legacy mismatch at iteration %u\n",
                    iteration);
            exit(EXIT_FAILURE);
        }
        if (memcmp(actual_volumes, expected_volumes,
                   sizeof(actual_volumes)) != 0) {
            fprintf(stderr, "volume legacy mismatch at iteration %u\n",
                    iteration);
            exit(EXIT_FAILURE);
        }
    }
}

int main(void)
{
    test_mix_bins_boundaries();
    test_mix_volumes_boundaries();
    test_split_volume_nibbles();
    test_legacy_equivalence();
    return EXIT_SUCCESS;
}

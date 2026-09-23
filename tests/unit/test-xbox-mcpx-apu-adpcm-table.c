/*
 * MCPX APU ADPCM nibble-table tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "hw/xbox/mcpx/apu/vp/adpcm.h"

static const uint16_t reference_steps[MCPX_ADPCM_INDEX_COUNT] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,
    17,    19,    21,    23,    25,    28,    31,    34,    37,
    41,    45,    50,    55,    60,    66,    73,    80,    88,
    97,    107,   118,   130,   143,   157,   173,   190,   209,
    230,   253,   279,   307,   337,   371,   408,   449,   494,
    544,   598,   658,   724,   796,   876,   963,   1060,  1166,
    1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749,
    3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,
    7132,  7845,  8630,  9493,  10442, 11487, 12635, 13899, 15289,
    16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
};

static int clamp_index(int index)
{
    if (index < 0) {
        return 0;
    }
    if (index > 88) {
        return 88;
    }
    return index;
}

static int test_table_exhaustive(void)
{
    static const int index_delta[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };
    MCPXAPUADPCMDecodeTable table;

    mcpx_apu_adpcm_decode_table_init(&table);
    for (int index = 0; index < MCPX_ADPCM_INDEX_COUNT; index++) {
        int step = reference_steps[index];

        for (int nibble = 0; nibble < MCPX_ADPCM_NIBBLE_COUNT; nibble++) {
            int delta = step >> 3;

            if (nibble & 1) {
                delta += step >> 2;
            }
            if (nibble & 2) {
                delta += step >> 1;
            }
            if (nibble & 4) {
                delta += step;
            }
            if (nibble & 8) {
                delta = -delta;
            }

            if (table.delta[index][nibble] != delta ||
                table.next_index[index][nibble] !=
                    clamp_index(index + index_delta[nibble & 7])) {
                fprintf(stderr, "table mismatch at index %d nibble %d\n",
                        index, nibble);
                return 1;
            }
        }
    }
    return 0;
}

static uint64_t hash_samples(const int16_t *samples, int count)
{
    uint64_t hash = UINT64_C(1469598103934665603);

    for (int i = 0; i < count; i++) {
        uint16_t value = samples[i];
        hash ^= value & 0xff;
        hash *= UINT64_C(1099511628211);
        hash ^= value >> 8;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static int check_block(MCPXAPUADPCMDecodeTable *table, uint8_t *block,
                       size_t bytes, int channels, uint64_t expected_hash)
{
    int16_t samples[65 * 2] = { 0 };
    int count = adpcm_decode_block(table, samples, block, bytes, channels);

    return count != 65 ||
           hash_samples(samples, count * channels) != expected_hash;
}

static int test_golden_blocks(void)
{
    MCPXAPUADPCMDecodeTable table;
    uint8_t mono[36] = { 0x34, 0x12, 44, 0 };
    uint8_t stereo[72] = { 0xdc, 0xfe, 12, 0, 0x20, 0x03, 80, 0 };
    uint8_t positive[36] = { 0x00, 0x7f, 88, 0 };
    uint8_t negative[36] = { 0x00, 0x81, 88, 0 };

    for (size_t i = 4; i < sizeof(mono); i++) {
        mono[i] = (i * 37 + 11) & 0xff;
    }
    for (size_t i = 8; i < sizeof(stereo); i++) {
        stereo[i] = (i * 53 + 7) & 0xff;
    }
    memset(positive + 4, 0x77, sizeof(positive) - 4);
    memset(negative + 4, 0xff, sizeof(negative) - 4);

    mcpx_apu_adpcm_decode_table_init(&table);
    return check_block(&table, mono, sizeof(mono), 1,
                       UINT64_C(0x5e97ac483abd7d47)) ||
           check_block(&table, stereo, sizeof(stereo), 2,
                       UINT64_C(0xe938b3d250bf3b45)) ||
           check_block(&table, positive, sizeof(positive), 1,
                       UINT64_C(0x2d9a29798250e552)) ||
           check_block(&table, negative, sizeof(negative), 1,
                       UINT64_C(0x3c3224e29a9b6dc8));
}

int main(void)
{
    int exhaustive = test_table_exhaustive();
    int golden = test_golden_blocks();

    puts("TAP version 13");
    puts("1..2");
    printf("%s 1 - every ADPCM index and nibble matches scalar semantics\n",
           exhaustive ? "not ok" : "ok");
    printf("%s 2 - mono, stereo, and clipping blocks match golden output\n",
           golden ? "not ok" : "ok");
    return exhaustive || golden;
}

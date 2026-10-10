/*
 * MCPX APU ADPCM nibble-table tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

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

static int test_cached_blocks(void)
{
    MCPXAPUADPCMDecodeTable table;
    MCPXADPCMBlockCache cache = { 0 };
    uint8_t block[72] = { 0 };
    int16_t expected[65 * 2];
    int count;
    bool hit;

    mcpx_apu_adpcm_decode_table_init(&table);
    for (unsigned int channels = 1; channels <= 2; channels++) {
        size_t bytes = 36 * channels;
        memset(block, 0, sizeof(block));
        mcpx_apu_adpcm_cache_reset(&cache);
        for (unsigned int i = 4 * channels; i < bytes; i++) {
            block[i] = (i * 37 + 11) & 0xff;
        }
        int expected_count = adpcm_decode_block(&table, expected, block,
                                                bytes, channels);
        const int16_t *samples = mcpx_apu_adpcm_decode_cached(
            &table, &cache, block, bytes, channels, &count, &hit);
        if (hit || count != expected_count || !samples ||
            memcmp(samples, expected, count * channels * sizeof(*samples))) {
            return 1;
        }
        samples = mcpx_apu_adpcm_decode_cached(
            &table, &cache, block, bytes, channels, &count, &hit);
        if (!hit || samples != cache.decoded) {
            return 1;
        }
        block[bytes - 1] ^= 0xff;
        expected_count = adpcm_decode_block(&table, expected, block,
                                            bytes, channels);
        samples = mcpx_apu_adpcm_decode_cached(
            &table, &cache, block, bytes, channels, &count, &hit);
        if (hit || count != expected_count || !samples ||
            memcmp(samples, expected, count * channels * sizeof(*samples))) {
            return 1;
        }
    }
    return 0;
}

static int check_rejected_block(MCPXAPUADPCMDecodeTable *table,
                                MCPXADPCMBlockCache *cache,
                                const uint8_t *valid,
                                unsigned int valid_channels,
                                const uint8_t *invalid, size_t invalid_size,
                                unsigned int invalid_channels)
{
    int16_t expected[MCPX_ADPCM_MAX_DECODED_SAMPLES];
    size_t valid_size = 36 * valid_channels;
    int count;
    bool hit;
    const int16_t *samples;

    mcpx_apu_adpcm_cache_reset(cache);
    samples = mcpx_apu_adpcm_decode_cached(table, cache, valid, valid_size,
                                          valid_channels, &count, &hit);
    if (!samples || hit || count != 65) {
        return 1;
    }
    memcpy(expected, samples, count * valid_channels * sizeof(*samples));

    count = -1;
    hit = true;
    samples = mcpx_apu_adpcm_decode_cached(table, cache, invalid, invalid_size,
                                          invalid_channels, &count, &hit);
    if (samples || count || hit || cache->valid) {
        return 1;
    }

    samples = mcpx_apu_adpcm_decode_cached(table, cache, valid, valid_size,
                                          valid_channels, &count, &hit);
    return !samples || hit || count != 65 ||
           memcmp(samples, expected,
                  count * valid_channels * sizeof(*samples));
}

static int test_rejected_blocks(void)
{
    MCPXAPUADPCMDecodeTable table;
    MCPXADPCMBlockCache cache = { 0 };
    uint8_t valid[MCPX_ADPCM_MAX_BLOCK_BYTES] = { 0 };
    uint8_t invalid[MCPX_ADPCM_MAX_BLOCK_BYTES + 1];

    mcpx_apu_adpcm_decode_table_init(&table);
    for (unsigned int channels = 1; channels <= 2; channels++) {
        size_t bytes = 36 * channels;

        memset(valid, 0, sizeof(valid));
        for (unsigned int ch = 0; ch < channels; ch++) {
            valid[ch * 4] = 0x34;
            valid[ch * 4 + 1] = 0x12;
            valid[ch * 4 + 2] = 44;
        }
        for (size_t i = 4 * channels; i < bytes; i++) {
            valid[i] = (i * 53 + 7) & 0xff;
        }

        for (size_t size = 0; size < 4 * channels; size++) {
            if (check_rejected_block(&table, &cache, valid, channels,
                                     size ? valid : NULL, size, channels)) {
                return 1;
            }
        }
        if (check_rejected_block(&table, &cache, valid, channels,
                                 valid, bytes, 0) ||
            check_rejected_block(&table, &cache, valid, channels,
                                 valid, bytes, 3)) {
            return 1;
        }

        memcpy(invalid, valid, sizeof(valid));
        invalid[sizeof(valid)] = 0;
        if (check_rejected_block(&table, &cache, valid, channels,
                                 invalid, sizeof(invalid), channels)) {
            return 1;
        }
        if (channels == 1 &&
            check_rejected_block(&table, &cache, valid, channels,
                                 invalid, sizeof(valid), channels)) {
            return 1;
        }

        for (unsigned int ch = 0; ch < channels; ch++) {
            static const uint8_t bad_indices[] = { 89, 255 };

            for (size_t i = 0; i < ARRAY_SIZE(bad_indices); i++) {
                memcpy(invalid, valid, sizeof(valid));
                invalid[ch * 4 + 2] = bad_indices[i];
                if (check_rejected_block(&table, &cache, valid, channels,
                                         invalid, bytes, channels)) {
                    return 1;
                }
            }
            memcpy(invalid, valid, sizeof(valid));
            invalid[ch * 4 + 3] = 1;
            if (check_rejected_block(&table, &cache, valid, channels,
                                     invalid, bytes, channels)) {
                return 1;
            }
        }
    }
    return 0;
}

int main(void)
{
    int exhaustive = test_table_exhaustive();
    int golden = test_golden_blocks();
    int cached = test_cached_blocks();
    int rejected = test_rejected_blocks();

    puts("TAP version 13");
    puts("1..4");
    printf("%s 1 - every ADPCM index and nibble matches scalar semantics\n",
           exhaustive ? "not ok" : "ok");
    printf("%s 2 - mono, stereo, and clipping blocks match golden output\n",
           golden ? "not ok" : "ok");
    printf("%s 3 - cached and changed blocks preserve decoded output\n",
           cached ? "not ok" : "ok");
    printf("%s 4 - rejected blocks invalidate and recover without stale PCM\n",
           rejected ? "not ok" : "ok");
    return exhaustive || golden || cached || rejected;
}

/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "hw/xbox/mcpx/apu/vp/adpcm.h"

static void init_mono_block(uint8_t block[36])
{
    block[0] = 0x34;
    block[1] = 0x12;
    block[2] = 10;
    block[3] = 0;

    for (int i = 4; i < 36; i++) {
        block[i] = i * 7 + 3;
    }
}

static void init_stereo_block(uint8_t block[72])
{
    block[0] = 0x34;
    block[1] = 0x12;
    block[2] = 10;
    block[3] = 0;
    block[4] = 0x78;
    block[5] = 0x56;
    block[6] = 20;
    block[7] = 0;

    for (int i = 8; i < 72; i++) {
        block[i] = i * 5 + 11;
    }
}

static void test_cache_reuses_unchanged_block(void)
{
    uint8_t block[36];
    int16_t expected[65];
    MCPXADPCMBlockCache cache = { 0 };
    int sample_count;
    bool cache_hit;

    init_mono_block(block);
    int expected_count = adpcm_decode_block(expected, block, sizeof(block), 1);

    const int16_t *decoded = mcpx_apu_adpcm_decode_cached(
        &cache, block, sizeof(block), 1, &sample_count, &cache_hit);
    g_assert_nonnull(decoded);
    g_assert_false(cache_hit);
    g_assert_cmpint(sample_count, ==, expected_count);
    g_assert_cmpmem(decoded, sample_count * sizeof(*decoded), expected,
                    expected_count * sizeof(*expected));

    decoded = mcpx_apu_adpcm_decode_cached(
        &cache, block, sizeof(block), 1, &sample_count, &cache_hit);
    g_assert_nonnull(decoded);
    g_assert_true(cache_hit);
    g_assert_cmpint(sample_count, ==, expected_count);
    g_assert_cmpmem(decoded, sample_count * sizeof(*decoded), expected,
                    expected_count * sizeof(*expected));
}

static void test_cache_detects_guest_data_change(void)
{
    uint8_t block[36];
    int16_t first_decode[65];
    MCPXADPCMBlockCache cache = { 0 };
    int sample_count;
    bool cache_hit;

    init_mono_block(block);
    const int16_t *decoded = mcpx_apu_adpcm_decode_cached(
        &cache, block, sizeof(block), 1, &sample_count, &cache_hit);
    g_assert_nonnull(decoded);
    g_assert_false(cache_hit);
    memcpy(first_decode, decoded, sizeof(first_decode));

    block[11] ^= 0x55;
    decoded = mcpx_apu_adpcm_decode_cached(
        &cache, block, sizeof(block), 1, &sample_count, &cache_hit);
    g_assert_nonnull(decoded);
    g_assert_false(cache_hit);
    g_assert_cmpint(memcmp(decoded, first_decode, sizeof(first_decode)), !=, 0);
}

static void test_cache_reuses_unchanged_stereo_block(void)
{
    uint8_t block[72];
    int16_t expected[130];
    MCPXADPCMBlockCache cache = { 0 };
    int sample_count;
    bool cache_hit;

    init_stereo_block(block);
    int expected_count = adpcm_decode_block(expected, block, sizeof(block), 2);

    const int16_t *decoded = mcpx_apu_adpcm_decode_cached(
        &cache, block, sizeof(block), 2, &sample_count, &cache_hit);
    g_assert_nonnull(decoded);
    g_assert_false(cache_hit);
    g_assert_cmpint(sample_count, ==, expected_count);
    g_assert_cmpmem(decoded, sample_count * 2 * sizeof(*decoded), expected,
                    expected_count * 2 * sizeof(*expected));

    decoded = mcpx_apu_adpcm_decode_cached(
        &cache, block, sizeof(block), 2, &sample_count, &cache_hit);
    g_assert_nonnull(decoded);
    g_assert_true(cache_hit);
    g_assert_cmpint(sample_count, ==, expected_count);
    g_assert_cmpmem(decoded, sample_count * 2 * sizeof(*decoded), expected,
                    expected_count * 2 * sizeof(*expected));
}

static void test_cache_detects_format_changes(void)
{
    uint8_t block[72];
    int16_t expected[130];
    MCPXADPCMBlockCache cache = { 0 };
    int sample_count;
    bool cache_hit;

    init_stereo_block(block);
    g_assert_nonnull(mcpx_apu_adpcm_decode_cached(
        &cache, block, sizeof(block), 2, &sample_count, &cache_hit));
    g_assert_false(cache_hit);
    g_assert_nonnull(mcpx_apu_adpcm_decode_cached(
        &cache, block, sizeof(block), 2, &sample_count, &cache_hit));
    g_assert_true(cache_hit);

    int expected_count = adpcm_decode_block(expected, block, 36, 1);
    const int16_t *decoded = mcpx_apu_adpcm_decode_cached(
        &cache, block, 36, 1, &sample_count, &cache_hit);
    g_assert_nonnull(decoded);
    g_assert_false(cache_hit);
    g_assert_cmpint(sample_count, ==, expected_count);
    g_assert_cmpmem(decoded, sample_count * sizeof(*decoded), expected,
                    expected_count * sizeof(*expected));

    expected_count = adpcm_decode_block(expected, block, 32, 1);
    decoded = mcpx_apu_adpcm_decode_cached(
        &cache, block, 32, 1, &sample_count, &cache_hit);
    g_assert_nonnull(decoded);
    g_assert_false(cache_hit);
    g_assert_cmpint(sample_count, ==, expected_count);
    g_assert_cmpmem(decoded, sample_count * sizeof(*decoded), expected,
                    expected_count * sizeof(*expected));
}

static void test_cache_rejects_invalid_blocks(void)
{
    uint8_t block[MCPX_ADPCM_MAX_BLOCK_BYTES + 1];
    MCPXADPCMBlockCache cache = { 0 };
    int sample_count = -1;
    bool cache_hit = true;

    init_mono_block(block);
    g_assert_nonnull(mcpx_apu_adpcm_decode_cached(
        &cache, block, 36, 1, &sample_count, &cache_hit));
    g_assert_false(cache_hit);
    g_assert_true(cache.valid);

    g_assert_null(mcpx_apu_adpcm_decode_cached(
        &cache, block, 3, 1, &sample_count, &cache_hit));
    g_assert_cmpint(sample_count, ==, 0);
    g_assert_false(cache_hit);
    g_assert_false(cache.valid);

    g_assert_null(mcpx_apu_adpcm_decode_cached(
        &cache, block, sizeof(block), 1, &sample_count, &cache_hit));
    g_assert_cmpint(sample_count, ==, 0);
    g_assert_false(cache_hit);
    g_assert_false(cache.valid);

    g_assert_null(mcpx_apu_adpcm_decode_cached(
        &cache, block, 36, 0, &sample_count, &cache_hit));
    g_assert_null(mcpx_apu_adpcm_decode_cached(
        &cache, block, 36, 3, &sample_count, &cache_hit));

    block[2] = 89;
    g_assert_null(mcpx_apu_adpcm_decode_cached(
        &cache, block, 36, 1, &sample_count, &cache_hit));
    g_assert_false(cache.valid);
    block[2] = 10;
    block[3] = 1;
    g_assert_null(mcpx_apu_adpcm_decode_cached(
        &cache, block, 36, 1, &sample_count, &cache_hit));
    g_assert_false(cache.valid);

    block[3] = 0;
    g_assert_nonnull(mcpx_apu_adpcm_decode_cached(
        &cache, block, 36, 1, &sample_count, &cache_hit));
    g_assert_false(cache_hit);
}

static void test_cache_rejects_output_overflow_before_decode(void)
{
    struct {
        MCPXADPCMBlockCache cache;
        uint8_t guard[32];
    } fixture = { 0 };
    uint8_t block[MCPX_ADPCM_MAX_BLOCK_BYTES] = { 0 };
    int sample_count = -1;
    bool cache_hit = true;

    memset(fixture.guard, 0xa5, sizeof(fixture.guard));

    g_assert_null(mcpx_apu_adpcm_decode_cached(
        &fixture.cache, block, sizeof(block), 1, &sample_count, &cache_hit));
    g_assert_cmpint(sample_count, ==, 0);
    g_assert_false(cache_hit);
    g_assert_false(fixture.cache.valid);

    for (size_t i = 0; i < sizeof(fixture.guard); i++) {
        g_assert_cmphex(fixture.guard[i], ==, 0xa5);
    }
}

static void test_cache_input_capacity_sweep(void)
{
    uint8_t block[MCPX_ADPCM_MAX_BLOCK_BYTES + 1] = { 0 };
    int16_t reference[300];

    for (unsigned int channels = 0; channels <= 3; channels++) {
        for (size_t encoded_size = 0; encoded_size <= sizeof(block);
             encoded_size++) {
            struct {
                MCPXADPCMBlockCache cache;
                uint8_t guard[32];
            } fixture = { 0 };
            int sample_count = -1;
            bool cache_hit = true;
            int expected_count = 0;

            memset(fixture.guard, 0xa5, sizeof(fixture.guard));
            if (channels == 1 || channels == 2) {
                expected_count = adpcm_decode_block(
                    reference, block, encoded_size, channels);
            }
            bool fits = encoded_size <= MCPX_ADPCM_MAX_BLOCK_BYTES &&
                        expected_count > 0 &&
                        expected_count * channels <=
                            MCPX_ADPCM_MAX_DECODED_SAMPLES;

            const int16_t *decoded = mcpx_apu_adpcm_decode_cached(
                &fixture.cache, block, encoded_size, channels, &sample_count,
                &cache_hit);

            if (fits) {
                g_assert_nonnull(decoded);
                g_assert_cmpint(sample_count, ==, expected_count);
                g_assert_cmpmem(decoded,
                                sample_count * channels * sizeof(*decoded),
                                reference,
                                expected_count * channels * sizeof(*reference));
            } else {
                g_assert_null(decoded);
                g_assert_cmpint(sample_count, ==, 0);
                g_assert_false(fixture.cache.valid);
            }
            g_assert_false(cache_hit);

            for (size_t i = 0; i < sizeof(fixture.guard); i++) {
                g_assert_cmphex(fixture.guard[i], ==, 0xa5);
            }
        }
    }
}

static void test_cache_reset_forces_decode(void)
{
    uint8_t block[72];
    MCPXADPCMBlockCache cache = { 0 };
    int sample_count;
    bool cache_hit;

    init_stereo_block(block);
    g_assert_nonnull(mcpx_apu_adpcm_decode_cached(
        &cache, block, sizeof(block), 2, &sample_count, &cache_hit));
    g_assert_false(cache_hit);
    g_assert_nonnull(mcpx_apu_adpcm_decode_cached(
        &cache, block, sizeof(block), 2, &sample_count, &cache_hit));
    g_assert_true(cache_hit);

    mcpx_apu_adpcm_cache_reset(&cache);
    g_assert_nonnull(mcpx_apu_adpcm_decode_cached(
        &cache, block, sizeof(block), 2, &sample_count, &cache_hit));
    g_assert_false(cache_hit);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/mcpx/apu/adpcm/cache-reuses-unchanged-block",
                    test_cache_reuses_unchanged_block);
    g_test_add_func("/mcpx/apu/adpcm/cache-detects-guest-data-change",
                    test_cache_detects_guest_data_change);
    g_test_add_func("/mcpx/apu/adpcm/cache-reuses-unchanged-stereo-block",
                    test_cache_reuses_unchanged_stereo_block);
    g_test_add_func("/mcpx/apu/adpcm/cache-detects-format-changes",
                    test_cache_detects_format_changes);
    g_test_add_func("/mcpx/apu/adpcm/cache-rejects-invalid-blocks",
                    test_cache_rejects_invalid_blocks);
    g_test_add_func("/mcpx/apu/adpcm/rejects-output-overflow-before-decode",
                    test_cache_rejects_output_overflow_before_decode);
    g_test_add_func("/mcpx/apu/adpcm/input-capacity-sweep",
                    test_cache_input_capacity_sweep);
    g_test_add_func("/mcpx/apu/adpcm/cache-reset-forces-decode",
                    test_cache_reset_forces_decode);
    return g_test_run();
}

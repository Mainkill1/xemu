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

static void test_cache_reset_forces_decode(void)
{
    uint8_t block[36];
    MCPXADPCMBlockCache cache = { 0 };
    int sample_count;
    bool cache_hit;

    init_mono_block(block);
    g_assert_nonnull(mcpx_apu_adpcm_decode_cached(
        &cache, block, sizeof(block), 1, &sample_count, &cache_hit));
    g_assert_false(cache_hit);
    g_assert_nonnull(mcpx_apu_adpcm_decode_cached(
        &cache, block, sizeof(block), 1, &sample_count, &cache_hit));
    g_assert_true(cache_hit);

    mcpx_apu_adpcm_cache_reset(&cache);
    g_assert_nonnull(mcpx_apu_adpcm_decode_cached(
        &cache, block, sizeof(block), 1, &sample_count, &cache_hit));
    g_assert_false(cache_hit);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/mcpx/apu/adpcm/cache-reuses-unchanged-block",
                    test_cache_reuses_unchanged_block);
    g_test_add_func("/mcpx/apu/adpcm/cache-detects-guest-data-change",
                    test_cache_detects_guest_data_change);
    g_test_add_func("/mcpx/apu/adpcm/cache-reset-forces-decode",
                    test_cache_reset_forces_decode);
    return g_test_run();
}

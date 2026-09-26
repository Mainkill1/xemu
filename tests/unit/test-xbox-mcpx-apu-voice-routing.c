/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"

#include "hw/xbox/mcpx/apu/apu_regs.h"
#include "hw/xbox/mcpx/apu/vp/voice_routing.h"

#define FIELD(value, mask) (((uint32_t)(value) << ctz32(mask)) & (mask))
#define FIELD_VALUE(word, mask) (((word) & (mask)) >> ctz32(mask))

static MCPXAPUVoiceRouting reference_routing(uint32_t cfg_fmt,
                                             uint32_t cfg_vbin, bool is_3d,
                                             const uint8_t hrtf_submix[4])
{
    MCPXAPUVoiceRouting routing = { 0 };
    int bins[8];

    if (FIELD_VALUE(cfg_fmt, NV_PAVS_VOICE_CFG_FMT_MULTIPASS)) {
        int mp_bin = FIELD_VALUE(cfg_fmt,
                                 NV_PAVS_VOICE_CFG_FMT_MULTIPASS_BIN);

        routing.src |= 1U << mp_bin;
        if (FIELD_VALUE(cfg_fmt, NV_PAVS_VOICE_CFG_FMT_CLEAR_MIX)) {
            routing.clr |= 1U << mp_bin;
        }
    }

    if (is_3d) {
        for (int i = 0; i < 4; i++) {
            bins[i] = hrtf_submix[i];
        }
    } else {
        bins[0] = FIELD_VALUE(cfg_vbin, NV_PAVS_VOICE_CFG_VBIN_V0BIN);
        bins[1] = FIELD_VALUE(cfg_vbin, NV_PAVS_VOICE_CFG_VBIN_V1BIN);
        bins[2] = FIELD_VALUE(cfg_vbin, NV_PAVS_VOICE_CFG_VBIN_V2BIN);
        bins[3] = FIELD_VALUE(cfg_vbin, NV_PAVS_VOICE_CFG_VBIN_V3BIN);
    }
    bins[4] = FIELD_VALUE(cfg_vbin, NV_PAVS_VOICE_CFG_VBIN_V4BIN);
    bins[5] = FIELD_VALUE(cfg_vbin, NV_PAVS_VOICE_CFG_VBIN_V5BIN);
    bins[6] = FIELD_VALUE(cfg_fmt, NV_PAVS_VOICE_CFG_FMT_V6BIN);
    bins[7] = FIELD_VALUE(cfg_fmt, NV_PAVS_VOICE_CFG_FMT_V7BIN);

    for (size_t i = 0; i < ARRAY_SIZE(bins); i++) {
        routing.dst |= 1U << bins[i];
    }
    return routing;
}

static void assert_routing_equal(uint32_t cfg_fmt, uint32_t cfg_vbin,
                                 bool is_3d,
                                 const uint8_t hrtf_submix[4])
{
    MCPXAPUVoiceRouting expected =
        reference_routing(cfg_fmt, cfg_vbin, is_3d, hrtf_submix);
    MCPXAPUVoiceRouting actual =
        mcpx_apu_decode_voice_routing(cfg_fmt, cfg_vbin, is_3d,
                                      hrtf_submix);

    g_assert_cmphex(actual.src, ==, expected.src);
    g_assert_cmphex(actual.dst, ==, expected.dst);
    g_assert_cmphex(actual.clr, ==, expected.clr);
}

static void test_explicit_routing_cases(void)
{
    static const uint8_t hrtf_submix[4] = { 31, 30, 29, 28 };
    uint32_t cfg_fmt =
        NV_PAVS_VOICE_CFG_FMT_MULTIPASS |
        NV_PAVS_VOICE_CFG_FMT_CLEAR_MIX |
        FIELD(27, NV_PAVS_VOICE_CFG_FMT_MULTIPASS_BIN) |
        FIELD(6, NV_PAVS_VOICE_CFG_FMT_V6BIN) |
        FIELD(7, NV_PAVS_VOICE_CFG_FMT_V7BIN);
    uint32_t cfg_vbin =
        FIELD(0, NV_PAVS_VOICE_CFG_VBIN_V0BIN) |
        FIELD(1, NV_PAVS_VOICE_CFG_VBIN_V1BIN) |
        FIELD(2, NV_PAVS_VOICE_CFG_VBIN_V2BIN) |
        FIELD(3, NV_PAVS_VOICE_CFG_VBIN_V3BIN) |
        FIELD(4, NV_PAVS_VOICE_CFG_VBIN_V4BIN) |
        FIELD(5, NV_PAVS_VOICE_CFG_VBIN_V5BIN);

    assert_routing_equal(cfg_fmt, cfg_vbin, false, hrtf_submix);
    assert_routing_equal(cfg_fmt, cfg_vbin, true, hrtf_submix);
    assert_routing_equal(0, 0, false, hrtf_submix);
    assert_routing_equal(
        NV_PAVS_VOICE_CFG_FMT_MULTIPASS |
            FIELD(31, NV_PAVS_VOICE_CFG_FMT_MULTIPASS_BIN),
        UINT32_MAX, true, hrtf_submix);
}

static void test_matches_reference_words(void)
{
    uint32_t state = UINT32_C(0x6d2b79f5);

    for (int i = 0; i < 10000; i++) {
        uint8_t hrtf_submix[4];

        state = state * UINT32_C(1664525) + UINT32_C(1013904223);
        uint32_t cfg_fmt = state;
        state = state * UINT32_C(1664525) + UINT32_C(1013904223);
        uint32_t cfg_vbin = state;
        for (size_t bin = 0; bin < ARRAY_SIZE(hrtf_submix); bin++) {
            state = state * UINT32_C(1664525) + UINT32_C(1013904223);
            hrtf_submix[bin] = state & 31;
        }

        assert_routing_equal(cfg_fmt, cfg_vbin, false, hrtf_submix);
        assert_routing_equal(cfg_fmt, cfg_vbin, true, hrtf_submix);
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/mcpx-apu/voice-routing/explicit",
                    test_explicit_routing_cases);
    g_test_add_func("/mcpx-apu/voice-routing/reference-words",
                    test_matches_reference_words);

    return g_test_run();
}

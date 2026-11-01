/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"

#include "hw/xbox/mcpx/apu/apu_regs.h"
#include "hw/xbox/mcpx/apu/vp/voice_format.h"

#define FIELD(value, mask) (((uint32_t)(value) << ctz32(mask)) & (mask))

static void test_decodes_zero_format(void)
{
    MCPXAPUVoiceFormat format = mcpx_apu_decode_voice_format(0);

    g_assert_false(format.stereo);
    g_assert_cmpuint(format.channels, ==, 1);
    g_assert_cmpuint(format.sample_size, ==,
                     NV_PAVS_VOICE_CFG_FMT_SAMPLE_SIZE_U8);
    g_assert_cmpuint(format.container_size_index, ==,
                     NV_PAVS_VOICE_CFG_FMT_CONTAINER_SIZE_B8);
    g_assert_false(format.stream);
    g_assert_false(format.loop);
    g_assert_cmpuint(format.samples_per_block, ==, 1);
    g_assert_false(format.persist);
    g_assert_false(format.multipass);
    g_assert_false(format.linked);
}

static void test_decodes_all_sample_fields(void)
{
    uint32_t word =
        NV_PAVS_VOICE_CFG_FMT_STEREO | NV_PAVS_VOICE_CFG_FMT_DATA_TYPE |
        NV_PAVS_VOICE_CFG_FMT_LOOP | NV_PAVS_VOICE_CFG_FMT_PERSIST |
        NV_PAVS_VOICE_CFG_FMT_MULTIPASS | NV_PAVS_VOICE_CFG_FMT_LINKED |
        FIELD(NV_PAVS_VOICE_CFG_FMT_SAMPLE_SIZE_S24,
              NV_PAVS_VOICE_CFG_FMT_SAMPLE_SIZE) |
        FIELD(NV_PAVS_VOICE_CFG_FMT_CONTAINER_SIZE_ADPCM,
              NV_PAVS_VOICE_CFG_FMT_CONTAINER_SIZE) |
        FIELD(26, NV_PAVS_VOICE_CFG_FMT_SAMPLES_PER_BLOCK);
    MCPXAPUVoiceFormat format = mcpx_apu_decode_voice_format(word);

    g_assert_true(format.stereo);
    g_assert_cmpuint(format.channels, ==, 2);
    g_assert_cmpuint(format.sample_size, ==,
                     NV_PAVS_VOICE_CFG_FMT_SAMPLE_SIZE_S24);
    g_assert_cmpuint(format.container_size_index, ==,
                     NV_PAVS_VOICE_CFG_FMT_CONTAINER_SIZE_ADPCM);
    g_assert_true(format.stream);
    g_assert_true(format.loop);
    g_assert_cmpuint(format.samples_per_block, ==, 27);
    g_assert_true(format.persist);
    g_assert_true(format.multipass);
    g_assert_true(format.linked);
}

static void test_ignores_unrelated_format_fields(void)
{
    uint32_t word = NV_PAVS_VOICE_CFG_FMT_V6BIN | NV_PAVS_VOICE_CFG_FMT_V7BIN |
                    NV_PAVS_VOICE_CFG_FMT_HEADROOM |
                    NV_PAVS_VOICE_CFG_FMT_CLEAR_MIX;
    MCPXAPUVoiceFormat format = mcpx_apu_decode_voice_format(word);

    g_assert_false(format.stereo);
    g_assert_cmpuint(format.channels, ==, 1);
    g_assert_cmpuint(format.sample_size, ==, 0);
    g_assert_cmpuint(format.container_size_index, ==, 0);
    g_assert_false(format.stream);
    g_assert_false(format.loop);
    g_assert_cmpuint(format.samples_per_block, ==, 1);
    g_assert_false(format.persist);
    g_assert_false(format.multipass);
    g_assert_false(format.linked);
}

static void test_decodes_every_sample_field_combination(void)
{
    static const uint16_t unrelated_patterns[] = {
        0x0000, 0x0001, 0x001f, 0x0020, 0x03ff, 0x0400,
        0x1c00, 0x2000, 0x4000, 0x8000, 0xffff,
    };

    for (uint32_t upper = 0; upper <= UINT16_MAX; upper++) {
        for (unsigned i = 0; i < ARRAY_SIZE(unrelated_patterns); i++) {
            uint32_t word = (upper << 16) | unrelated_patterns[i];
            MCPXAPUVoiceFormat format = mcpx_apu_decode_voice_format(word);

            g_assert_cmpuint(format.stereo, ==, (word >> 27) & 1);
            g_assert_cmpuint(format.channels, ==, ((word >> 27) & 1) + 1);
            g_assert_cmpuint(format.sample_size, ==, (word >> 28) & 3);
            g_assert_cmpuint(format.container_size_index, ==, (word >> 30) & 3);
            g_assert_cmpuint(format.stream, ==, (word >> 24) & 1);
            g_assert_cmpuint(format.loop, ==, (word >> 25) & 1);
            g_assert_cmpuint(format.samples_per_block, ==,
                             ((word >> 16) & 0x1f) + 1);
            g_assert_cmpuint(format.persist, ==, (word >> 23) & 1);
            g_assert_cmpuint(format.multipass, ==, (word >> 21) & 1);
            g_assert_cmpuint(format.linked, ==, (word >> 22) & 1);
        }
    }
}

static void test_rejects_zero_field_mask(void)
{
    if (g_test_subprocess()) {
        mcpx_apu_voice_format_field(0, 0);
        return;
    }

    g_test_trap_subprocess(NULL, 0, 0);
    g_test_trap_assert_failed();
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/mcpx-apu/voice-format/zero", test_decodes_zero_format);
    g_test_add_func("/mcpx-apu/voice-format/sample-fields",
                    test_decodes_all_sample_fields);
    g_test_add_func("/mcpx-apu/voice-format/unrelated-fields",
                    test_ignores_unrelated_format_fields);
    g_test_add_func("/mcpx-apu/voice-format/exhaustive",
                    test_decodes_every_sample_field_combination);
    g_test_add_func("/mcpx-apu/voice-format/zero-mask",
                    test_rejects_zero_field_mask);

    return g_test_run();
}

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
    g_assert_cmpuint(format.container_size, ==,
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
    uint32_t word = NV_PAVS_VOICE_CFG_FMT_STEREO |
                    NV_PAVS_VOICE_CFG_FMT_DATA_TYPE |
                    NV_PAVS_VOICE_CFG_FMT_LOOP |
                    NV_PAVS_VOICE_CFG_FMT_PERSIST |
                    NV_PAVS_VOICE_CFG_FMT_MULTIPASS |
                    NV_PAVS_VOICE_CFG_FMT_LINKED |
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
    g_assert_cmpuint(format.container_size, ==,
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
    uint32_t word = NV_PAVS_VOICE_CFG_FMT_V6BIN |
                    NV_PAVS_VOICE_CFG_FMT_V7BIN |
                    NV_PAVS_VOICE_CFG_FMT_HEADROOM |
                    NV_PAVS_VOICE_CFG_FMT_CLEAR_MIX;
    MCPXAPUVoiceFormat format = mcpx_apu_decode_voice_format(word);

    g_assert_false(format.stereo);
    g_assert_cmpuint(format.channels, ==, 1);
    g_assert_cmpuint(format.sample_size, ==, 0);
    g_assert_cmpuint(format.container_size, ==, 0);
    g_assert_false(format.stream);
    g_assert_false(format.loop);
    g_assert_cmpuint(format.samples_per_block, ==, 1);
    g_assert_false(format.persist);
    g_assert_false(format.multipass);
    g_assert_false(format.linked);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/mcpx-apu/voice-format/zero", test_decodes_zero_format);
    g_test_add_func("/mcpx-apu/voice-format/sample-fields",
                    test_decodes_all_sample_fields);
    g_test_add_func("/mcpx-apu/voice-format/unrelated-fields",
                    test_ignores_unrelated_format_fields);

    return g_test_run();
}

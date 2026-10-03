/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "hw/xbox/mcpx/apu/apu_int.h"

#define TEST_MEMORY_SIZE (1U << 20)
#define TEST_VOICE_BASE 0x1000
#define TEST_SGE_BASE 0x2000
#define TEST_SSL_BASE 0x3000
#define TEST_DATA_BASE 0x4000

static uint8_t test_memory[TEST_MEMORY_SIZE];

AddressSpace address_space_memory;
MCPXAPUState *g_state;
struct McpxApuDebug g_dbg, g_dbg_cache;
int g_dbg_voice_monitor = -1;
uint64_t g_dbg_muted_voices[4];

static uint8_t reader_test_ldub_phys(AddressSpace *as, hwaddr addr)
{
    g_assert_cmpuint(addr, <, sizeof(test_memory));
    return test_memory[addr];
}

static uint16_t reader_test_lduw_le_phys(AddressSpace *as, hwaddr addr)
{
    g_assert_cmpuint(addr + sizeof(uint16_t), <=, sizeof(test_memory));
    return lduw_le_p(&test_memory[addr]);
}

static uint32_t reader_test_ldl_le_phys(AddressSpace *as, hwaddr addr)
{
    g_assert_cmpuint(addr + sizeof(uint32_t), <=, sizeof(test_memory));
    return ldl_le_p(&test_memory[addr]);
}

static void reader_test_stb_phys(AddressSpace *as, hwaddr addr, uint8_t value)
{
    g_assert_cmpuint(addr, <, sizeof(test_memory));
    test_memory[addr] = value;
}

static void reader_test_stl_le_phys(AddressSpace *as, hwaddr addr,
                                    uint32_t value)
{
    g_assert_cmpuint(addr + sizeof(uint32_t), <=, sizeof(test_memory));
    stl_le_p(&test_memory[addr], value);
}

#define ldub_phys reader_test_ldub_phys
#define lduw_le_phys reader_test_lduw_le_phys
#define ldl_le_phys reader_test_ldl_le_phys
#define stb_phys reader_test_stb_phys
#define stl_le_phys reader_test_stl_le_phys
#include "../../hw/xbox/mcpx/apu/vp/vp.c"
#undef ldub_phys
#undef lduw_le_phys
#undef ldl_le_phys
#undef stb_phys
#undef stl_le_phys

typedef struct ReaderFixture {
    MCPXAPUState d;
    float samples[NUM_SAMPLES_PER_FRAME][2];
} ReaderFixture;

static uint32_t voice_reg_load(hwaddr offset)
{
    return ldl_le_p(&test_memory[TEST_VOICE_BASE + offset]);
}

static void voice_reg_store(hwaddr offset, uint32_t value)
{
    stl_le_p(&test_memory[TEST_VOICE_BASE + offset], value);
}

static void voice_reg_set(hwaddr offset, uint32_t mask, uint32_t value)
{
    uint32_t reg = voice_reg_load(offset);

    reg &= ~mask;
    reg |= value << ctz32(mask) & mask;
    voice_reg_store(offset, reg);
}

static void init_adpcm_block(uint8_t *block, size_t size,
                             unsigned int channels)
{
    g_assert_cmpuint(size, >=, channels * 4);
    memset(block, 0, size);
    for (unsigned int channel = 0; channel < channels; channel++) {
        block[channel * 4] = 0x34 + channel;
        block[channel * 4 + 1] = 0x12;
        block[channel * 4 + 2] = 10 + channel;
    }
    for (size_t i = channels * 4; i < size; i++) {
        block[i] = i * 7 + 3;
    }
}

static void fixture_init(ReaderFixture *fixture, bool stream, bool stereo,
                         unsigned int samples_per_block, uint32_t cbo,
                         uint32_t ebo)
{
    memset(fixture, 0, sizeof(*fixture));
    memset(test_memory, 0, sizeof(test_memory));
    memset(&g_dbg, 0, sizeof(g_dbg));

    fixture->d.regs[NV_PAPU_VPVADDR] = TEST_VOICE_BASE;
    fixture->d.regs[NV_PAPU_VPSGEADDR] = TEST_SGE_BASE;
    fixture->d.regs[NV_PAPU_VPSSLADDR] = TEST_SSL_BASE;
    fixture->d.ram_ptr = test_memory;
    fixture->d.vp.filters[0].voice = 0;
    fixture->d.vp.filters[0].resampler_channels = stereo ? 2 : 1;

    uint32_t format =
        NV_PAVS_VOICE_CFG_FMT_CONTAINER_SIZE_ADPCM
        << ctz32(NV_PAVS_VOICE_CFG_FMT_CONTAINER_SIZE);
    format |= (samples_per_block - 1)
              << ctz32(NV_PAVS_VOICE_CFG_FMT_SAMPLES_PER_BLOCK);
    if (stereo) {
        format |= NV_PAVS_VOICE_CFG_FMT_STEREO;
    }
    if (stream) {
        format |= NV_PAVS_VOICE_CFG_FMT_DATA_TYPE |
                  NV_PAVS_VOICE_CFG_FMT_PERSIST;
    }
    voice_reg_store(NV_PAVS_VOICE_CFG_FMT, format);
    voice_reg_set(NV_PAVS_VOICE_PAR_OFFSET,
                  NV_PAVS_VOICE_PAR_OFFSET_CBO, cbo);
    voice_reg_set(NV_PAVS_VOICE_PAR_NEXT, NV_PAVS_VOICE_PAR_NEXT_EBO, ebo);
    voice_reg_set(NV_PAVS_VOICE_CUR_PSL_START,
                  NV_PAVS_VOICE_CUR_PSL_START_BA, 0);
    voice_reg_set(NV_PAVS_VOICE_PAR_STATE,
                  NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE, 1);

    stl_le_p(&test_memory[TEST_SGE_BASE], TEST_DATA_BASE);

    fixture->d.vp.ssl[0].count[0] = 1;
    stl_le_p(&test_memory[TEST_SSL_BASE], TEST_DATA_BASE);
    uint32_t segment_length = 64;
    segment_length |= NV_PAVS_VOICE_CFG_FMT_CONTAINER_SIZE_ADPCM << 16;
    segment_length |= (samples_per_block - 1) << 18;
    segment_length |= stereo ? 1U << 23 : 0;
    stl_le_p(&test_memory[TEST_SSL_BASE + 4], segment_length);
}

static uint32_t fixture_cbo(void)
{
    return voice_reg_load(NV_PAVS_VOICE_PAR_OFFSET) &
           NV_PAVS_VOICE_PAR_OFFSET_CBO;
}

static void test_rejects_oversized_sge_before_ingestion(void)
{
    ReaderFixture fixture;

    fixture_init(&fixture, false, false, 3, 0, 63);
    g_assert_cmpint(voice_get_samples(&fixture.d, 0, fixture.samples, 1), ==,
                    -1);
    g_assert_cmpuint(fixture_cbo(), ==, 0);
    g_assert_false(fixture.d.vp.filters[0].adpcm_cache.valid);
}

static void test_rejects_oversized_stream_before_ingestion(void)
{
    ReaderFixture fixture;

    fixture_init(&fixture, true, false, 32, 0, 63);
    g_assert_cmpint(voice_get_samples(&fixture.d, 0, fixture.samples, 1), ==,
                    -1);
    g_assert_cmpuint(fixture_cbo(), ==, 0);
    g_assert_false(fixture.d.vp.filters[0].adpcm_cache.valid);
}

static void test_valid_mono_and_stereo_controls(void)
{
    ReaderFixture fixture;

    fixture_init(&fixture, false, false, 1, 0, 63);
    init_adpcm_block(&test_memory[TEST_DATA_BASE], 36, 1);
    g_assert_cmpint(voice_get_samples(&fixture.d, 0, fixture.samples, 32), ==,
                    32);
    g_assert_cmpuint(fixture_cbo(), ==, 32);

    fixture_init(&fixture, false, true, 2, 0, 63);
    init_adpcm_block(&test_memory[TEST_DATA_BASE], 72, 2);
    g_assert_cmpint(voice_get_samples(&fixture.d, 0, fixture.samples, 32), ==,
                    32);
    g_assert_cmpuint(fixture_cbo(), ==, 32);
}

static void test_short_stereo_stops_before_primed_tail(void)
{
    ReaderFixture fixture;
    uint8_t long_block[72];
    int decoded_samples;

    fixture_init(&fixture, false, true, 1, 0, 63);
    init_adpcm_block(long_block, sizeof(long_block), 2);
    g_assert_nonnull(mcpx_apu_adpcm_decode_cached(
        &fixture.d.vp.filters[0].adpcm_cache, long_block, sizeof(long_block),
        2, &decoded_samples, NULL));
    g_assert_cmpint(decoded_samples, ==, 65);

    init_adpcm_block(&test_memory[TEST_DATA_BASE], 36, 2);
    g_assert_cmpint(voice_get_samples(&fixture.d, 0, fixture.samples, 32), ==,
                    25);
    g_assert_cmpuint(fixture_cbo(), ==, 25);
    g_assert_false(fixture.d.vp.filters[0].adpcm_cache.valid);

    g_assert_cmpint(voice_get_samples(&fixture.d, 0, fixture.samples, 1), ==,
                    -1);
    g_assert_cmpuint(fixture_cbo(), ==, 25);
}

static void test_callback_preserves_prefix_before_malformed_block(void)
{
    ReaderFixture fixture;
    float *data;

    fixture_init(&fixture, false, false, 1, 56, 127);
    init_adpcm_block(&test_memory[TEST_DATA_BASE], 36, 1);
    init_adpcm_block(&test_memory[TEST_DATA_BASE + 36], 36, 1);
    test_memory[TEST_DATA_BASE + 36 + 2] = 89;

    g_assert_cmpint(voice_resample_callback(&fixture.d.vp.filters[0], &data),
                    ==, NUM_SAMPLES_PER_FRAME);
    g_assert_true(data == fixture.d.vp.filters[0].mono_resample_buf);
    for (int i = 0; i < 8; i++) {
        g_assert_cmpfloat(data[i], !=, 0.0f);
    }
    for (int i = 8; i < NUM_SAMPLES_PER_FRAME; i++) {
        g_assert_cmpfloat(data[i], ==, 0.0f);
    }
    g_assert_cmpuint(fixture_cbo(), ==, 64);
    g_assert_false(fixture.d.vp.filters[0].adpcm_cache.valid);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/mcpx/apu/adpcm-reader/rejects-oversized-sge",
                    test_rejects_oversized_sge_before_ingestion);
    g_test_add_func("/mcpx/apu/adpcm-reader/rejects-oversized-stream",
                    test_rejects_oversized_stream_before_ingestion);
    g_test_add_func("/mcpx/apu/adpcm-reader/valid-controls",
                    test_valid_mono_and_stereo_controls);
    g_test_add_func("/mcpx/apu/adpcm-reader/short-stereo-prefix",
                    test_short_stereo_stops_before_primed_tail);
    g_test_add_func("/mcpx/apu/adpcm-reader/preserves-prefix",
                    test_callback_preserves_prefix_before_malformed_block);
    return g_test_run();
}

/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "system/address-spaces.h"
#include "system/memory.h"

/* Deterministic physical-memory boundary; the production block reader runs
 * unchanged. This does not qualify QEMU's mapping lifetime or real MMIO. */
AddressSpace address_space_memory;
static uint8_t ram[0x10000];
static uint32_t table_base;
static bool switch_table_on_map;
static bool switch_descriptor_on_map;
static hwaddr map_limit;
static bool non_ram;
static bool fail_map;
static bool high_descriptor;
static unsigned live_maps;
static unsigned physical_words;
static unsigned cached_words;

static uint32_t test_phys_load(AddressSpace *as, hwaddr addr)
{
    g_assert_true(as == &address_space_memory);
    if (high_descriptor && addr == 0x100000000ULL) {
        return 0x4000;
    }
    g_assert_cmpuint(addr + 4, <=, sizeof(ram));
    if (addr >= 0x4000) {
        physical_words++;
    }
    return ldl_le_p(ram + addr);
}

static int64_t test_cache_init(MemoryRegionCache *cache, AddressSpace *as,
                               hwaddr addr, hwaddr len, bool is_write)
{
    g_assert_true(as == &address_space_memory);
    g_assert_false(is_write);
    g_assert_cmpuint(addr + len, <=, sizeof(ram));
    if (fail_map) {
        return -EFAULT;
    }
    cache->ptr = non_ram ? NULL : ram + addr;
    cache->len = MIN(len, map_limit);
    live_maps++;
    if (switch_table_on_map) {
        table_base = 0x1800;
        switch_table_on_map = false;
    }
    if (switch_descriptor_on_map) {
        stl_le_p(ram + table_base, 0x6000);
        switch_descriptor_on_map = false;
    }
    return cache->len;
}

static void test_cache_destroy(MemoryRegionCache *cache)
{
    if (cache->len) {
        g_assert_cmpuint(live_maps, >, 0);
        live_maps--;
    }
}

static uint32_t test_cached_load(MemoryRegionCache *cache, hwaddr offset,
                                 MemTxAttrs attrs, MemTxResult *result)
{
    g_assert_cmpuint(offset + 4, <=, cache->len);
    cached_words++;
    return ldl_le_p(cache->ptr + offset);
}

#define ldl_le_phys test_phys_load
#define address_space_cache_init test_cache_init
#define address_space_cache_destroy test_cache_destroy
#define address_space_ldl_le_cached test_cached_load

#include "hw/xbox/mcpx/apu/vp/sample-memory.h"

static void reset_memory(void)
{
    g_assert_cmpuint(live_maps, ==, 0);
    memset(ram, 0, sizeof(ram));
    table_base = 0x1000;
    switch_table_on_map = false;
    switch_descriptor_on_map = false;
    map_limit = sizeof(ram);
    non_ram = false;
    fail_map = false;
    high_descriptor = false;
    physical_words = cached_words = 0;
    stl_le_p(ram + 0x1000, 0x4000);
    stl_le_p(ram + 0x1008, 0x8000);
    stl_le_p(ram + 0x1800, 0x6000);
}

static void test_table_base_changes_between_words(void)
{
    uint32_t words[3];

    reset_memory();
    switch_table_on_map = true;
    stl_le_p(ram + 0x1000, 0x4000);
    stl_le_p(ram + 0x1800, 0x6000);
    stl_le_p(ram + 0x4000, 0x11223344);
    stl_le_p(ram + 0x4004, 0xdeadbeef);
    stl_le_p(ram + 0x4008, 0xbad0cafe);
    stl_le_p(ram + 0x6004, 0x55667788);
    stl_le_p(ram + 0x6008, 0x99aabbcc);

    mcpx_apu_read_adpcm_block(&table_base, 0, words, G_N_ELEMENTS(words));

    /* Word zero was translated before the table-base update. */
    g_assert_cmphex(words[0], ==, 0x11223344);
    g_assert_cmphex(words[1], ==, 0x55667788);
    g_assert_cmphex(words[2], ==, 0x99aabbcc);
    g_assert_cmpuint(live_maps, ==, 0);
}

static void test_descriptor_changes_between_words(void)
{
    uint32_t words[3];

    reset_memory();
    switch_descriptor_on_map = true;
    stl_le_p(ram + 0x4000, 0x11223344);
    stl_le_p(ram + 0x4004, 0xdeadbeef);
    stl_le_p(ram + 0x6004, 0x55667788);
    stl_le_p(ram + 0x6008, 0x99aabbcc);
    mcpx_apu_read_adpcm_block(&table_base, 0, words, G_N_ELEMENTS(words));
    g_assert_cmphex(words[0], ==, 0x11223344);
    g_assert_cmphex(words[1], ==, 0x55667788);
    g_assert_cmphex(words[2], ==, 0x99aabbcc);
    g_assert_cmpuint(live_maps, ==, 0);
}

static void test_noncontiguous_sge_pages(void)
{
    uint32_t words[4];

    reset_memory();
    stl_le_p(ram + 0x4ff8, 0x11223344);
    stl_le_p(ram + 0x4ffc, 0x55667788);
    stl_le_p(ram + 0x5000, 0xdeadbeef);
    stl_le_p(ram + 0x8000, 0x99aabbcc);
    stl_le_p(ram + 0x8004, 0xddeeff00);
    mcpx_apu_read_adpcm_block(&table_base, 0xff8, words, G_N_ELEMENTS(words));
    g_assert_cmphex(words[0], ==, 0x11223344);
    g_assert_cmphex(words[1], ==, 0x55667788);
    g_assert_cmphex(words[2], ==, 0x99aabbcc);
    g_assert_cmphex(words[3], ==, 0xddeeff00);
    g_assert_cmpuint(live_maps, ==, 0);
}

static void test_unaligned_word_crosses_sge_page(void)
{
    uint32_t words[3];

    reset_memory();
    /* A physical load straddles its physical page; SGE is consulted again
     * for the next word, matching the original per-word reader. */
    stl_le_p(ram + 0x4ffe, 0x04030201);
    stl_le_p(ram + 0x8002, 0x08070605);
    stl_le_p(ram + 0x8006, 0x0c0b0a09);
    mcpx_apu_read_adpcm_block(&table_base, 0xffe, words, G_N_ELEMENTS(words));
    g_assert_cmphex(words[0], ==, 0x04030201);
    g_assert_cmphex(words[1], ==, 0x08070605);
    g_assert_cmphex(words[2], ==, 0x0c0b0a09);
    g_assert_cmpuint(physical_words, ==, 1);
    g_assert_cmpuint(live_maps, ==, 0);
}

static void test_short_or_unavailable_mapping(void)
{
    const struct {
        hwaddr limit;
        bool non_ram;
        bool fail;
        unsigned cached;
    } cases[] = {
        { 3, false, false, 0 },
        { 7, false, false, 1 },
        { 12, true, false, 0 },
        { 12, false, true, 0 },
    };

    for (unsigned i = 0; i < G_N_ELEMENTS(cases); i++) {
        uint32_t words[3];
        reset_memory();
        map_limit = cases[i].limit;
        non_ram = cases[i].non_ram;
        fail_map = cases[i].fail;
        stl_le_p(ram + 0x4000, 0x11223344);
        stl_le_p(ram + 0x4004, 0x55667788);
        stl_le_p(ram + 0x4008, 0x99aabbcc);
        mcpx_apu_read_adpcm_block(&table_base, 0, words, G_N_ELEMENTS(words));
        g_assert_cmphex(words[0], ==, 0x11223344);
        g_assert_cmphex(words[1], ==, 0x55667788);
        g_assert_cmphex(words[2], ==, 0x99aabbcc);
        g_assert_cmpuint(cached_words, ==, cases[i].cached);
        g_assert_cmpuint(physical_words, ==, 3 - cases[i].cached);
        g_assert_cmpuint(live_maps, ==, 0);
    }
}

static void test_descriptor_address_does_not_wrap(void)
{
    reset_memory();
    high_descriptor = true;
    g_assert_cmphex(mcpx_apu_adpcm_word_address(0xfffffff8, 0x1000), ==,
                    0x4000);
}

static void test_mono_and_stereo_encoded_blocks(void)
{
    const uint32_t expected[] = {
        0x03020100, 0x07060504, 0x0b0a0908, 0x0f0e0d0c, 0x13121110, 0x17161514,
        0x1b1a1918, 0x1f1e1d1c, 0x23222120, 0x27262524, 0x2b2a2928, 0x2f2e2d2c,
        0x33323130, 0x37363534, 0x3b3a3938, 0x3f3e3d3c, 0x43424140, 0x47464544,
    };

    for (unsigned channels = 1; channels <= 2; channels++) {
        uint32_t words[18];
        unsigned word_count = 9 * channels;
        reset_memory();
        for (unsigned i = 0; i < 72; i++) {
            ram[0x4000 + i] = i;
        }
        mcpx_apu_read_adpcm_block(&table_base, 0, words, word_count);
        g_assert_cmpmem(words, word_count * sizeof(*words), expected,
                        word_count * sizeof(*expected));
        g_assert_cmpuint(cached_words, ==, word_count);
        g_assert_cmpuint(physical_words, ==, 0);
        g_assert_cmpuint(live_maps, ==, 0);
    }
}

static void test_sge_page_chunk(void)
{
    g_assert_cmpuint(mcpx_apu_adpcm_chunk_bytes(0x1000, 36, 4096), ==, 36);
    g_assert_cmpuint(mcpx_apu_adpcm_chunk_bytes(0x1ffe, 36, 4096), ==, 2);
    g_assert_cmpuint(mcpx_apu_adpcm_chunk_bytes(0x2002, 32, 4096), ==, 32);
}

static void test_cached_word_eligibility(void)
{
    /* A short mapping cannot safely serve a four-byte physical load. */
    g_assert_false(mcpx_apu_cached_word_eligible(0x5000, 0x5000, 0, 3));
    g_assert_true(mcpx_apu_cached_word_eligible(0x5000, 0x5004, 4, 8));

    /* Re-reading a changed SGE descriptor must force the physical fallback. */
    g_assert_false(mcpx_apu_cached_word_eligible(0x5000, 0x9004, 4, 8));
    g_assert_false(mcpx_apu_cached_word_eligible(0x5000, 0x5004, 4, 7));

    /* Never accept a wrapped physical address as a contiguous cache hit. */
    g_assert_false(mcpx_apu_cached_word_eligible(UINT64_MAX - 1, 2, 4, 8));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/mcpx-apu/sample-memory/sge-page-chunk",
                    test_sge_page_chunk);
    g_test_add_func("/mcpx-apu/sample-memory/cached-word-eligibility",
                    test_cached_word_eligibility);
    g_test_add_func("/mcpx-apu/sample-memory/table-base-changes-between-words",
                    test_table_base_changes_between_words);
    g_test_add_func("/mcpx-apu/sample-memory/descriptor-changes-between-words",
                    test_descriptor_changes_between_words);
    g_test_add_func("/mcpx-apu/sample-memory/noncontiguous-sge-pages",
                    test_noncontiguous_sge_pages);
    g_test_add_func("/mcpx-apu/sample-memory/unaligned-word-crosses-sge-page",
                    test_unaligned_word_crosses_sge_page);
    g_test_add_func("/mcpx-apu/sample-memory/short-or-unavailable-mapping",
                    test_short_or_unavailable_mapping);
    g_test_add_func("/mcpx-apu/sample-memory/descriptor-address-does-not-wrap",
                    test_descriptor_address_does_not_wrap);
    g_test_add_func("/mcpx-apu/sample-memory/mono-and-stereo-encoded-blocks",
                    test_mono_and_stereo_encoded_blocks);
    return g_test_run();
}

#include "qemu/osdep.h"

#include "hw/xbox/mcpx/apu/vp/sge.h"

#define TEST_PAGE_SIZE 4096

typedef struct TestSGEReader {
    MCPXAPUSGETranslationCache cache;
    hwaddr page_bases[4];
    unsigned int reads;
} TestSGEReader;

static hwaddr test_translate(TestSGEReader *reader, uint32_t address)
{
    hwaddr physical;

    if (!mcpx_apu_sge_cache_translate(&reader->cache, address, TEST_PAGE_SIZE,
                                      &physical)) {
        unsigned int entry = address / TEST_PAGE_SIZE;

        reader->reads++;
        physical = mcpx_apu_sge_cache_fill(&reader->cache, address,
                                           TEST_PAGE_SIZE,
                                           reader->page_bases[entry]);
    }

    return physical;
}

static void test_reuses_same_page_translation(void)
{
    TestSGEReader reader = {
        .page_bases = { 0x10000, 0x20000, 0x30000, 0x40000 },
    };

    g_assert_cmphex(test_translate(&reader, 0x120), ==, 0x10120);
    g_assert_cmphex(test_translate(&reader, 0x124), ==, 0x10124);
    g_assert_cmphex(test_translate(&reader, 0xffc), ==, 0x10ffc);
    g_assert_cmpuint(reader.reads, ==, 1);
}

static void test_reloads_at_page_boundary(void)
{
    TestSGEReader reader = {
        .page_bases = { 0x10000, 0x28000, 0x30000, 0x40000 },
    };

    g_assert_cmphex(test_translate(&reader, 0xffc), ==, 0x10ffc);
    g_assert_cmphex(test_translate(&reader, 0x1000), ==, 0x28000);
    g_assert_cmphex(test_translate(&reader, 0x1004), ==, 0x28004);
    g_assert_cmpuint(reader.reads, ==, 2);
}

static void test_reloads_after_non_adjacent_page(void)
{
    TestSGEReader reader = {
        .page_bases = { 0x10000, 0x20000, 0x34000, 0x40000 },
    };

    g_assert_cmphex(test_translate(&reader, 0x80), ==, 0x10080);
    g_assert_cmphex(test_translate(&reader, 0x2080), ==, 0x34080);
    g_assert_cmphex(test_translate(&reader, 0x84), ==, 0x10084);
    g_assert_cmpuint(reader.reads, ==, 3);
}

static void test_reloads_after_sge_base_change(void)
{
    MCPXAPUSGETranslationCache cache = { 0 };
    hwaddr physical;

    g_assert_cmphex(mcpx_apu_sge_cache_fill(&cache, 0x1000, 0x120,
                                            TEST_PAGE_SIZE, 0x8000),
                    ==, 0x8120);
    g_assert_true(mcpx_apu_sge_cache_translate(
        &cache, 0x1000, 0x124, TEST_PAGE_SIZE, &physical));
    g_assert_cmphex(physical, ==, 0x8124);
    g_assert_false(mcpx_apu_sge_cache_translate(
        &cache, 0x2000, 0x124, TEST_PAGE_SIZE, &physical));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/mcpx-apu/sge-cache/reuses-same-page",
                    test_reuses_same_page_translation);
    g_test_add_func("/mcpx-apu/sge-cache/reloads-page-boundary",
                    test_reloads_at_page_boundary);
    g_test_add_func("/mcpx-apu/sge-cache/reloads-non-adjacent-page",
                    test_reloads_after_non_adjacent_page);
    g_test_add_func("/mcpx-apu/sge-cache/reloads-sge-base-change",
                    test_reloads_after_sge_base_change);

    return g_test_run();
}

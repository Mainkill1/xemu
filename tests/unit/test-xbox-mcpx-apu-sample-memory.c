/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <glib.h>

#include "hw/xbox/mcpx/apu/vp/sample-memory.h"

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
    return g_test_run();
}

/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "hw/xbox/mcpx/apu/vp/voice-store.h"

static void test_equal_store_only_elided_in_plain_ram(void)
{
    const uint64_t ram_size = 128ULL * 1024 * 1024;
    const uint32_t word = 0x12345678;

    g_assert_false(mcpx_apu_voice_store_required(0x200000, ram_size,
                                                  word, word));
    g_assert_true(mcpx_apu_voice_store_required(0x200000, ram_size,
                                                 word, word ^ 1));

    /* Lower memory can contain ROM or device overlays. */
    g_assert_true(mcpx_apu_voice_store_required(0x000a0000, ram_size,
                                                 word, word));
    g_assert_true(mcpx_apu_voice_store_required(0x000fffff, ram_size,
                                                 word, word));
    g_assert_false(mcpx_apu_voice_store_required(0x00100000, ram_size,
                                                  word, word));

    /* A complete word must fit in the configured physical RAM region. */
    g_assert_false(mcpx_apu_voice_store_required(ram_size - 4, ram_size,
                                                  word, word));
    g_assert_true(mcpx_apu_voice_store_required(ram_size - 3, ram_size,
                                                 word, word));
    g_assert_true(mcpx_apu_voice_store_required(ram_size, ram_size,
                                                 word, word));
    g_assert_true(mcpx_apu_voice_store_required(UINT64_MAX, ram_size,
                                                 word, word));
    g_assert_true(mcpx_apu_voice_store_required(0x200000, 0, word, word));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/apu/voice-store/plain-ram",
                    test_equal_store_only_elided_in_plain_ram);
    return g_test_run();
}

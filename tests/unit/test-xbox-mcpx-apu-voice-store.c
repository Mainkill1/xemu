/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qemu/bswap.h"
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

static void test_guest_write_before_equal_store_validation(void)
{
    uint32_t ram_word = cpu_to_le32(0x12345678);
    uint32_t apu_read = le32_to_cpu(ram_word);

    /* The guest changes the word after the APU's original read. */
    ram_word = cpu_to_le32(0xaabbccdd);

    g_assert_false(mcpx_apu_voice_try_equal_store(&ram_word, apu_read));
    g_assert_cmphex(le32_to_cpu(ram_word), ==, 0xaabbccdd);
}

static void test_equal_store_validation_linearizes(void)
{
    uint32_t ram_word = cpu_to_le32(0x12345678);

    g_assert_true(mcpx_apu_voice_try_equal_store(&ram_word, 0x12345678));
    g_assert_cmphex(le32_to_cpu(ram_word), ==, 0x12345678);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/apu/voice-store/plain-ram",
                    test_equal_store_only_elided_in_plain_ram);
    g_test_add_func("/xbox/apu/voice-store/guest-write-before-validation",
                    test_guest_write_before_equal_store_validation);
    g_test_add_func("/xbox/apu/voice-store/equal-validation-linearizes",
                    test_equal_store_validation_linearizes);
    return g_test_run();
}

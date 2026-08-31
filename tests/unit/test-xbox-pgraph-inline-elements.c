/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"

#include "hw/xbox/nv2a/pgraph/inline-elements.h"

static void test_capacity_boundaries(void)
{
    g_assert_cmpuint(pgraph_inline_packet_words_available(0, 2, 8), ==, 4);
    g_assert_cmpuint(pgraph_inline_packet_words_available(2, 2, 8), ==, 3);
    g_assert_cmpuint(pgraph_inline_packet_words_available(7, 2, 8), ==, 0);
    g_assert_cmpuint(pgraph_inline_packet_words_available(8, 1, 8), ==, 0);
    g_assert_cmpuint(pgraph_inline_packet_words_available(9, 1, 8), ==, 0);
    g_assert_cmpuint(pgraph_inline_packet_words_available(0, 0, 8), ==, 0);
    g_assert_cmpuint(pgraph_inline_packet_words_available(0, 2, SIZE_MAX),
                     ==, SIZE_MAX / 2);

    g_assert_true(pgraph_inline_packet_fits(2, 3, 2, 8));
    g_assert_false(pgraph_inline_packet_fits(1, 4, 2, 8));
    g_assert_false(pgraph_inline_packet_fits(7, SIZE_MAX, 2, 8));
}

static void test_element16_store(void)
{
    uint32_t destination[2] = { 0 };

    pgraph_inline_element16_store(destination, 0xaabbccdd);
    g_assert_cmphex(destination[0], ==, 0xccdd);
    g_assert_cmphex(destination[1], ==, 0xaabb);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/pgraph/inline-elements/capacity-boundaries",
                    test_capacity_boundaries);
    g_test_add_func("/xbox/pgraph/inline-elements/element16-store",
                    test_element16_store);
    return g_test_run();
}

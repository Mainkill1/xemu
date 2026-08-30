/*
 * NV2A inline element helper tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/inline-elements.h"

static void test_packet_mode(void)
{
    g_assert_cmpint(pgraph_inline_packet_mode(false, false), ==,
                    PGRAPH_INLINE_PACKET_BULK);
    g_assert_cmpint(pgraph_inline_packet_mode(false, true), ==,
                    PGRAPH_INLINE_PACKET_SCALAR_TRACE);
    g_assert_cmpint(pgraph_inline_packet_mode(true, false), ==,
                    PGRAPH_INLINE_PACKET_SCALAR_INCREMENTING);
    g_assert_cmpint(pgraph_inline_packet_mode(true, true), ==,
                    PGRAPH_INLINE_PACKET_SCALAR_INCREMENTING);
}

static void test_capacity_boundaries(void)
{
    g_assert_true(pgraph_inline_packet_fits(0, 4, 2, 8));
    g_assert_true(pgraph_inline_packet_fits(2, 3, 2, 8));
    g_assert_true(pgraph_inline_packet_fits(8, 0, 1, 8));

    g_assert_false(pgraph_inline_packet_fits(1, 4, 2, 8));
    g_assert_false(pgraph_inline_packet_fits(8, 1, 1, 8));
    g_assert_false(pgraph_inline_packet_fits(9, 0, 1, 8));
    g_assert_false(pgraph_inline_packet_fits(0, 1, 0, 8));
}

static void test_capacity_overflow_guard(void)
{
    g_assert_false(pgraph_inline_packet_fits(7, SIZE_MAX, 2, 8));
    g_assert_false(pgraph_inline_packet_fits(SIZE_MAX, 0, 1, 8));
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
    g_test_add_func("/xbox/pgraph/inline-elements/packet-mode",
                    test_packet_mode);
    g_test_add_func("/xbox/pgraph/inline-elements/capacity-boundaries",
                    test_capacity_boundaries);
    g_test_add_func("/xbox/pgraph/inline-elements/capacity-overflow-guard",
                    test_capacity_overflow_guard);
    g_test_add_func("/xbox/pgraph/inline-elements/element16-store",
                    test_element16_store);

    return g_test_run();
}

/*
 * NV2A fixed inline-array capacity tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hw/xbox/nv2a/pgraph/inline-capacity.h"

#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,       \
                    __LINE__, #condition);                                 \
            abort();                                                       \
        }                                                                  \
    } while (0)

static void test_capacity_exact_fit(void)
{
    CHECK(pgraph_inline_has_capacity(3, 2, 5));
    CHECK(pgraph_inline_has_capacity(5, 0, 5));
    CHECK(!pgraph_inline_has_capacity(5, 1, 5));
}

static void test_capacity_arithmetic_does_not_wrap(void)
{
    CHECK(!pgraph_inline_has_capacity(1, SIZE_MAX, 8));
    CHECK(!pgraph_inline_has_capacity(SIZE_MAX, 1, 8));
    CHECK(pgraph_inline_has_capacity(0, SIZE_MAX, SIZE_MAX));
}

static void test_element16_exact_fit(void)
{
    uint32_t destination[4] = { 0, 0, 0xaaaaaaaa, 0xbbbbbbbb };
    unsigned int length = 2;

    CHECK(pgraph_inline_append_element16(destination, &length, 4,
                                         0x12345678));
    CHECK(length == 4);
    CHECK(destination[2] == 0x5678);
    CHECK(destination[3] == 0x1234);
}

static void test_element16_capacity_plus_one_is_atomic(void)
{
    uint32_t destination[4] = {
        0x11111111, 0x22222222, 0xaaaaaaaa, 0xbbbbbbbb,
    };
    const uint32_t expected[4] = {
        0x11111111, 0x22222222, 0xaaaaaaaa, 0xbbbbbbbb,
    };
    unsigned int length = 3;

    CHECK(!pgraph_inline_append_element16(destination, &length, 4,
                                          0x12345678));
    CHECK(length == 3);
    CHECK(memcmp(destination, expected, sizeof(expected)) == 0);
}

static void test_element32_full_is_atomic(void)
{
    uint32_t destination[2] = { 0xaaaaaaaa, 0xbbbbbbbb };
    const uint32_t expected[2] = { 0xaaaaaaaa, 0xbbbbbbbb };
    unsigned int length = 2;

    CHECK(!pgraph_inline_append_element32(destination, &length, 2,
                                          0x12345678));
    CHECK(length == 2);
    CHECK(memcmp(destination, expected, sizeof(expected)) == 0);
}

static void test_sequence_exact_fit(void)
{
    uint32_t destination[5] = { 0x11, 0x22, 0, 0, 0 };
    unsigned int length = 2;

    CHECK(pgraph_inline_append_sequence(destination, &length, 5, 7, 3));
    CHECK(length == 5);
    CHECK(destination[2] == 7);
    CHECK(destination[3] == 8);
    CHECK(destination[4] == 9);
}

static void test_sequence_capacity_plus_one_is_atomic(void)
{
    uint32_t destination[4] = {
        0x11111111, 0x22222222, 0xaaaaaaaa, 0xbbbbbbbb,
    };
    const uint32_t expected[4] = {
        0x11111111, 0x22222222, 0xaaaaaaaa, 0xbbbbbbbb,
    };
    unsigned int length = 2;

    CHECK(!pgraph_inline_append_sequence(destination, &length, 4, 7, 3));
    CHECK(length == 2);
    CHECK(memcmp(destination, expected, sizeof(expected)) == 0);
}

static void test_sequence_value_overflow_is_atomic(void)
{
    uint32_t destination[2] = { 0xaaaaaaaa, 0xbbbbbbbb };
    const uint32_t expected[2] = { 0xaaaaaaaa, 0xbbbbbbbb };
    unsigned int length = 0;

    CHECK(!pgraph_inline_append_sequence(destination, &length, 2,
                                         UINT32_MAX, 2));
    CHECK(length == 0);
    CHECK(memcmp(destination, expected, sizeof(expected)) == 0);
}

static void test_checked_range_end(void)
{
    uint32_t result = 0;

    CHECK(pgraph_u32_add_checked(UINT32_MAX - 1, 1, &result));
    CHECK(result == UINT32_MAX);
    CHECK(!pgraph_u32_add_checked(UINT32_MAX, 1, &result));
}

int main(void)
{
    test_capacity_exact_fit();
    test_capacity_arithmetic_does_not_wrap();
    test_element16_exact_fit();
    test_element16_capacity_plus_one_is_atomic();
    test_element32_full_is_atomic();
    test_sequence_exact_fit();
    test_sequence_capacity_plus_one_is_atomic();
    test_sequence_value_overflow_is_atomic();
    test_checked_range_end();
    return EXIT_SUCCESS;
}

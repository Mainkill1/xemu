/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "hw/xbox/nv2a/pgraph/vk/buffer-size.h"

#define TEST_MIB (UINT64_C(1024) * 1024)

static bool test_checked_arithmetic(void)
{
    uint64_t result;

    return pgraph_vk_size_add(64, 32, &result) && result == 96 &&
           !pgraph_vk_size_add(UINT64_MAX, 1, &result) &&
           pgraph_vk_size_mul3(4096, 4096, 4, &result) &&
           result == 64 * TEST_MIB &&
           !pgraph_vk_size_mul3(UINT64_MAX, 2, 1, &result);
}

static bool test_checked_alignment(void)
{
    uint64_t result;

    return pgraph_vk_size_align_up(65, 64, &result) && result == 128 &&
           pgraph_vk_size_align_up(128, 64, &result) && result == 128 &&
           !pgraph_vk_size_align_up(1, 0, &result) &&
           !pgraph_vk_size_align_up(UINT64_MAX - 1, 4, &result);
}

static bool test_geometric_growth(void)
{
    uint64_t result;

    return pgraph_vk_size_grow_geometric(0, 64 * TEST_MIB, 64 * TEST_MIB,
                                         &result) &&
           result == 64 * TEST_MIB &&
           pgraph_vk_size_grow_geometric(64 * TEST_MIB, 64 * TEST_MIB + 1,
                                         64 * TEST_MIB, &result) &&
           result == 128 * TEST_MIB &&
           pgraph_vk_size_grow_geometric(UINT64_C(1) << 62,
                                         UINT64_C(1) << 63, 1, &result) &&
           result == (UINT64_C(1) << 63) &&
           !pgraph_vk_size_grow_geometric(UINT64_C(1) << 63,
                                          (UINT64_C(1) << 63) + 1, 1,
                                          &result) &&
           !pgraph_vk_size_grow_geometric(1, 0, 1, &result);
}

int main(void)
{
    bool arithmetic = test_checked_arithmetic();
    bool alignment = test_checked_alignment();
    bool growth = test_geometric_growth();

    puts("TAP version 13");
    puts("1..3");
    printf("%s 1 - checked add and multiply\n", arithmetic ? "ok" : "not ok");
    printf("%s 2 - checked alignment\n", alignment ? "ok" : "not ok");
    printf("%s 3 - bounded geometric growth\n", growth ? "ok" : "not ok");

    return arithmetic && alignment && growth ? 0 : 1;
}

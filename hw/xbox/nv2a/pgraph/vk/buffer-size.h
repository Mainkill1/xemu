/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 *
 * Checked size arithmetic shared by Vulkan scratch-buffer users.
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_BUFFER_SIZE_H
#define HW_XBOX_NV2A_PGRAPH_VK_BUFFER_SIZE_H

#include <stdbool.h>
#include <stdint.h>

static inline bool pgraph_vk_size_add(uint64_t lhs, uint64_t rhs,
                                      uint64_t *result)
{
    return !__builtin_add_overflow(lhs, rhs, result);
}

static inline bool pgraph_vk_size_mul(uint64_t lhs, uint64_t rhs,
                                      uint64_t *result)
{
    return !__builtin_mul_overflow(lhs, rhs, result);
}

static inline bool pgraph_vk_size_mul3(uint64_t first, uint64_t second,
                                       uint64_t third, uint64_t *result)
{
    uint64_t product;

    return pgraph_vk_size_mul(first, second, &product) &&
           pgraph_vk_size_mul(product, third, result);
}

static inline bool pgraph_vk_size_align_up(uint64_t value, uint64_t alignment,
                                           uint64_t *result)
{
    uint64_t remainder;

    if (alignment == 0) {
        return false;
    }

    remainder = value % alignment;
    if (remainder == 0) {
        *result = value;
        return true;
    }

    return pgraph_vk_size_add(value, alignment - remainder, result);
}

static inline bool pgraph_vk_size_grow_geometric(uint64_t current,
                                                  uint64_t required,
                                                  uint64_t minimum,
                                                  uint64_t *result)
{
    uint64_t size = current > minimum ? current : minimum;

    if (required == 0 || size == 0) {
        return false;
    }

    while (size < required) {
        if (size > UINT64_MAX / 2) {
            return false;
        }
        size *= 2;
    }

    *result = size;
    return true;
}

#endif

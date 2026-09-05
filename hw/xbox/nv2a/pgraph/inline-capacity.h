/*
 * NV2A fixed inline-array capacity helpers
 *
 * Copyright (c) 2026 xemu project contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef HW_XBOX_NV2A_PGRAPH_INLINE_CAPACITY_H
#define HW_XBOX_NV2A_PGRAPH_INLINE_CAPACITY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Subtraction-based capacity check. This remains well-defined even when the
 * guest-controlled append count is SIZE_MAX.
 */
static inline bool pgraph_inline_has_capacity(size_t current_length,
                                               size_t append_count,
                                               size_t capacity)
{
    return current_length <= capacity &&
           append_count <= capacity - current_length;
}

static inline bool pgraph_u32_add_checked(uint32_t first, uint32_t second,
                                          uint32_t *result)
{
    if (second > UINT32_MAX - first) {
        return false;
    }

    *result = first + second;
    return true;
}

static inline bool pgraph_inline_append_element16(uint32_t *destination,
                                                   unsigned int *length,
                                                   size_t capacity,
                                                   uint32_t parameter)
{
    size_t current_length = *length;

    if (!pgraph_inline_has_capacity(current_length, 2, capacity)) {
        return false;
    }

    destination[current_length] = parameter & 0xffff;
    destination[current_length + 1] = parameter >> 16;
    *length = current_length + 2;
    return true;
}

static inline bool pgraph_inline_append_element32(uint32_t *destination,
                                                   unsigned int *length,
                                                   size_t capacity,
                                                   uint32_t parameter)
{
    size_t current_length = *length;

    if (!pgraph_inline_has_capacity(current_length, 1, capacity)) {
        return false;
    }

    destination[current_length] = parameter;
    *length = current_length + 1;
    return true;
}

static inline bool pgraph_inline_append_sequence(uint32_t *destination,
                                                  unsigned int *length,
                                                  size_t capacity,
                                                  uint32_t start,
                                                  uint32_t count)
{
    size_t current_length = *length;
    uint32_t end;

    if (!pgraph_inline_has_capacity(current_length, count, capacity) ||
        !pgraph_u32_add_checked(start, count, &end)) {
        return false;
    }

    for (uint32_t value = start; value < end; value++) {
        destination[current_length++] = value;
    }
    *length = current_length;
    return true;
}

#endif

/* SPDX-License-Identifier: LGPL-2.0-or-later */
#ifndef HW_XBOX_NV2A_PGRAPH_VK_BUFFER_GROWTH_H
#define HW_XBOX_NV2A_PGRAPH_VK_BUFFER_GROWTH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static inline bool pgraph_vk_buffer_required_size_checked(
    uint64_t offset, uint64_t size, uint64_t alignment, uint64_t *required)
{
    if (!alignment) {
        return false;
    }

    uint64_t remainder = offset % alignment;
    uint64_t padding = remainder ? alignment - remainder : 0;
    if (offset > UINT64_MAX - padding) {
        return false;
    }
    offset += padding;
    if (size > UINT64_MAX - offset) {
        return false;
    }
    *required = offset + size;
    return true;
}

static inline size_t pgraph_vk_buffer_growth_target(
    size_t current_size, size_t minimum_size, size_t required_size)
{
    size_t target = current_size > minimum_size ? current_size : minimum_size;

    if (!target && required_size) {
        target = 1;
    }
    while (target < required_size) {
        if (target > SIZE_MAX / 2) {
            return required_size;
        }
        target *= 2;
    }
    return target;
}

#endif

/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 *
 * Pure buffer layout helpers shared by the Vulkan renderer and unit tests.
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_BUFFER_LAYOUT_H
#define HW_XBOX_NV2A_PGRAPH_VK_BUFFER_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static inline bool pgraph_vk_buffer_checked_add(uint64_t a, uint64_t b,
                                                uint64_t *result)
{
    if (a > UINT64_MAX - b) {
        return false;
    }
    *result = a + b;
    return true;
}

static inline bool pgraph_vk_buffer_checked_mul(uint64_t a, uint64_t b,
                                                uint64_t *result)
{
    if (a && b > UINT64_MAX / a) {
        return false;
    }
    *result = a * b;
    return true;
}

static inline bool pgraph_vk_buffer_checked_align_up(uint64_t value,
                                                     uint64_t alignment,
                                                     uint64_t *result)
{
    if (!alignment) {
        return false;
    }

    uint64_t remainder = value % alignment;
    uint64_t padding = remainder ? alignment - remainder : 0;
    return pgraph_vk_buffer_checked_add(value, padding, result);
}

static inline bool pgraph_vk_buffer_image_size(uint64_t width,
                                               uint64_t height,
                                               uint64_t bytes_per_pixel,
                                               uint64_t *result)
{
    uint64_t pixels;

    return pgraph_vk_buffer_checked_mul(width, height, &pixels) &&
           pgraph_vk_buffer_checked_mul(pixels, bytes_per_pixel, result);
}

static inline bool pgraph_vk_buffer_layout_required_size(
    uint64_t offset, const uint64_t *sizes, size_t count, uint64_t alignment,
    uint64_t *required_size)
{
    if (!alignment) {
        return false;
    }

    for (size_t i = 0; i < count; i++) {
        uint64_t remainder = offset % alignment;
        uint64_t padding = remainder ? alignment - remainder : 0;

        if (offset > UINT64_MAX - padding) {
            return false;
        }
        offset += padding;

        if (sizes[i] > UINT64_MAX - offset) {
            return false;
        }
        offset += sizes[i];
    }

    *required_size = offset;
    return true;
}

static inline size_t pgraph_vk_buffer_growth_target_bounded(
    size_t current_size, size_t minimum_size, size_t required_size,
    size_t maximum_size)
{
    if (required_size > maximum_size) {
        return 0;
    }

    size_t new_size = current_size > minimum_size ? current_size : minimum_size;
    if (!new_size && required_size) {
        new_size = 1;
    }
    if (new_size > maximum_size) {
        new_size = required_size;
    }

    while (new_size < required_size) {
        if (new_size > maximum_size / 2) {
            return required_size;
        }
        new_size *= 2;
    }

    return new_size;
}

static inline size_t pgraph_vk_buffer_growth_target(size_t current_size,
                                                    size_t minimum_size,
                                                    size_t required_size)
{
    return pgraph_vk_buffer_growth_target_bounded(
        current_size, minimum_size, required_size, SIZE_MAX);
}

#endif

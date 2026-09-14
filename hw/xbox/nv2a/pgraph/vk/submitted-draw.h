/* Bounded, crash-only record of draws in the current Vulkan command buffer.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_XBOX_NV2A_PGRAPH_VK_SUBMITTED_DRAW_H
#define HW_XBOX_NV2A_PGRAPH_VK_SUBMITTED_DRAW_H

#include <stdint.h>

#define PGRAPH_VK_SUBMITTED_DRAW_CAPACITY 16384u

typedef enum PGRAPHVkSubmittedDrawKind {
    PGRAPH_VK_SUBMITTED_DRAW_CLEAR,
    PGRAPH_VK_SUBMITTED_DRAW_ARRAY,
    PGRAPH_VK_SUBMITTED_DRAW_INDEXED,
    PGRAPH_VK_SUBMITTED_DRAW_INLINE,
} PGRAPHVkSubmittedDrawKind;

typedef struct PGRAPHVkSubmittedDraw {
    uint64_t pipeline_key_hash;
    uint64_t shader_key_hash;
    uint64_t uber_controls_hash;
    uint64_t uber_control_offset;
    uint32_t draw_time;
    uint32_t descriptor_set_index;
    uint32_t first;
    uint32_t count;
    uint8_t route;
    uint8_t kind;
} PGRAPHVkSubmittedDraw;

typedef struct PGRAPHVkSubmittedDrawRing {
    PGRAPHVkSubmittedDraw *records;
    uint32_t head;
    uint32_t count;
    uint64_t dropped;
} PGRAPHVkSubmittedDrawRing;

static inline void pgraph_vk_submitted_draw_reset(
    PGRAPHVkSubmittedDrawRing *ring)
{
    ring->head = 0;
    ring->count = 0;
    ring->dropped = 0;
}

static inline void pgraph_vk_submitted_draw_add(
    PGRAPHVkSubmittedDrawRing *ring, const PGRAPHVkSubmittedDraw *draw)
{
    if (!ring->records) {
        return;
    }
    uint32_t slot = (ring->head + ring->count) &
                    (PGRAPH_VK_SUBMITTED_DRAW_CAPACITY - 1);
    if (ring->count == PGRAPH_VK_SUBMITTED_DRAW_CAPACITY) {
        ring->head = (ring->head + 1) &
                     (PGRAPH_VK_SUBMITTED_DRAW_CAPACITY - 1);
        ring->dropped++;
    } else {
        ring->count++;
    }
    ring->records[slot] = *draw;
}

#endif

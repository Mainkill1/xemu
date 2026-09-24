/*
 * NV2A Vulkan draw completion lifecycle
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_XBOX_NV2A_PGRAPH_VK_DRAW_LIFECYCLE_H
#define HW_XBOX_NV2A_PGRAPH_VK_DRAW_LIFECYCLE_H

#include <stdbool.h>

typedef struct PGRAPHState PGRAPHState;
typedef struct PGRAPHVkState PGRAPHVkState;

typedef enum PGRAPHVkDrawPrepareResult {
    PGRAPH_VK_DRAW_PREPARE_READY,
    PGRAPH_VK_DRAW_PREPARE_OMITTED_SHADER_MISS,
    PGRAPH_VK_DRAW_PREPARE_FAILED,
} PGRAPHVkDrawPrepareResult;

typedef enum PGRAPHVkDrawResult {
    PGRAPH_VK_DRAW_SUBMITTED,
    PGRAPH_VK_DRAW_OMITTED_SHADER_MISS,
    PGRAPH_VK_DRAW_FAILED,
} PGRAPHVkDrawResult;

void pgraph_vk_complete_draw_lifecycle(
    PGRAPHState *pg, PGRAPHVkState *r, PGRAPHVkDrawResult result,
    bool color_write, bool zeta_write, bool color_dirty, bool zeta_dirty);

#endif

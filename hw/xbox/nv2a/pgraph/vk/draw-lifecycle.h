/*
 * NV2A Vulkan draw completion lifecycle
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_XBOX_NV2A_PGRAPH_VK_DRAW_LIFECYCLE_H
#define HW_XBOX_NV2A_PGRAPH_VK_DRAW_LIFECYCLE_H

#include <stdbool.h>
#include <stddef.h>

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

static inline PGRAPHVkDrawResult pgraph_vk_draw_result_from_prepare(
    PGRAPHVkDrawPrepareResult result)
{
    switch (result) {
    case PGRAPH_VK_DRAW_PREPARE_READY:
        return PGRAPH_VK_DRAW_SUBMITTED;
    case PGRAPH_VK_DRAW_PREPARE_OMITTED_SHADER_MISS:
        return PGRAPH_VK_DRAW_OMITTED_SHADER_MISS;
    case PGRAPH_VK_DRAW_PREPARE_FAILED:
        return PGRAPH_VK_DRAW_FAILED;
    default:
        g_assert_not_reached();
    }
}

typedef enum PGRAPHVkDrawShaderMissAction {
    PGRAPH_VK_DRAW_MISS_WAIT,
    PGRAPH_VK_DRAW_MISS_READY,
    PGRAPH_VK_DRAW_MISS_OMIT,
} PGRAPHVkDrawShaderMissAction;

typedef enum PGRAPHVkDrawEncoding {
    PGRAPH_VK_DRAW_ENCODING_ARRAYS,
    PGRAPH_VK_DRAW_ENCODING_INLINE_ELEMENTS,
    PGRAPH_VK_DRAW_ENCODING_INLINE_BUFFER,
    PGRAPH_VK_DRAW_ENCODING_INLINE_ARRAY,
} PGRAPHVkDrawEncoding;

typedef struct PGRAPHVkDrawOmissionCheckpoint {
    PGRAPHVkDrawEncoding encoding;
    size_t vertex_inline_staging_offset;
    size_t index_staging_offset;
    size_t vertex_ram_stale_page_count;
} PGRAPHVkDrawOmissionCheckpoint;

PGRAPHVkDrawShaderMissAction pgraph_vk_draw_shader_miss_action(
    bool continue_requested, bool nonblocking_supported,
    bool omission_supported, bool executable_ready);

void pgraph_vk_draw_omission_checkpoint_capture(
    const PGRAPHVkState *r, PGRAPHVkDrawEncoding encoding,
    PGRAPHVkDrawOmissionCheckpoint *checkpoint);
bool pgraph_vk_draw_omission_state_is_clean(
    const PGRAPHVkState *r,
    const PGRAPHVkDrawOmissionCheckpoint *checkpoint);
void pgraph_vk_discard_unsubmitted_draw_state(
    PGRAPHVkState *r,
    const PGRAPHVkDrawOmissionCheckpoint *checkpoint);

void pgraph_vk_complete_draw_lifecycle(
    PGRAPHState *pg, PGRAPHVkState *r, PGRAPHVkDrawResult result,
    bool color_write, bool zeta_write, bool color_dirty, bool zeta_dirty);

#endif

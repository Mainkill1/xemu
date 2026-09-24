/*
 * Completed Vulkan display output and UI host-copy upload identity.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_DISPLAY_OUTPUT_STATE_H
#define HW_XBOX_NV2A_PGRAPH_VK_DISPLAY_OUTPUT_STATE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct PGRAPHVkHostCopyUploadState {
    bool valid;
    uint64_t generation;
    int width;
    int height;
} PGRAPHVkHostCopyUploadState;

typedef enum PGRAPHVkBlackoutStatus {
    PGRAPH_VK_BLACKOUT_INACTIVE,
    PGRAPH_VK_BLACKOUT_COMPILING,
    PGRAPH_VK_BLACKOUT_DEFERRED,
    PGRAPH_VK_BLACKOUT_FAILED,
} PGRAPHVkBlackoutStatus;

typedef struct PGRAPHVkBlackoutState {
    uint64_t miss_epoch;
    uint64_t last_omission_frame;
    uint32_t pending_demands;
    bool submitted_draw_after_last_omission;
    PGRAPHVkBlackoutStatus status;
} PGRAPHVkBlackoutState;

static inline bool pgraph_vk_blackout_active(
    const PGRAPHVkBlackoutState *blackout)
{
    return blackout->status != PGRAPH_VK_BLACKOUT_INACTIVE;
}

static inline void pgraph_vk_blackout_reset(PGRAPHVkBlackoutState *blackout)
{
    *blackout = (PGRAPHVkBlackoutState) { 0 };
}

static inline void pgraph_vk_blackout_note_omission(
    PGRAPHVkBlackoutState *blackout, uint64_t guest_frame,
    uint32_t pending_demands, PGRAPHVkBlackoutStatus status)
{
    blackout->miss_epoch++;
    if (blackout->miss_epoch == 0) {
        blackout->miss_epoch = 1;
    }
    blackout->last_omission_frame = guest_frame;
    blackout->pending_demands = pending_demands;
    blackout->submitted_draw_after_last_omission = false;
    blackout->status = status == PGRAPH_VK_BLACKOUT_DEFERRED ||
                               status == PGRAPH_VK_BLACKOUT_FAILED ?
                           status : PGRAPH_VK_BLACKOUT_COMPILING;
}

static inline void pgraph_vk_blackout_note_demand_snapshot(
    PGRAPHVkBlackoutState *blackout, uint32_t pending_demands,
    bool deferred, bool failed)
{
    if (!pgraph_vk_blackout_active(blackout)) {
        return;
    }
    blackout->pending_demands = pending_demands;
    if (blackout->status == PGRAPH_VK_BLACKOUT_FAILED || failed) {
        blackout->status = PGRAPH_VK_BLACKOUT_FAILED;
    } else if (deferred) {
        blackout->status = PGRAPH_VK_BLACKOUT_DEFERRED;
    } else {
        blackout->status = PGRAPH_VK_BLACKOUT_COMPILING;
    }
}

static inline void pgraph_vk_blackout_note_submitted_draw(
    PGRAPHVkBlackoutState *blackout)
{
    if (pgraph_vk_blackout_active(blackout)) {
        blackout->submitted_draw_after_last_omission = true;
    }
}

static inline void pgraph_vk_blackout_note_flip(
    PGRAPHVkBlackoutState *blackout)
{
    if (blackout->status == PGRAPH_VK_BLACKOUT_COMPILING &&
        blackout->pending_demands == 0 &&
        blackout->submitted_draw_after_last_omission) {
        pgraph_vk_blackout_reset(blackout);
    }
}

static inline bool pgraph_vk_host_copy_upload_needed(
    const PGRAPHVkHostCopyUploadState *upload, uint64_t completed_generation,
    int width, int height)
{
    return !upload->valid || upload->generation != completed_generation ||
           upload->width != width || upload->height != height;
}

static inline void pgraph_vk_host_copy_mark_uploaded(
    PGRAPHVkHostCopyUploadState *upload, uint64_t completed_generation,
    int width, int height)
{
    upload->generation = completed_generation;
    upload->width = width;
    upload->height = height;
    upload->valid = true;
}

static inline void pgraph_vk_host_copy_invalidate_upload(
    PGRAPHVkHostCopyUploadState *upload)
{
    upload->valid = false;
}

static inline void pgraph_vk_host_copy_publish_completed(
    uint64_t *completed_generation, PGRAPHVkHostCopyUploadState *upload)
{
    ++*completed_generation;
    if (*completed_generation == 0) {
        *completed_generation = 1;
        pgraph_vk_host_copy_invalidate_upload(upload);
    }
}

#endif

/*
 * Vulkan completed-display upload state transitions.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "hw/xbox/nv2a/pgraph/vk/display-output-state.h"

static bool first_output_needs_upload(void)
{
    PGRAPHVkHostCopyUploadState upload = { 0 };
    return pgraph_vk_host_copy_upload_needed(&upload, 0, 640, 480);
}

static bool completed_output_is_uploaded_once(void)
{
    PGRAPHVkHostCopyUploadState upload = { 0 };
    pgraph_vk_host_copy_mark_uploaded(&upload, 7, 640, 480);
    return !pgraph_vk_host_copy_upload_needed(&upload, 7, 640, 480);
}

static bool fresh_output_needs_upload_at_same_size(void)
{
    PGRAPHVkHostCopyUploadState upload = { 0 };
    pgraph_vk_host_copy_mark_uploaded(&upload, 7, 640, 480);
    return pgraph_vk_host_copy_upload_needed(&upload, 8, 640, 480);
}

static bool changed_dimensions_need_upload(void)
{
    PGRAPHVkHostCopyUploadState upload = { 0 };
    pgraph_vk_host_copy_mark_uploaded(&upload, 7, 640, 480);
    return pgraph_vk_host_copy_upload_needed(&upload, 7, 1280, 480) &&
           pgraph_vk_host_copy_upload_needed(&upload, 7, 640, 960);
}

static bool destroyed_texture_needs_upload(void)
{
    PGRAPHVkHostCopyUploadState upload = { 0 };
    pgraph_vk_host_copy_mark_uploaded(&upload, 7, 640, 480);
    pgraph_vk_host_copy_invalidate_upload(&upload);
    return pgraph_vk_host_copy_upload_needed(&upload, 7, 640, 480);
}

static bool uncommitted_upload_remains_needed(void)
{
    PGRAPHVkHostCopyUploadState upload = { 0 };
    return pgraph_vk_host_copy_upload_needed(&upload, 7, 640, 480);
}

static bool generation_wrap_invalidates_previous_upload(void)
{
    PGRAPHVkHostCopyUploadState upload = { 0 };
    uint64_t generation = UINT64_MAX;
    pgraph_vk_host_copy_mark_uploaded(&upload, generation, 640, 480);
    pgraph_vk_host_copy_publish_completed(&generation, &upload);
    return generation == 1 &&
           pgraph_vk_host_copy_upload_needed(&upload, generation, 640, 480);
}

static bool first_omission_enters_blackout(void)
{
    PGRAPHVkBlackoutState blackout = { 0 };
    pgraph_vk_blackout_note_omission(
        &blackout, 12, 1, PGRAPH_VK_BLACKOUT_COMPILING);
    return pgraph_vk_blackout_active(&blackout) &&
           blackout.status == PGRAPH_VK_BLACKOUT_COMPILING &&
           blackout.miss_epoch == 1 &&
           blackout.last_omission_frame == 12 &&
           blackout.pending_demands == 1 &&
           !blackout.submitted_draw_after_last_omission;
}

static bool repeated_omission_cancels_pending_release(void)
{
    PGRAPHVkBlackoutState blackout = { 0 };
    pgraph_vk_blackout_note_omission(
        &blackout, 20, 1, PGRAPH_VK_BLACKOUT_COMPILING);
    pgraph_vk_blackout_note_demand_snapshot(&blackout, 0, false, false);
    pgraph_vk_blackout_note_submitted_draw(&blackout);

    pgraph_vk_blackout_note_omission(
        &blackout, 21, 1, PGRAPH_VK_BLACKOUT_COMPILING);
    pgraph_vk_blackout_note_flip(&blackout);
    return pgraph_vk_blackout_active(&blackout) &&
           blackout.miss_epoch == 2 &&
           blackout.last_omission_frame == 21 &&
           blackout.pending_demands == 1 &&
           !blackout.submitted_draw_after_last_omission;
}

static bool blackout_requires_resolution_draw_and_flip(void)
{
    PGRAPHVkBlackoutState blackout = { 0 };
    pgraph_vk_blackout_note_omission(
        &blackout, 30, 2, PGRAPH_VK_BLACKOUT_COMPILING);

    pgraph_vk_blackout_note_submitted_draw(&blackout);
    pgraph_vk_blackout_note_flip(&blackout);
    if (!pgraph_vk_blackout_active(&blackout)) {
        return false;
    }

    pgraph_vk_blackout_note_demand_snapshot(&blackout, 0, false, false);
    if (!pgraph_vk_blackout_active(&blackout)) {
        return false;
    }
    pgraph_vk_blackout_note_flip(&blackout);
    return !pgraph_vk_blackout_active(&blackout);
}

static bool deferred_can_recover_but_failure_is_sticky(void)
{
    PGRAPHVkBlackoutState blackout = { 0 };
    pgraph_vk_blackout_note_omission(
        &blackout, 40, 0, PGRAPH_VK_BLACKOUT_DEFERRED);
    if (blackout.status != PGRAPH_VK_BLACKOUT_DEFERRED) {
        return false;
    }

    pgraph_vk_blackout_note_demand_snapshot(&blackout, 1, false, false);
    if (blackout.status != PGRAPH_VK_BLACKOUT_COMPILING) {
        return false;
    }
    pgraph_vk_blackout_note_demand_snapshot(&blackout, 0, false, true);
    pgraph_vk_blackout_note_submitted_draw(&blackout);
    pgraph_vk_blackout_note_flip(&blackout);
    if (blackout.status != PGRAPH_VK_BLACKOUT_FAILED) {
        return false;
    }

    pgraph_vk_blackout_note_demand_snapshot(&blackout, 0, false, false);
    pgraph_vk_blackout_note_flip(&blackout);
    if (blackout.status != PGRAPH_VK_BLACKOUT_FAILED) {
        return false;
    }

    pgraph_vk_blackout_reset(&blackout);
    return !pgraph_vk_blackout_active(&blackout) &&
           blackout.status == PGRAPH_VK_BLACKOUT_INACTIVE;
}

int main(void)
{
    static const struct {
        const char *name;
        bool (*run)(void);
    } tests[] = {
        { "first output needs upload", first_output_needs_upload },
        { "same completed output uploads once",
          completed_output_is_uploaded_once },
        { "new generation uploads at same size",
          fresh_output_needs_upload_at_same_size },
        { "changed dimensions upload", changed_dimensions_need_upload },
        { "destroyed texture invalidates upload",
          destroyed_texture_needs_upload },
        { "uncommitted upload remains needed",
          uncommitted_upload_remains_needed },
        { "generation wrap invalidates upload",
          generation_wrap_invalidates_previous_upload },
        { "first omission enters blackout", first_omission_enters_blackout },
        { "repeated omission cancels release",
          repeated_omission_cancels_pending_release },
        { "recovery requires resolution draw and flip",
          blackout_requires_resolution_draw_and_flip },
        { "deferred recovers and failure is sticky",
          deferred_can_recover_but_failure_is_sticky },
    };
    const size_t count = sizeof(tests) / sizeof(tests[0]);
    bool passed = true;

    puts("TAP version 13");
    printf("1..%zu\n", count);
    for (size_t i = 0; i < count; i++) {
        bool result = tests[i].run();
        printf("%s %zu - %s\n", result ? "ok" : "not ok", i + 1,
               tests[i].name);
        passed &= result;
    }
    return passed ? 0 : 1;
}

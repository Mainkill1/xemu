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

static bool failed_upload_remains_retryable(void)
{
    PGRAPHVkHostCopyUploadState upload = { 0 };
    /* A failed GL operation never calls mark_uploaded(). */
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

int main(void)
{
    bool results[] = {
        first_output_needs_upload(),
        completed_output_is_uploaded_once(),
        fresh_output_needs_upload_at_same_size(),
        changed_dimensions_need_upload(),
        destroyed_texture_needs_upload(),
        failed_upload_remains_retryable(),
        generation_wrap_invalidates_previous_upload(),
    };
    const char *names[] = {
        "first output needs upload",
        "same completed output uploads once",
        "new generation uploads at same size",
        "changed dimensions upload",
        "destroyed texture invalidates upload",
        "failed upload remains retryable",
        "generation wrap invalidates upload",
    };
    bool passed = true;

    puts("TAP version 13");
    puts("1..7");
    for (int i = 0; i < 7; i++) {
        printf("%s %d - %s\n", results[i] ? "ok" : "not ok", i + 1,
               names[i]);
        passed &= results[i];
    }
    return passed ? 0 : 1;
}

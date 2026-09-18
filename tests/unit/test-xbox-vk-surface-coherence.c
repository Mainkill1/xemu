/*
 * NV2A Vulkan surface/guest-memory coherence tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdbool.h>
#include <stdio.h>

#include "hw/xbox/nv2a/pgraph/vk/surface-coherence.h"

static bool test_clean_guest_memory_preserves_surface_state(void)
{
    bool download_pending = true;
    bool draw_dirty = true;
    bool upload_pending = false;

    if (pgraph_vk_surface_resolve_guest_write(
            false, &download_pending, &draw_dirty, &upload_pending)) {
        return false;
    }

    return download_pending && draw_dirty && !upload_pending;
}

static bool test_guest_write_preempts_stale_surface_download(void)
{
    bool download_pending = true;
    bool draw_dirty = true;
    bool upload_pending = false;

    if (!pgraph_vk_surface_resolve_guest_write(
            true, &download_pending, &draw_dirty, &upload_pending)) {
        return false;
    }

    return !download_pending && !draw_dirty && upload_pending;
}

static bool test_guest_write_keeps_existing_upload_pending(void)
{
    bool download_pending = false;
    bool draw_dirty = false;
    bool upload_pending = true;

    return pgraph_vk_surface_resolve_guest_write(
               true, &download_pending, &draw_dirty, &upload_pending) &&
           !download_pending && !draw_dirty && upload_pending;
}

static bool test_upload_pending_transition_is_counted_once(void)
{
    bool upload_pending = false;

    return pgraph_vk_surface_mark_upload_pending(&upload_pending) &&
           upload_pending &&
           !pgraph_vk_surface_mark_upload_pending(&upload_pending) &&
           upload_pending;
}

int main(void)
{
    bool clean = test_clean_guest_memory_preserves_surface_state();
    bool preempt = test_guest_write_preempts_stale_surface_download();
    bool upload = test_guest_write_keeps_existing_upload_pending();
    bool transition = test_upload_pending_transition_is_counted_once();

    puts("TAP version 13");
    puts("1..4");
    printf("%s 1 - clean guest memory preserves surface state\n",
           clean ? "ok" : "not ok");
    printf("%s 2 - guest write preempts stale surface download\n",
           preempt ? "ok" : "not ok");
    printf("%s 3 - guest write keeps upload pending\n",
           upload ? "ok" : "not ok");
    printf("%s 4 - upload pending transition is counted once\n",
           transition ? "ok" : "not ok");

    return clean && preempt && upload && transition ? 0 : 1;
}

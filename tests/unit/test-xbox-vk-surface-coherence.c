/*
 * NV2A Vulkan surface/guest-memory coherence tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdbool.h>
#include <stdint.h>
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

typedef struct DirtyPageFixture {
    bool dirty;
    bool surface_stale[3];
    unsigned int tests;
    unsigned int visits;
    uint64_t tested_start;
    uint64_t tested_size;
} DirtyPageFixture;

static bool consume_dirty_range(void *opaque, uint64_t start, uint64_t size)
{
    DirtyPageFixture *fixture = opaque;
    bool dirty = fixture->dirty;

    fixture->dirty = false;
    fixture->tests++;
    fixture->tested_start = start;
    fixture->tested_size = size;
    return dirty;
}

static void mark_overlapping_surfaces(void *opaque, uint64_t start,
                                      uint64_t size)
{
    DirtyPageFixture *fixture = opaque;
    static const uint64_t surface_start[] = { 0, 256, 4096 };
    static const uint64_t surface_size[] = { 2048, 1024, 4096 };

    for (unsigned int i = 0; i < 3; i++) {
        if (pgraph_vk_surface_range_overlaps(surface_start[i],
                                            surface_size[i], start, size)) {
            fixture->surface_stale[i] = true;
        }
    }
    fixture->visits++;
}

static bool test_one_dirty_page_reaches_both_overlapping_surfaces(void)
{
    DirtyPageFixture fixture = { .dirty = true };

    bool any_dirty = pgraph_vk_surface_consume_dirty_range(
        128, 128, 4096, consume_dirty_range, mark_overlapping_surfaces,
        &fixture);

    return any_dirty && fixture.tests == 1 && fixture.visits == 1 &&
           fixture.tested_start == 128 && fixture.tested_size == 128 &&
           fixture.surface_stale[0] && fixture.surface_stale[1] &&
           !fixture.surface_stale[2] && !fixture.dirty;
}

static bool test_clean_range_does_not_touch_surfaces(void)
{
    DirtyPageFixture fixture = { 0 };

    bool any_dirty = pgraph_vk_surface_consume_dirty_range(
        128, 128, 4096, consume_dirty_range, mark_overlapping_surfaces,
        &fixture);

    return !any_dirty && fixture.tests == 1 && fixture.visits == 0 &&
           !fixture.surface_stale[0] && !fixture.surface_stale[1] &&
           !fixture.surface_stale[2];
}

int main(void)
{
    bool clean = test_clean_guest_memory_preserves_surface_state();
    bool preempt = test_guest_write_preempts_stale_surface_download();
    bool upload = test_guest_write_keeps_existing_upload_pending();
    bool transition = test_upload_pending_transition_is_counted_once();
    bool overlap = test_one_dirty_page_reaches_both_overlapping_surfaces();
    bool clean_range = test_clean_range_does_not_touch_surfaces();

    puts("TAP version 13");
    puts("1..6");
    printf("%s 1 - clean guest memory preserves surface state\n",
           clean ? "ok" : "not ok");
    printf("%s 2 - guest write preempts stale surface download\n",
           preempt ? "ok" : "not ok");
    printf("%s 3 - guest write keeps upload pending\n",
           upload ? "ok" : "not ok");
    printf("%s 4 - upload pending transition is counted once\n",
           transition ? "ok" : "not ok");
    printf("%s 5 - one dirty page reaches both overlapping surfaces\n",
           overlap ? "ok" : "not ok");
    printf("%s 6 - clean range preserves cached surfaces\n",
           clean_range ? "ok" : "not ok");

    return clean && preempt && upload && transition && overlap &&
           clean_range ? 0 : 1;
}

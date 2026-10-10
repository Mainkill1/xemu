/*
 * Vulkan idle-read materialization ordering checks.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/xbox/nv2a/nv2a_int.h"

/* Retain the callback without registering the unrelated renderer lifecycle. */
#define pgraph_renderer_register(renderer) ((void)sizeof(*(renderer)))
#include "hw/xbox/nv2a/pgraph/vk/renderer.c"
#undef pgraph_renderer_register

typedef struct ReadbackFixture {
    NV2AState d;
    PGRAPHVkState renderer;
    SurfaceBinding surfaces[2];
    unsigned int attempted;
    unsigned int published;
    unsigned int waits;
    unsigned int finish_calls;
    bool download_saw_active;
    bool fail_first;
    bool legacy;
    bool reports_retired;
} ReadbackFixture;

static ReadbackFixture *current;

void pgraph_vk_finish(PGRAPHState *pg, FinishReason reason)
{
    g_assert_true(pg == &current->d.pgraph);
    g_assert_cmpint(reason, ==, VK_FINISH_REASON_SURFACE_DOWN);
    current->finish_calls++;
    if (current->renderer.in_command_buffer) {
        current->waits++;
        current->renderer.in_command_buffer = false;
    }
    current->reports_retired = true;
}

bool pgraph_vk_surface_download_if_dirty(NV2AState *d, SurfaceBinding *surface)
{
    g_assert_true(d == &current->d);
    current->attempted++;
    if (!surface->draw_dirty || surface->readback_superseded_by_guest) {
        return true;
    }
    current->download_saw_active |= current->renderer.in_command_buffer;
    if (current->fail_first && surface == &current->surfaces[0]) {
        return false;
    }
    if (current->legacy) {
        /* The legacy helper owns its producer wait and auxiliary copy. */
        pgraph_vk_finish(&d->pgraph, VK_FINISH_REASON_SURFACE_DOWN);
        current->waits++;
    } else if (current->renderer.in_command_buffer) {
        /* An active color producer can include its readback in one wait. */
        pgraph_vk_finish(&d->pgraph, VK_FINISH_REASON_SURFACE_DOWN);
    } else {
        current->waits++;
    }
    current->published++;
    surface->draw_dirty = false;
    return true;
}

static void fixture_init(ReadbackFixture *fixture, unsigned int surfaces)
{
    memset(fixture, 0, sizeof(*fixture));
    current = fixture;
    fixture->d.pgraph.vk_renderer_state = &fixture->renderer;
    fixture->renderer.in_command_buffer = true;
    QTAILQ_INIT(&fixture->renderer.surfaces);
    for (unsigned int i = 0; i < surfaces; i++) {
        fixture->surfaces[i].draw_dirty = true;
        QTAILQ_INSERT_TAIL(&fixture->renderer.surfaces, &fixture->surfaces[i],
                          entry);
    }
}

static void test_active_color(void)
{
    g_autofree ReadbackFixture *fixture = g_new0(ReadbackFixture, 1);
    fixture_init(fixture, 1);
    g_assert_true(pgraph_vk_complete_cpu_read(&fixture->d));
    g_assert_true(fixture->download_saw_active);
    g_assert_cmpuint(fixture->waits, ==, 1);
    g_assert_cmpuint(fixture->published, ==, 1);
    g_assert_true(fixture->reports_retired);
    g_assert_false(fixture->renderer.in_command_buffer);
}

static void test_command_only(void)
{
    g_autofree ReadbackFixture *fixture = g_new0(ReadbackFixture, 1);
    fixture_init(fixture, 0);
    g_assert_true(pgraph_vk_complete_cpu_read(&fixture->d));
    g_assert_cmpuint(fixture->finish_calls, ==, 1);
    g_assert_cmpuint(fixture->waits, ==, 1);
    g_assert_true(fixture->reports_retired);
}

static void test_superseded(void)
{
    g_autofree ReadbackFixture *fixture = g_new0(ReadbackFixture, 1);
    fixture_init(fixture, 1);
    fixture->surfaces[0].readback_superseded_by_guest = true;
    g_assert_true(pgraph_vk_complete_cpu_read(&fixture->d));
    g_assert_cmpuint(fixture->attempted, ==, 1);
    g_assert_cmpuint(fixture->published, ==, 0);
    g_assert_cmpuint(fixture->finish_calls, ==, 1);
    g_assert_true(fixture->reports_retired);
}

static void test_failure_continues(void)
{
    g_autofree ReadbackFixture *fixture = g_new0(ReadbackFixture, 1);
    fixture_init(fixture, 2);
    fixture->fail_first = true;
    g_assert_false(pgraph_vk_complete_cpu_read(&fixture->d));
    g_assert_cmpuint(fixture->attempted, ==, 2);
    g_assert_cmpuint(fixture->published, ==, 1);
    g_assert_cmpuint(fixture->waits, ==, 1);
    g_assert_true(fixture->reports_retired);
    g_assert_false(fixture->renderer.in_command_buffer);
}

static void test_legacy_download(void)
{
    g_autofree ReadbackFixture *fixture = g_new0(ReadbackFixture, 1);
    fixture_init(fixture, 1);
    fixture->legacy = true;
    g_assert_true(pgraph_vk_complete_cpu_read(&fixture->d));
    g_assert_cmpuint(fixture->waits, ==, 2);
    g_assert_cmpuint(fixture->published, ==, 1);
    g_assert_true(fixture->reports_retired);
}

static void test_report_only(void)
{
    g_autofree ReadbackFixture *fixture = g_new0(ReadbackFixture, 1);
    fixture_init(fixture, 0);
    fixture->renderer.in_command_buffer = false;
    g_assert_true(pgraph_vk_complete_cpu_read(&fixture->d));
    g_assert_cmpuint(fixture->finish_calls, ==, 1);
    g_assert_cmpuint(fixture->waits, ==, 0);
    g_assert_true(fixture->reports_retired);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/nv2a/vk/idle-readback/active-color", test_active_color);
    g_test_add_func("/nv2a/vk/idle-readback/command-only", test_command_only);
    g_test_add_func("/nv2a/vk/idle-readback/superseded", test_superseded);
    g_test_add_func("/nv2a/vk/idle-readback/failure-continues",
                    test_failure_continues);
    g_test_add_func("/nv2a/vk/idle-readback/legacy", test_legacy_download);
    g_test_add_func("/nv2a/vk/idle-readback/report-only", test_report_only);
    return g_test_run();
}

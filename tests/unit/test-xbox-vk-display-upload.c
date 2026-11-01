/*
 * Production display/upload submission-order tests
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/display.c"

bool tcg_allowed;
static bool queue_upload, fail_upload, force_seen;
static unsigned int finish_calls;

bool pgraph_vk_upload_surface_data(NV2AState *d, SurfaceBinding *surface,
                                    bool force)
{
    PGRAPHVkState *r = d->pgraph.vk_renderer_state;

    force_seen = force;
    if (fail_upload) {
        return false;
    }
    if (queue_upload) {
        r->in_command_buffer = true;
        r->command_buffer_start_time = d->pgraph.draw_time;
        surface->draw_time = d->pgraph.draw_time;
    }
    return true;
}

void pgraph_vk_finish(PGRAPHState *pg, FinishReason reason)
{
    g_assert_cmpint(reason, ==, VK_FINISH_REASON_PRESENTING);
    pg->vk_renderer_state->in_command_buffer = false;
    finish_calls++;
}

static void test_display_upload(gconstpointer data)
{
    unsigned int scenario = GPOINTER_TO_UINT(data);
    g_autofree NV2AState *d = g_new0(NV2AState, 1);
    g_autofree PGRAPHVkState *r = g_new0(PGRAPHVkState, 1);
    SurfaceBinding surface = { .draw_time = 10 };

    d->pgraph.vk_renderer_state = r;
    d->pgraph.draw_time = 20;
    r->command_buffer_start_time = 10;
    r->in_command_buffer = scenario == 2;
    queue_upload = scenario < 2;
    fail_upload = scenario == 3;
    tcg_allowed = scenario != 1;
    finish_calls = 0;
    bool ready = prepare_display_surface(&d->pgraph, &surface);
    g_assert_cmpint(ready, ==, !fail_upload);
    g_assert_cmpint(force_seen, ==, !tcg_allowed);
    g_assert_false(r->in_command_buffer);
    g_assert_cmpuint(finish_calls, ==, fail_upload ? 0 : 1);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_data_func("/xbox/vk/display/pending-tcg-upload", NULL,
                         test_display_upload);
    g_test_add_data_func("/xbox/vk/display/forced-upload", GUINT_TO_POINTER(1),
                         test_display_upload);
    g_test_add_data_func("/xbox/vk/display/existing-draw", GUINT_TO_POINTER(2),
                         test_display_upload);
    g_test_add_data_func("/xbox/vk/display/upload-failure", GUINT_TO_POINTER(3),
                         test_display_upload);
    return g_test_run();
}

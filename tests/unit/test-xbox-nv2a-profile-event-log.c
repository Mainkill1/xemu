/*
 * NV2A opt-in event log tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include <glib/gstdio.h>

#include "qemu/log.h"
#include "hw/xbox/nv2a/debug.h"

static void assert_occurs_once(const char *contents, const char *needle)
{
    const char *first = strstr(contents, needle);

    g_assert_nonnull(first);
    g_assert_null(strstr(first + strlen(needle), needle));
}

static void test_events_are_written_once(void)
{
    g_autofree char *path = NULL;
    g_autofree char *contents = NULL;
    size_t length;
    int fd;

    fd = g_file_open_tmp("xemu-event-log-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);

    g_assert_true(qemu_set_log_filename_flags(path, 0, NULL));
    nv2a_profile_log_event_once(NV2A_PROFILE_EVENT_GPU_SUBMIT);
    g_assert_true(g_file_get_contents(path, &contents, &length, NULL));
    g_assert_cmpuint(length, ==, 0);
    g_clear_pointer(&contents, g_free);

    g_assert_true(qemu_set_log(qemu_str_to_log_mask("nv2a"), NULL));

    nv2a_profile_log_event_once(NV2A_PROFILE_EVENT_GPU_SUBMIT);
    nv2a_profile_log_event_once(NV2A_PROFILE_EVENT_GPU_SUBMIT);
    nv2a_profile_log_event_once(NV2A_PROFILE_EVENT_SHADER_COMPILE);
    nv2a_profile_log_event_once(NV2A_PROFILE_EVENT_SHADER_COMPILE);
    nv2a_profile_log_event_once(NV2A_PROFILE_EVENT_READBACK);
    nv2a_profile_log_event_once(NV2A_PROFILE_EVENT_READBACK);

    g_assert_true(g_file_get_contents(path, &contents, &length, NULL));
    g_assert_cmpuint(length, >, 0);
    assert_occurs_once(contents, "\"event\":\"gpu_submit\"");
    assert_occurs_once(contents, "\"event\":\"shader_compile\"");
    assert_occurs_once(contents, "\"event\":\"readback\"");
}

static void test_preview_flip_snapshot(void)
{
    uint64_t before = 0, after = 0, interval = 0;
    nv2a_profile_preview_flip_snapshot(&before, &interval);
    nv2a_profile_increment();
    nv2a_profile_preview_flip_snapshot(&after, &interval);
    g_assert_cmpuint(after, ==, before + 1);
    g_usleep(1000);
    nv2a_profile_increment();
    nv2a_profile_preview_flip_snapshot(&after, &interval);
    g_assert_cmpuint(after, ==, before + 2);
    g_assert_cmpuint(interval, >, 0);
}

static void test_preview_renderer_epoch(void)
{
    uint64_t before = nv2a_profile_preview_renderer_epoch();
    uint64_t flips = 0, interval = 0;
    nv2a_profile_increment();
    g_usleep(1000);
    nv2a_profile_increment();
    nv2a_profile_preview_flip_snapshot(&flips, &interval);
    g_assert_cmpuint(interval, >, 0);
    nv2a_profile_preview_advance_renderer_epoch();
    g_assert_cmpuint(nv2a_profile_preview_renderer_epoch(), ==, before + 1);
    nv2a_profile_preview_flip_snapshot(&flips, &interval);
    g_assert_cmpuint(interval, ==, 0);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/nv2a/profile/event-log-once",
                    test_events_are_written_once);
    g_test_add_func("/nv2a/profile/preview-flip-snapshot",
                    test_preview_flip_snapshot);
    g_test_add_func("/nv2a/profile/preview-renderer-epoch",
                    test_preview_renderer_epoch);

    return g_test_run();
}

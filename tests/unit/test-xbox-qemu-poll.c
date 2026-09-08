/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Focused Windows tests for xemu's short-deadline polling path. */
#include "qemu/osdep.h"
#include "qemu/timer.h"

#include <windows.h>

#define SHORT_TIMEOUT_NS 500000
#define READY_TIMEOUT_NS 1000000

static GPollFD pollfd_from_handle(HANDLE handle)
{
    return (GPollFD) {
        .fd = (gintptr)handle,
        .events = G_IO_IN,
    };
}

static int64_t poll_elapsed_us(GPollFD *fds, guint nfds, int64_t timeout,
                               int *result)
{
    int64_t start = g_get_monotonic_time();

    *result = qemu_poll_ns(fds, nfds, timeout);
    return g_get_monotonic_time() - start;
}

static void assert_short_timeout(GPollFD *fds, guint nfds)
{
    int64_t elapsed_us;
    int result;

    elapsed_us = poll_elapsed_us(fds, nfds, SHORT_TIMEOUT_NS, &result);
    g_assert_cmpint(result, ==, 0);
    g_assert_cmpint(elapsed_us, >=, 50);
    g_assert_cmpint(elapsed_us, <, 100000);
}

static void test_timeout_only(void)
{
    GPollFD unused = { 0 };

    assert_short_timeout(&unused, 0);
}

static void test_ready_handle_then_timeout(void)
{
    HANDLE event = CreateEventW(NULL, TRUE, TRUE, NULL);
    GPollFD fd;
    int result;

    g_assert_nonnull(event);
    fd = pollfd_from_handle(event);
    result = qemu_poll_ns(&fd, 1, READY_TIMEOUT_NS);
    g_assert_cmpint(result, ==, 1);
    g_assert_cmpint(fd.revents, ==, G_IO_IN);

    g_assert_true(ResetEvent(event));
    fd.revents = 0;
    assert_short_timeout(&fd, 1);
    g_assert_cmpint(fd.revents, ==, 0);
    CloseHandle(event);
}

static void test_raced_timer_does_not_leak(void)
{
    HANDLE event = CreateEventW(NULL, TRUE, FALSE, NULL);
    GPollFD fd;

    g_assert_nonnull(event);
    fd = pollfd_from_handle(event);

    /*
     * Repeatedly make the original event and a near-immediate timer ready.
     * The following timeout must never consume a timer signal left behind by
     * the preceding poll.
     */
    for (unsigned int i = 0; i < 256; i++) {
        int result;

        g_assert_true(SetEvent(event));
        fd.revents = 0;
        result = qemu_poll_ns(&fd, 1, 100);
        g_assert_cmpint(result, ==, 1);
        g_assert_cmpint(fd.revents, ==, G_IO_IN);

        g_assert_true(ResetEvent(event));
        fd.revents = 0;
        assert_short_timeout(&fd, 1);
    }
    CloseHandle(event);
}

static void test_multiple_ready_handles(void)
{
    HANDLE events[2];
    GPollFD fds[2];
    int result;

    for (unsigned int i = 0; i < G_N_ELEMENTS(events); i++) {
        events[i] = CreateEventW(NULL, TRUE, TRUE, NULL);
        g_assert_nonnull(events[i]);
        fds[i] = pollfd_from_handle(events[i]);
    }

    result = qemu_poll_ns(fds, G_N_ELEMENTS(fds), READY_TIMEOUT_NS);
    g_assert_cmpint(result, ==, 2);
    for (unsigned int i = 0; i < G_N_ELEMENTS(events); i++) {
        g_assert_cmpint(fds[i].revents, ==, G_IO_IN);
        CloseHandle(events[i]);
    }
}

static void test_windows_handle_boundary(void)
{
    HANDLE events[MAXIMUM_WAIT_OBJECTS];
    GPollFD fds[MAXIMUM_WAIT_OBJECTS];
    int result;

    for (unsigned int i = 0; i < G_N_ELEMENTS(events); i++) {
        events[i] = CreateEventW(NULL, TRUE, FALSE, NULL);
        g_assert_nonnull(events[i]);
        fds[i] = pollfd_from_handle(events[i]);
    }

    /* qemu_poll_ns appends its private timer, taking this above 64 handles. */
    g_assert_true(SetEvent(events[G_N_ELEMENTS(events) - 1]));
    result = qemu_poll_ns(fds, G_N_ELEMENTS(fds), READY_TIMEOUT_NS);
    g_assert_cmpint(result, ==, 1);
    for (unsigned int i = 0; i < G_N_ELEMENTS(events); i++) {
        g_assert_cmpint(fds[i].revents, ==,
                        i == G_N_ELEMENTS(events) - 1 ? G_IO_IN : 0);
        CloseHandle(events[i]);
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/qemu-poll/timeout-only", test_timeout_only);
    g_test_add_func("/xbox/qemu-poll/ready-then-timeout",
                    test_ready_handle_then_timeout);
    g_test_add_func("/xbox/qemu-poll/raced-timer-does-not-leak",
                    test_raced_timer_does_not_leak);
    g_test_add_func("/xbox/qemu-poll/multiple-ready-handles",
                    test_multiple_ready_handles);
    g_test_add_func("/xbox/qemu-poll/windows-handle-boundary",
                    test_windows_handle_boundary);
    return g_test_run();
}

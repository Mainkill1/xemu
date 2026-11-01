/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "chardev/char-fe.h"
#include "qapi/error.h"
#include "qemu/config-file.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/sockets.h"
#include "system/system.h"

extern GMainContext *qemu_main_context;

static Chardev *new_socket(CharFrontend *frontend, int *peer)
{
    int pair[2];
    g_autofree char *backend = NULL;

    g_assert_cmpint(socketpair(AF_UNIX, SOCK_STREAM, 0, pair), ==, 0);
    backend = g_strdup_printf("socket,id=context-test,fd=%d", pair[0]);
    QemuOpts *opts =
        qemu_opts_parse_noisily(qemu_find_opts("chardev"), backend, true);
    g_assert_nonnull(opts);
    Chardev *chr = qemu_chr_new_from_opts(opts, NULL, &error_abort);
    qemu_opts_del(opts);
    g_assert_nonnull(chr);
    qemu_chr_fe_init(frontend, chr, &error_abort);
    *peer = pair[1];
    return chr;
}

static int can_read(void *opaque)
{
    return 1;
}

static void read_byte(void *opaque, const uint8_t *buffer, int size)
{
}

static void set_context(CharFrontend *frontend, GMainContext *context)
{
    qemu_chr_fe_set_handlers(frontend, can_read, read_byte, NULL, NULL, NULL,
                             context, true);
}

static void assert_watch_context(Chardev *chr, GMainContext *context)
{
    g_assert_true(chr->gcontext == context);
    g_assert_nonnull(chr->gsource);
    g_assert_true(g_source_get_context(chr->gsource) == context);
}

static void test_default_context(void)
{
    CharFrontend frontend;
    int peer;
    Chardev *chr = new_socket(&frontend, &peer);

    set_context(&frontend, NULL);
    assert_watch_context(chr, qemu_main_context);
    qemu_chr_fe_deinit(&frontend, true);
    close(peer);
}

static void test_reset_before_context_teardown(void)
{
    CharFrontend frontend;
    int peer;
    Chardev *chr = new_socket(&frontend, &peer);
    GMainContext *context = g_main_context_new();

    set_context(&frontend, context);
    assert_watch_context(chr, context);
    /* QMP removes handlers before its I/O-thread context is finalized. */
    qemu_chr_fe_set_handlers(&frontend, NULL, NULL, NULL, NULL, NULL, NULL,
                             true);
    assert_watch_context(chr, qemu_main_context);
    g_main_context_unref(context);
    qemu_chr_fe_deinit(&frontend, true);
    close(peer);
}

static void test_rebind_explicit_context(void)
{
    CharFrontend frontend;
    int peer;
    Chardev *chr = new_socket(&frontend, &peer);
    GMainContext *first = g_main_context_new();
    GMainContext *second = g_main_context_new();

    set_context(&frontend, first);
    assert_watch_context(chr, first);
    set_context(&frontend, second);
    assert_watch_context(chr, second);
    g_main_context_unref(first);
    set_context(&frontend, NULL);
    assert_watch_context(chr, qemu_main_context);
    g_main_context_unref(second);
    qemu_chr_fe_deinit(&frontend, true);
    close(peer);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qemu_init_main_loop(&error_abort);
    socket_init();
    module_call_init(MODULE_INIT_QOM);
    qemu_add_opts(&qemu_chardev_opts);
    g_assert_nonnull(qemu_main_context);

    g_test_add_func("/xbox/chardev/context/default", test_default_context);
    g_test_add_func("/xbox/chardev/context/reset-before-teardown",
                    test_reset_before_context_teardown);
    g_test_add_func("/xbox/chardev/context/rebind-explicit",
                    test_rebind_explicit_context);
    int result = g_test_run();
    return result;
}

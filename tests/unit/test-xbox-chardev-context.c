/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "chardev/char-fe.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-char.h"
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

static Chardev *new_backend(const char *options)
{
    QemuOpts *opts =
        qemu_opts_parse_noisily(qemu_find_opts("chardev"), options, true);
    g_assert_nonnull(opts);
    Chardev *chr = qemu_chr_new_from_opts(opts, NULL, &error_abort);
    qemu_opts_del(opts);
    g_assert_nonnull(chr);
    return chr;
}

static void test_forwarded_default_context(void)
{
    CharFrontend frontend;
    Chardev *chr = new_backend("ringbuf,id=forwarded-default");

    g_assert_false(qemu_chr_has_feature(chr, QEMU_CHAR_FEATURE_GCONTEXT));
    qemu_chr_fe_init(&frontend, chr, &error_abort);
    set_context(&frontend, qemu_main_context);
    g_assert_cmpint(qemu_chr_fe_write(&frontend, (const uint8_t *)"ok", 2),
                    ==, 2);
    g_autofree char *data =
        qmp_ringbuf_read("forwarded-default", 2, false, 0, &error_abort);
    g_assert_cmpstr(data, ==, "ok");
    qemu_chr_fe_deinit(&frontend, true);
}

static void test_composite_default_context(const void *opaque)
{
    CharFrontend frontend;
    Chardev *chr, *base;

    if (g_str_equal(opaque, "mux")) {
        chr = new_backend("ringbuf,id=composite-default,mux=on");
        base = qemu_chr_find("composite-default-base");
    } else {
        base = new_backend("ringbuf,id=composite-default-base");
        chr = new_backend("hub,id=composite-default,"
                          "chardevs.0=composite-default-base");
    }
    g_assert_nonnull(base);
    g_assert_false(qemu_chr_has_feature(base, QEMU_CHAR_FEATURE_GCONTEXT));
    qemu_chr_fe_init(&frontend, chr, &error_abort);
    set_context(&frontend, NULL);
    g_assert_cmpint(qemu_chr_fe_write(&frontend, (const uint8_t *)"ok", 2),
                    ==, 2);
    g_autofree char *data =
        qmp_ringbuf_read("composite-default-base", 2, false, 0, &error_abort);
    g_assert_cmpstr(data, ==, "ok");
    qemu_chr_fe_deinit(&frontend, true);
    object_unparent(OBJECT(base));
}

static void test_unsupported_custom_context(void)
{
    if (g_test_subprocess()) {
        CharFrontend frontend;
        Chardev *chr = new_backend("ringbuf,id=unsupported-custom");
        GMainContext *context = g_main_context_new();

        g_assert_false(qemu_chr_has_feature(chr, QEMU_CHAR_FEATURE_GCONTEXT));
        qemu_chr_fe_init(&frontend, chr, &error_abort);
        set_context(&frontend, context);
        qemu_chr_fe_deinit(&frontend, true);
        g_main_context_unref(context);
        return;
    }
    g_test_trap_subprocess(NULL, 0, 0);
    g_test_trap_assert_failed();
    g_test_trap_assert_stderr("*qemu_chr_be_update_read_handlers*context*");
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
    g_test_add_func("/xbox/chardev/context/forwarded-default",
                    test_forwarded_default_context);
    g_test_add_data_func("/xbox/chardev/context/mux-default", "mux",
                         test_composite_default_context);
    g_test_add_data_func("/xbox/chardev/context/hub-default", "hub",
                         test_composite_default_context);
    g_test_add_func("/xbox/chardev/context/unsupported-custom",
                    test_unsupported_custom_context);
    int result = g_test_run();
    return result;
}

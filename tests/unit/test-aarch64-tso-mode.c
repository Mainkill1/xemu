/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "tcg/aarch64/tso-mode.h"

static void test_mode(void)
{
    const char *enabled[] = { "1", "on", "true", "yes" };
    const char *disabled[] = { NULL, "0",       "off", "false",      "no",
                               "",   "invalid", "2",   "true suffix" };

    for (size_t i = 0; i < G_N_ELEMENTS(enabled); i++) {
        g_assert_true(tcg_aarch64_tso_mode(enabled[i], true));
        g_assert_false(tcg_aarch64_tso_mode(enabled[i], false));
    }
    for (size_t i = 0; i < G_N_ELEMENTS(disabled); i++) {
        g_assert_false(tcg_aarch64_tso_mode(disabled[i], true));
        g_assert_false(tcg_aarch64_tso_mode(disabled[i], false));
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/aarch64/tso/opt-in-capability", test_mode);
    return g_test_run();
}

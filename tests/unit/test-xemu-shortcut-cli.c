/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "ui/xemu-shortcut-evidence.h"

static const char digest[] =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

static void test_consumes_and_owns(void)
{
    const char *input[] = {
        "xemu",
        "-machine",
        "xbox",
        "-xemu-tweak",
        "pgraph_bulk_packets=enabled",
        "-xemu-shortcut-evidence",
        "run.json",
        "-xemu-shortcut-session",
        "session-A1",
        "-xemu-shortcut-workload",
        "pgr2-fixed-input",
        "-xemu-shortcut-input-sha256",
        digest,
        "-xemu-shortcut-order-group",
        "group",
        "-xemu-shortcut-order",
        "ABBA",
        "-xemu-shortcut-position",
        "1",
        "-xemu-shortcut-start-frame",
        "1200",
        "-xemu-shortcut-frames",
        "3600",
        "-xemu-tweak",
        "pgraph_bulk_packets=disabled",
        "-m",
        "64",
    };
    char *argv[ARRAY_SIZE(input)];
    char *owned[ARRAY_SIZE(input)];
    for (size_t i = 0; i < ARRAY_SIZE(input); i++) {
        argv[i] = owned[i] = g_strdup(input[i]);
    }
    XemuShortcutEvidenceOptions options = {0};
    GArray *overrides = NULL;
    Error *error = NULL;
    g_assert_true(xemu_shortcut_evidence_parse_early(
        ARRAY_SIZE(argv), argv, &options, &overrides, &error));
    g_assert_null(error);
    g_assert_cmpstr(argv[1], ==, "-machine");
    g_assert_cmpstr(argv[2], ==, "xbox");
    g_assert_cmpstr(argv[25], ==, "-m");
    g_assert_cmpstr(argv[26], ==, "64");
    for (unsigned int i = 3; i < 25; i++) {
        g_assert_null(argv[i]);
    }
    for (size_t i = 0; i < ARRAY_SIZE(owned); i++) {
        memset(owned[i], 'x', strlen(owned[i]));
        g_free(owned[i]);
    }
    g_assert_cmpstr(options.output_path, ==, "run.json");
    g_assert_cmpstr(options.session_id, ==, "session-A1");
    g_assert_cmpstr(options.input_sha256, ==, digest);
    g_assert_cmpstr(options.order, ==, "ABBA");
    g_assert_cmpuint(options.position, ==, 1);
    g_assert_cmpuint(options.start_frame, ==, 1200);
    g_assert_cmpuint(options.frame_count, ==, 3600);
    g_assert_cmpuint(overrides->len, ==, 2);
    XemuTweakRequestedState request = {0};
    g_assert_true(xemu_tweak_apply_overrides(
        &request, (XemuTweakOverride *)overrides->data, overrides->len,
        &error));
    g_assert_cmpint(request.policy[XEMU_TWEAK_PGRAPH_BULK_PACKETS], ==,
                    XEMU_TWEAK_POLICY_DISABLED);
    g_array_unref(overrides);
    xemu_shortcut_evidence_options_clear(&options);
}

/* These fixtures mutate argv slots only; the owned-input test also exercises
 * mutable strings and frees them immediately after parsing. */
static void test_invalid_is_atomic(void)
{
    const char *tokens[] = {"unknown=enabled", "pgraph_bulk_packets=banana",
                            "pgraph_bulk_packets", "cache_shaders=", ""};
    for (size_t i = 0; i < ARRAY_SIZE(tokens); i++) {
        char *argv[] = {(char *)"xemu", (char *)"-m", (char *)"64",
                        (char *)"-xemu-tweak", (char *)tokens[i]};
        XemuShortcutEvidenceOptions options = {0};
        GArray *overrides = NULL;
        Error *error = NULL;
        g_assert_false(xemu_shortcut_evidence_parse_early(
            ARRAY_SIZE(argv), argv, &options, &overrides, &error));
        g_assert_nonnull(error);
        g_assert_nonnull(strstr(error_get_pretty(error), tokens[i]));
        g_assert_cmpstr(argv[3], ==, "-xemu-tweak");
        g_assert_cmpstr(argv[4], ==, tokens[i]);
        g_assert_null(options.output_path);
        g_assert_null(overrides);
        error_free(error);
    }
}

static void test_metadata_and_windows(void)
{
    const char *invalid[][2] = {
        {"-xemu-shortcut-order", "ABAB"},
        {"-xemu-shortcut-position", "0"},
        {"-xemu-shortcut-position", "5"},
        {"-xemu-shortcut-position", "1x"},
        {"-xemu-shortcut-frames", "0"},
        {"-xemu-shortcut-frames", "-1"},
        {"-xemu-shortcut-frames", "18446744073709551616"},
        {"-xemu-shortcut-start-frame", " 2"},
        {"-xemu-shortcut-input-sha256", "ABC"},
    };
    for (size_t i = 0; i < ARRAY_SIZE(invalid); i++) {
        char *argv[] = {
            (char *)"xemu",        (char *)"-xemu-shortcut-evidence",
            (char *)"run.json",    (char *)"-xemu-shortcut-session",
            (char *)"s",           (char *)"-xemu-shortcut-workload",
            (char *)"w",           (char *)"-xemu-shortcut-input-sha256",
            (char *)digest,        (char *)"-xemu-shortcut-order-group",
            (char *)"g",           (char *)"-xemu-shortcut-order",
            (char *)"BAAB",        (char *)"-xemu-shortcut-position",
            (char *)"2",           (char *)"-xemu-shortcut-start-frame",
            (char *)"0",           (char *)"-xemu-shortcut-frames",
            (char *)"32",          (char *)invalid[i][0],
            (char *)invalid[i][1],
        };
        XemuShortcutEvidenceOptions options = {0};
        GArray *overrides = NULL;
        Error *error = NULL;
        g_assert_false(xemu_shortcut_evidence_parse_early(
            ARRAY_SIZE(argv), argv, &options, &overrides, &error));
        g_assert_nonnull(error);
        g_assert_nonnull(strstr(error_get_pretty(error), invalid[i][1]));
        error_free(error);
    }
    char *missing[] = {(char *)"xemu", (char *)"-xemu-shortcut-evidence",
                       (char *)"run.json"};
    char *no_output[] = {(char *)"xemu", (char *)"-xemu-shortcut-session",
                         (char *)"s"};
    char *no_value[] = {(char *)"xemu", (char *)"-xemu-tweak"};
    char **cases[] = {missing, no_output, no_value};
    const int counts[] = {3, 3, 2};
    for (unsigned int i = 0; i < ARRAY_SIZE(cases); i++) {
        XemuShortcutEvidenceOptions options = {0};
        GArray *overrides = NULL;
        Error *error = NULL;
        g_assert_false(xemu_shortcut_evidence_parse_early(
            counts[i], cases[i], &options, &overrides, &error));
        g_assert_nonnull(error);
        g_assert_null(overrides);
        error_free(error);
    }
}

static void test_tweaks_without_capture(void)
{
    char *argv[] = {(char *)"xemu", (char *)"-xemu-tweak",
                    (char *)"cache_shaders=disabled", (char *)"-gpu",
                    (char *)"auto"};
    XemuShortcutEvidenceOptions options = {0};
    GArray *overrides = NULL;
    Error *error = NULL;
    g_assert_true(xemu_shortcut_evidence_parse_early(
        ARRAY_SIZE(argv), argv, &options, &overrides, &error));
    g_assert_null(error);
    g_assert_null(options.output_path);
    g_assert_cmpuint(overrides->len, ==, 1);
    g_assert_cmpstr(argv[3], ==, "-gpu");
    g_assert_cmpstr(argv[4], ==, "auto");
    g_array_unref(overrides);
    xemu_shortcut_evidence_options_clear(&options);
}

static void test_failure_preserves_existing_owned_outputs(void)
{
    char *argv[] = {(char *)"xemu",
                    (char *)"-xemu-shortcut-evidence",
                    (char *)"new.json",
                    (char *)"-xemu-tweak",
                    (char *)"cache_shaders=enabled",
                    (char *)"-xemu-tweak",
                    (char *)"unknown=enabled"};
    XemuShortcutEvidenceOptions options = {
        .output_path = g_strdup("existing.json"),
        .session_id = g_strdup("existing-session"),
        .position = 3,
        .start_frame = 101,
        .frame_count = 27,
    };
    XemuShortcutEvidenceOptions before = options;
    GArray *overrides = g_array_new(false, false, sizeof(XemuTweakOverride));
    XemuTweakOverride original;
    g_assert_true(xemu_tweak_parse_override("cache_shaders=disabled", &original,
                                            &error_abort));
    g_array_append_val(overrides, original);
    GArray *owned_array = overrides;
    Error *error = NULL;
    g_assert_false(xemu_shortcut_evidence_parse_early(
        ARRAY_SIZE(argv), argv, &options, &overrides, &error));
    g_assert_nonnull(error);
    g_assert_true(options.output_path == before.output_path);
    g_assert_true(options.session_id == before.session_id);
    g_assert_cmpuint(options.position, ==, 3);
    g_assert_cmpuint(options.start_frame, ==, 101);
    g_assert_cmpuint(options.frame_count, ==, 27);
    g_assert_true(overrides == owned_array);
    g_assert_cmpuint(overrides->len, ==, 1);
    g_assert_cmpint(g_array_index(overrides, XemuTweakOverride, 0).policy, ==,
                    XEMU_TWEAK_POLICY_DISABLED);
    g_assert_cmpstr(argv[1], ==, "-xemu-shortcut-evidence");
    g_assert_cmpstr(argv[5], ==, "-xemu-tweak");
    error_free(error);
    g_array_unref(overrides);
    xemu_shortcut_evidence_options_clear(&options);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xemu/shortcut-cli/owned-consumption",
                    test_consumes_and_owns);
    g_test_add_func("/xemu/shortcut-cli/invalid-atomic",
                    test_invalid_is_atomic);
    g_test_add_func("/xemu/shortcut-cli/metadata-window",
                    test_metadata_and_windows);
    g_test_add_func("/xemu/shortcut-cli/tweaks-alone",
                    test_tweaks_without_capture);
    g_test_add_func("/xemu/shortcut-cli/existing-owned-atomic",
                    test_failure_preserves_existing_owned_outputs);
    return g_test_run();
}

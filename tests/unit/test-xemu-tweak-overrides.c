/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "ui/xemu-tweak-overrides.h"

static XemuTweakOverride parse(const char *token)
{
    XemuTweakOverride result;
    Error *err = NULL;
    g_assert_true(xemu_tweak_parse_override(token, &result, &err));
    g_assert_null(err);
    return result;
}

static void test_valid_policies(void)
{
    const struct {
        const char *token;
        XemuTweakPolicy policy;
    } cases[] = {
        {"pgraph_bulk_packets=auto", XEMU_TWEAK_POLICY_AUTO},
        {"pgraph_bulk_packets=disabled", XEMU_TWEAK_POLICY_DISABLED},
        {"pgraph_bulk_packets=enabled", XEMU_TWEAK_POLICY_ENABLED},
    };
    for (size_t i = 0; i < G_N_ELEMENTS(cases); i++) {
        XemuTweakOverride result = parse(cases[i].token);
        g_assert_cmpint(result.kind, ==, XEMU_TWEAK_OVERRIDE_POLICY);
        g_assert_cmpint(result.tweak, ==, XEMU_TWEAK_PGRAPH_BULK_PACKETS);
        g_assert_cmpint(result.policy, ==, cases[i].policy);
    }
}

static void test_registry_application(void)
{
    const struct {
        const char *name;
        XemuTweak tweak;
    } cases[] = {
        {"cpu_saving_wait", XEMU_TWEAK_CPU_SAVING_WAIT},
        {"pgraph_bulk_packets", XEMU_TWEAK_PGRAPH_BULK_PACKETS},
        {"pgraph_fence_fastpath", XEMU_TWEAK_PGRAPH_FENCE_FASTPATH},
        {"vk_color_download_folding", XEMU_TWEAK_VK_COLOR_DOWNLOAD_FOLDING},
        {"vk_bounded_vertex_uploads", XEMU_TWEAK_VK_BOUNDED_VERTEX_UPLOADS},
        {"vk_vertex_copy_shortcuts", XEMU_TWEAK_VK_VERTEX_COPY_SHORTCUTS},
        {"vk_transient_buffer_growth", XEMU_TWEAK_VK_TRANSIENT_BUFFER_GROWTH},
        {"gl_native_s3tc", XEMU_TWEAK_GL_NATIVE_S3TC},
        {"vk_shader_fastpath", XEMU_TWEAK_VK_SHADER_FASTPATH},
        {"nv20_vertex_arithmetic", XEMU_TWEAK_NV20_VERTEX_ARITHMETIC},
    };
    g_assert_cmpuint(G_N_ELEMENTS(cases) + 1, ==, XEMU_TWEAK_COUNT);
    for (size_t i = 0; i < G_N_ELEMENTS(cases); i++) {
        char *token = g_strdup_printf("%s=disabled", cases[i].name);
        XemuTweakOverride override = parse(token);
        XemuTweakRequestedState requested = {0};
        Error *err = NULL;
        g_assert_cmpstr(xemu_tweak_name(cases[i].tweak), ==, cases[i].name);
        g_assert_true(
            xemu_tweak_apply_overrides(&requested, &override, 1, &err));
        g_assert_null(err);
        for (unsigned int j = 0; j < XEMU_TWEAK_COUNT; j++) {
            g_assert_cmpint(requested.policy[j], ==,
                            j == (unsigned int)cases[i].tweak
                                ? XEMU_TWEAK_POLICY_DISABLED
                                : XEMU_TWEAK_POLICY_AUTO);
        }
        g_free(token);
    }
    g_assert_null(xemu_tweak_name((XemuTweak)-1));
    g_assert_null(xemu_tweak_name(XEMU_TWEAK_COUNT));
}

static void test_invalid_tokens(void)
{
    const char *cases[] = {
        NULL,
        "",
        "pgraph_bulk_packets",
        "=auto",
        "pgraph_bulk_packets=",
        "pgraph_bulk_packets=off",
        "pgraph_bulk_packets=AUTO",
        "pgraph_bulk_packets=enabled=disabled",
        "unknown=auto",
        "tweaks.pgraph_bulk_packets=auto",
        "pgraph_bulk_packets =auto",
        "vk_ubershader_mode=auto",
        "vk_ubershader_mode=unknown",
        "cache_shaders=off",
    };
    for (size_t i = 0; i < G_N_ELEMENTS(cases); i++) {
        XemuTweakOverride output;
        memset(&output, 0xa5, sizeof(output));
        XemuTweakOverride before = output;
        Error *err = NULL;
        g_assert_false(xemu_tweak_parse_override(cases[i], &output, &err));
        g_assert_nonnull(err);
        const char *expected = cases[i] ? cases[i] : "(null)";
        g_assert_nonnull(strstr(error_get_pretty(err), expected));
        g_assert_cmpmem(&output, sizeof(output), &before, sizeof(before));
        error_free(err);
    }
}

static void test_mode_and_cache(void)
{
    const struct {
        const char *token;
        XemuVulkanUbershaderMode mode;
    } cases[] = {
        {"vk_ubershader_mode=off", XEMU_VK_UBERSHADER_OFF},
        {"vk_ubershader_mode=fallback", XEMU_VK_UBERSHADER_FALLBACK},
        {"vk_ubershader_mode=prewarm", XEMU_VK_UBERSHADER_PREWARM},
        {"vk_ubershader_mode=always", XEMU_VK_UBERSHADER_ALWAYS},
    };
    for (size_t i = 0; i < G_N_ELEMENTS(cases); i++) {
        XemuTweakOverride overrides[] = {
            parse(cases[i].token),
            parse("cache_shaders=disabled"),
        };
        XemuTweakRequestedState requested = {
            .cache_policy = XEMU_TWEAK_POLICY_ENABLED,
        };
        Error *err = NULL;
        g_assert_cmpint(overrides[0].kind, ==, XEMU_TWEAK_OVERRIDE_MODE);
        g_assert_cmpint(overrides[1].kind, ==, XEMU_TWEAK_OVERRIDE_CACHE);
        g_assert_true(xemu_tweak_apply_overrides(
            &requested, overrides, G_N_ELEMENTS(overrides), &err));
        g_assert_null(err);
        g_assert_cmpint(requested.ubershader_mode, ==, cases[i].mode);
        g_assert_cmpint(requested.cache_policy, ==, XEMU_TWEAK_POLICY_DISABLED);
    }
    const char *cache_tokens[] = {"cache_shaders=auto",
                                  "cache_shaders=enabled"};
    const XemuTweakPolicy cache_policies[] = {
        XEMU_TWEAK_POLICY_AUTO,
        XEMU_TWEAK_POLICY_ENABLED,
    };
    for (size_t i = 0; i < G_N_ELEMENTS(cache_tokens); i++) {
        XemuTweakOverride override = parse(cache_tokens[i]);
        g_assert_cmpint(override.policy, ==, cache_policies[i]);
    }
}

static void test_last_wins_copy(void)
{
    XemuTweakRequestedState saved = {
        .policy = {[XEMU_TWEAK_PGRAPH_BULK_PACKETS] =
                       XEMU_TWEAK_POLICY_ENABLED},
        .ubershader_mode = XEMU_VK_UBERSHADER_PREWARM,
        .cache_policy = XEMU_TWEAK_POLICY_ENABLED,
    };
    XemuTweakRequestedState requested = saved;
    XemuTweakOverride overrides[] = {
        parse("pgraph_bulk_packets=disabled"),
        parse("vk_ubershader_mode=off"),
        parse("cache_shaders=disabled"),
        parse("pgraph_bulk_packets=enabled"),
        parse("cache_shaders=auto"),
        parse("vk_ubershader_mode=fallback"),
        parse("pgraph_bulk_packets=auto"),
    };
    Error *err = NULL;
    g_assert_true(xemu_tweak_apply_overrides(&requested, overrides,
                                             G_N_ELEMENTS(overrides), &err));
    g_assert_null(err);
    g_assert_cmpint(requested.policy[XEMU_TWEAK_PGRAPH_BULK_PACKETS], ==,
                    XEMU_TWEAK_POLICY_AUTO);
    g_assert_cmpint(requested.ubershader_mode, ==, XEMU_VK_UBERSHADER_FALLBACK);
    g_assert_cmpint(requested.cache_policy, ==, XEMU_TWEAK_POLICY_AUTO);
    g_assert_cmpint(saved.policy[XEMU_TWEAK_PGRAPH_BULK_PACKETS], ==,
                    XEMU_TWEAK_POLICY_ENABLED);
    g_assert_cmpint(saved.ubershader_mode, ==, XEMU_VK_UBERSHADER_PREWARM);
    g_assert_cmpint(saved.cache_policy, ==, XEMU_TWEAK_POLICY_ENABLED);
    g_assert_true(xemu_tweak_apply_overrides(&requested, NULL, 0, &err));
    g_assert_null(err);
}

static void test_forged_records_atomic(void)
{
    const XemuTweakOverride invalid[] = {
        {.kind = (XemuTweakOverrideKind)99},
        {.kind = XEMU_TWEAK_OVERRIDE_POLICY, .tweak = XEMU_TWEAK_COUNT},
        {.kind = XEMU_TWEAK_OVERRIDE_POLICY, .tweak = (XemuTweak)-1},
        {.kind = XEMU_TWEAK_OVERRIDE_POLICY,
         .tweak = XEMU_TWEAK_VK_HYBRID_UBERSHADERS},
        {.kind = XEMU_TWEAK_OVERRIDE_POLICY,
         .tweak = XEMU_TWEAK_PGRAPH_BULK_PACKETS,
         .policy = (XemuTweakPolicy)99},
        {.kind = XEMU_TWEAK_OVERRIDE_CACHE, .policy = (XemuTweakPolicy)-1},
        {.kind = XEMU_TWEAK_OVERRIDE_MODE,
         .mode = (XemuVulkanUbershaderMode)99},
    };
    for (size_t i = 0; i < G_N_ELEMENTS(invalid); i++) {
        XemuTweakRequestedState requested = {0};
        XemuTweakRequestedState before = requested;
        XemuTweakOverride overrides[] = {
            parse("pgraph_bulk_packets=enabled"),
            invalid[i],
        };
        Error *err = NULL;
        g_assert_false(xemu_tweak_apply_overrides(
            &requested, overrides, G_N_ELEMENTS(overrides), &err));
        g_assert_nonnull(err);
        g_assert_cmpmem(&requested, sizeof(requested), &before, sizeof(before));
        error_free(err);
    }
    XemuTweakRequestedState requested = {0};
    Error *err = NULL;
    g_assert_false(xemu_tweak_apply_overrides(&requested, NULL, 1, &err));
    g_assert_nonnull(err);
    error_free(err);
}

static void test_derived_permission(void)
{
    const char *tokens[] = {
        "vk_hybrid_ubershaders=auto",
        "vk_hybrid_ubershaders=disabled",
        "vk_hybrid_ubershaders=enabled",
    };
    g_assert_cmpstr(xemu_tweak_name(XEMU_TWEAK_VK_HYBRID_UBERSHADERS), ==,
                    "vk_hybrid_ubershaders");
    for (size_t i = 0; i < G_N_ELEMENTS(tokens); i++) {
        XemuTweakOverride output;
        Error *err = NULL;
        g_assert_false(xemu_tweak_parse_override(tokens[i], &output, &err));
        g_assert_nonnull(err);
        g_assert_nonnull(strstr(error_get_pretty(err), tokens[i]));
        g_assert_nonnull(strstr(error_get_pretty(err), "vk_ubershader_mode"));
        error_free(err);
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xemu/tweaks/overrides/valid-policies",
                    test_valid_policies);
    g_test_add_func("/xemu/tweaks/overrides/registry-application",
                    test_registry_application);
    g_test_add_func("/xemu/tweaks/overrides/invalid-tokens",
                    test_invalid_tokens);
    g_test_add_func("/xemu/tweaks/overrides/mode-cache", test_mode_and_cache);
    g_test_add_func("/xemu/tweaks/overrides/last-wins-copy",
                    test_last_wins_copy);
    g_test_add_func("/xemu/tweaks/overrides/forged-atomic",
                    test_forged_records_atomic);
    g_test_add_func("/xemu/tweaks/overrides/derived-permission",
                    test_derived_permission);
    return g_test_run();
}

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "ui/xemu-tweak-overrides.h"

static const char *const tweak_names[] = {
    [XEMU_TWEAK_CPU_SAVING_WAIT] = "cpu_saving_wait",
    [XEMU_TWEAK_PGRAPH_BULK_PACKETS] = "pgraph_bulk_packets",
    [XEMU_TWEAK_PGRAPH_FENCE_FASTPATH] = "pgraph_fence_fastpath",
    [XEMU_TWEAK_VK_COLOR_DOWNLOAD_FOLDING] = "vk_color_download_folding",
    [XEMU_TWEAK_VK_BOUNDED_VERTEX_UPLOADS] = "vk_bounded_vertex_uploads",
    [XEMU_TWEAK_VK_VERTEX_COPY_SHORTCUTS] = "vk_vertex_copy_shortcuts",
    [XEMU_TWEAK_VK_TRANSIENT_BUFFER_GROWTH] = "vk_transient_buffer_growth",
    [XEMU_TWEAK_GL_NATIVE_S3TC] = "gl_native_s3tc",
    [XEMU_TWEAK_VK_HYBRID_UBERSHADERS] = "vk_hybrid_ubershaders",
    [XEMU_TWEAK_VK_SHADER_FASTPATH] = "vk_shader_fastpath",
    [XEMU_TWEAK_NV20_VERTEX_ARITHMETIC] = "nv20_vertex_arithmetic",
    [XEMU_TWEAK_VK_SKIP_CLEAN_TEXTURE_STAGES] = "vk_skip_clean_texture_stages",
};
G_STATIC_ASSERT(G_N_ELEMENTS(tweak_names) == XEMU_TWEAK_COUNT);

const char *xemu_tweak_name(XemuTweak tweak)
{
    return (unsigned int)tweak < XEMU_TWEAK_COUNT ? tweak_names[tweak] : NULL;
}

bool xemu_tweak_parse_override(const char *text, XemuTweakOverride *out,
                               Error **errp)
{
    const char *equals = text ? strchr(text, '=') : NULL;
    const char *reason = "expected KEY=VALUE";
    XemuTweakOverride result = {0};
    if (!out || !equals || equals == text || !equals[1] ||
        strchr(equals + 1, '=')) {
        goto invalid;
    }

    size_t key_length = equals - text;
    const char *value = equals + 1;
    if (key_length == strlen("vk_ubershader_mode") &&
        !memcmp(text, "vk_ubershader_mode", key_length)) {
        const struct {
            const char *name;
            XemuVulkanUbershaderMode mode;
        } modes[] = {
            {"off", XEMU_VK_UBERSHADER_OFF},
            {"fallback", XEMU_VK_UBERSHADER_FALLBACK},
            {"prewarm", XEMU_VK_UBERSHADER_PREWARM},
            {"always", XEMU_VK_UBERSHADER_ALWAYS},
        };
        for (size_t i = 0; i < G_N_ELEMENTS(modes); i++) {
            if (!strcmp(value, modes[i].name)) {
                result.kind = XEMU_TWEAK_OVERRIDE_MODE;
                result.mode = modes[i].mode;
                *out = result;
                return true;
            }
        }
        reason = "mode must be off, fallback, prewarm or always";
        goto invalid;
    }

    if (key_length == strlen("cache_shaders") &&
        !memcmp(text, "cache_shaders", key_length)) {
        result.kind = XEMU_TWEAK_OVERRIDE_CACHE;
    } else {
        result.kind = XEMU_TWEAK_OVERRIDE_POLICY;
        result.tweak = XEMU_TWEAK_COUNT;
        for (unsigned int i = 0; i < XEMU_TWEAK_COUNT; i++) {
            if (strlen(tweak_names[i]) == key_length &&
                !memcmp(text, tweak_names[i], key_length)) {
                result.tweak = (XemuTweak)i;
                break;
            }
        }
        if (result.tweak == XEMU_TWEAK_COUNT) {
            reason = "unknown key";
            goto invalid;
        }
        if (result.tweak == XEMU_TWEAK_VK_HYBRID_UBERSHADERS) {
            reason = "derived permission; use vk_ubershader_mode";
            goto invalid;
        }
    }
    if (!strcmp(value, "auto")) {
        result.policy = XEMU_TWEAK_POLICY_AUTO;
    } else if (!strcmp(value, "disabled")) {
        result.policy = XEMU_TWEAK_POLICY_DISABLED;
    } else if (!strcmp(value, "enabled")) {
        result.policy = XEMU_TWEAK_POLICY_ENABLED;
    } else {
        reason = "policy must be auto, disabled or enabled";
        goto invalid;
    }
    *out = result;
    return true;

invalid:
    error_setg(errp, "Invalid tweak override '%s': %s", text ? text : "(null)",
               reason);
    return false;
}

static bool override_valid(const XemuTweakOverride *override)
{
    switch (override->kind) {
    case XEMU_TWEAK_OVERRIDE_POLICY:
        return (unsigned int) override->tweak < XEMU_TWEAK_COUNT &&
               override->tweak != XEMU_TWEAK_VK_HYBRID_UBERSHADERS &&
               (unsigned int) override->policy <= XEMU_TWEAK_POLICY_ENABLED;
    case XEMU_TWEAK_OVERRIDE_CACHE:
        return (unsigned int) override->policy <= XEMU_TWEAK_POLICY_ENABLED;
    case XEMU_TWEAK_OVERRIDE_MODE:
        return (unsigned int) override->mode <= XEMU_VK_UBERSHADER_ALWAYS;
    default:
        return false;
    }
}

bool xemu_tweak_apply_overrides(XemuTweakRequestedState *requested,
                                const XemuTweakOverride *overrides,
                                size_t count, Error **errp)
{
    if (!requested || (!overrides && count)) {
        error_setg(errp, "Invalid tweak override span or output");
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        if (!override_valid(&overrides[i])) {
            error_setg(errp, "Invalid tweak override record at index %zu", i);
            return false;
        }
    }
    XemuTweakRequestedState result = *requested;
    for (size_t i = 0; i < count; i++) {
        switch (overrides[i].kind) {
        case XEMU_TWEAK_OVERRIDE_POLICY:
            result.policy[overrides[i].tweak] = overrides[i].policy;
            break;
        case XEMU_TWEAK_OVERRIDE_MODE:
            result.ubershader_mode = overrides[i].mode;
            break;
        case XEMU_TWEAK_OVERRIDE_CACHE:
            result.cache_policy = overrides[i].policy;
            break;
        default:
            g_assert_not_reached();
        }
    }
    *requested = result;
    return true;
}

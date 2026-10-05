/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef UI_XEMU_TWEAK_OVERRIDES_H
#define UI_XEMU_TWEAK_OVERRIDES_H

#include "ui/xemu-tweaks.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "qapi/error.h"

typedef enum XemuTweakOverrideKind {
    XEMU_TWEAK_OVERRIDE_POLICY,
    XEMU_TWEAK_OVERRIDE_MODE,
    XEMU_TWEAK_OVERRIDE_CACHE,
} XemuTweakOverrideKind;

typedef struct XemuTweakOverride {
    XemuTweakOverrideKind kind;
    XemuTweak tweak;
    XemuTweakPolicy policy;
    XemuVulkanUbershaderMode mode;
} XemuTweakOverride;

/* Immutable canonical keys; NULL for an invalid enum. */
const char *xemu_tweak_name(XemuTweak tweak);

/* Pure; failure leaves output unchanged and reports the offending token. */
bool xemu_tweak_parse_override(const char *text, XemuTweakOverride *out,
                               Error **errp);
/* Pure; last wins. Invalid input leaves the complete request unchanged. */
bool xemu_tweak_apply_overrides(XemuTweakRequestedState *requested,
                                const XemuTweakOverride *overrides,
                                size_t count, Error **errp);
/* Own a copy before startup; immutable once workers may consume it. */
bool xemu_tweaks_set_overrides(const XemuTweakOverride *overrides, size_t count,
                               Error **errp);

#ifdef __cplusplus
}
#endif
#endif

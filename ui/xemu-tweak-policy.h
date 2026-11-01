/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_TWEAK_POLICY_H
#define XEMU_TWEAK_POLICY_H

#include <stdbool.h>
#include <stddef.h>

typedef enum XemuTweakPolicy {
    XEMU_TWEAK_POLICY_AUTO,
    XEMU_TWEAK_POLICY_DISABLED,
    /* Permit the shortcut only when its existing correctness guards pass. */
    XEMU_TWEAK_POLICY_ENABLED,
} XemuTweakPolicy;

typedef enum XemuTweakAvailability {
    XEMU_TWEAK_AVAILABLE,
    XEMU_TWEAK_UNSUPPORTED_BACKEND,
    XEMU_TWEAK_UNSUPPORTED_PLATFORM,
    XEMU_TWEAK_UNSUPPORTED_CAPABILITY,
    XEMU_TWEAK_BLOCKED_DEPENDENCY,
} XemuTweakAvailability;

typedef struct XemuTweakPolicyResolution {
    XemuTweakPolicy requested;
    bool selected;
    bool effective;
    bool restart_pending;
    XemuTweakAvailability availability;
    const char *reason;
} XemuTweakPolicyResolution;

static inline XemuTweakPolicyResolution xemu_tweak_policy_resolve(
    XemuTweakPolicy requested, bool auto_enabled, bool active_selected,
    bool apply_now, bool restart_latched, XemuTweakAvailability availability)
{
    XemuTweakPolicyResolution state = { requested, false,        false,
                                        false,     availability, NULL };
    if (requested < XEMU_TWEAK_POLICY_AUTO ||
        requested > XEMU_TWEAK_POLICY_ENABLED) {
        state.reason = "Unknown Advanced policy.";
        return state;
    }

    bool desired = requested == XEMU_TWEAK_POLICY_ENABLED ||
                   (requested == XEMU_TWEAK_POLICY_AUTO && auto_enabled);
    state.selected = apply_now && !restart_latched ? desired : active_selected;
    state.effective = state.selected && availability == XEMU_TWEAK_AVAILABLE;
    state.restart_pending = restart_latched && desired != active_selected;

    switch (availability) {
    case XEMU_TWEAK_UNSUPPORTED_BACKEND:
        state.reason = "Unavailable with the active renderer.";
        break;
    case XEMU_TWEAK_UNSUPPORTED_PLATFORM:
        state.reason = "Unavailable on this host platform.";
        break;
    case XEMU_TWEAK_UNSUPPORTED_CAPABILITY:
        state.reason = "Required host capability is unavailable.";
        break;
    case XEMU_TWEAK_BLOCKED_DEPENDENCY:
        state.reason = "A required Advanced setting is inactive.";
        break;
    default:
        state.reason = state.restart_pending ?
                           "Restart xemu to apply the saved choice." :
                       state.effective ? "Active for eligible work." :
                                         "Disabled.";
        break;
    }
    return state;
}

#endif

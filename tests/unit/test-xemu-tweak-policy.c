/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <string.h>

#include "ui/xemu-tweak-policy.h"

static void test_auto_and_explicit_modes(void)
{
    XemuTweakPolicyResolution state = xemu_tweak_policy_resolve(
        XEMU_TWEAK_POLICY_AUTO, true, false, true, false, XEMU_TWEAK_AVAILABLE);
    assert(state.selected && state.effective && !state.restart_pending);
    assert(state.requested == XEMU_TWEAK_POLICY_AUTO);

    state = xemu_tweak_policy_resolve(XEMU_TWEAK_POLICY_AUTO, false, true, true,
                                      false, XEMU_TWEAK_AVAILABLE);
    assert(!state.selected && !state.effective);

    state = xemu_tweak_policy_resolve(XEMU_TWEAK_POLICY_DISABLED, true, true,
                                      true, false, XEMU_TWEAK_AVAILABLE);
    assert(!state.selected && !state.effective);

    state = xemu_tweak_policy_resolve(XEMU_TWEAK_POLICY_ENABLED, false, false,
                                      true, false, XEMU_TWEAK_AVAILABLE);
    assert(state.selected && state.effective);
}

static void test_unavailable_and_restart_latched(void)
{
    XemuTweakPolicyResolution state =
        xemu_tweak_policy_resolve(XEMU_TWEAK_POLICY_ENABLED, false, false, true,
                                  false, XEMU_TWEAK_UNSUPPORTED_BACKEND);
    assert(state.selected && !state.effective);
    assert(state.availability == XEMU_TWEAK_UNSUPPORTED_BACKEND);
    assert(state.reason && strstr(state.reason, "renderer"));

    state =
        xemu_tweak_policy_resolve(XEMU_TWEAK_POLICY_ENABLED, false, true, true,
                                  false, XEMU_TWEAK_BLOCKED_DEPENDENCY);
    assert(state.selected && !state.effective);
    assert(state.reason && strstr(state.reason, "Advanced setting"));

    state = xemu_tweak_policy_resolve(XEMU_TWEAK_POLICY_DISABLED, true, true,
                                      true, true, XEMU_TWEAK_AVAILABLE);
    assert(state.selected && state.effective && state.restart_pending);
    assert(state.reason && strstr(state.reason, "Restart"));

    state = xemu_tweak_policy_resolve(XEMU_TWEAK_POLICY_ENABLED, false, false,
                                      true, true, XEMU_TWEAK_AVAILABLE);
    assert(!state.selected && !state.effective && state.restart_pending);
}

static void test_status_observes_installed_state(void)
{
    XemuTweakPolicyResolution state =
        xemu_tweak_policy_resolve(XEMU_TWEAK_POLICY_ENABLED, false, false,
                                  false, false, XEMU_TWEAK_AVAILABLE);
    assert(!state.selected && !state.effective);
    assert(!state.restart_pending);
}

static void test_invalid_mode_fails_closed(void)
{
    XemuTweakPolicyResolution state = xemu_tweak_policy_resolve(
        (XemuTweakPolicy)99, true, false, true, false, XEMU_TWEAK_AVAILABLE);
    assert(!state.selected && !state.effective);
    assert(state.reason && strstr(state.reason, "Unknown"));
}

int main(void)
{
    test_auto_and_explicit_modes();
    test_unavailable_and_restart_latched();
    test_status_observes_installed_state();
    test_invalid_mode_fails_closed();
    return 0;
}

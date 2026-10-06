/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"

#include "ui/xemu-tweak-policy.h"
#include "ui/xemu-tweaks.h"

XemuTweakBits xemu_tweaks_active;

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

static void test_hot_reader_rejects_invalid_setting(void)
{
    static const int invalid[] = { XEMU_TWEAK_COUNT, 63, 64, -1, INT_MAX };

    qatomic_set_u64(&xemu_tweaks_active, UINT64_MAX);
    for (unsigned int i = 0; i < ARRAY_SIZE(invalid); i++) {
        assert(!xemu_tweak_enabled((XemuTweak)invalid[i]));
    }
}

static void test_hot_reader_preserves_valid_permissions(void)
{
    for (unsigned int i = 0; i < XEMU_TWEAK_COUNT; i++) {
        qatomic_set_u64(&xemu_tweaks_active, UINT64_C(1) << i);
        for (unsigned int j = 0; j < XEMU_TWEAK_COUNT; j++) {
            assert(xemu_tweak_enabled((XemuTweak)j) == (i == j));
        }
    }
    qatomic_set_u64(&xemu_tweaks_active, 0);
}

int main(void)
{
    test_auto_and_explicit_modes();
    test_unavailable_and_restart_latched();
    test_status_observes_installed_state();
    test_invalid_mode_fails_closed();
    test_hot_reader_rejects_invalid_setting();
    test_hot_reader_preserves_valid_permissions();
    return 0;
}

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "xemu-tweaks.h"

bool xemu_vulkan_ubershader_mode_selectable(XemuVulkanUbershaderMode mode)
{
    return mode == XEMU_VK_UBERSHADER_OFF ||
           mode == XEMU_VK_UBERSHADER_FALLBACK ||
           mode == XEMU_VK_UBERSHADER_PREWARM ||
           mode == XEMU_VK_UBERSHADER_ALWAYS;
}

static bool recommended_policy(XemuTweak tweak)
{
    switch (tweak) {
    case XEMU_TWEAK_CPU_SAVING_WAIT:
    case XEMU_TWEAK_PGRAPH_BULK_PACKETS:
    case XEMU_TWEAK_PGRAPH_FENCE_FASTPATH:
    case XEMU_TWEAK_VK_COLOR_DOWNLOAD_FOLDING:
    case XEMU_TWEAK_VK_BOUNDED_VERTEX_UPLOADS:
    case XEMU_TWEAK_VK_VERTEX_COPY_SHORTCUTS:
    case XEMU_TWEAK_VK_TRANSIENT_BUFFER_GROWTH:
    case XEMU_TWEAK_GL_NATIVE_S3TC:
    case XEMU_TWEAK_VK_SHADER_FASTPATH:
    case XEMU_TWEAK_VK_SKIP_CLEAN_TEXTURE_STAGES:
        return true;
    default:
        return false;
    }
}

static XemuVulkanUbershaderRuntimeState
resolve_ubershader(const XemuTweakRequestedState *requested,
                   const XemuTweakEnvironment *env,
                   const XemuTweakResolution *startup, bool relatch)
{
    bool valid =
        xemu_vulkan_ubershader_mode_selectable(requested->ubershader_mode);
    XemuVulkanUbershaderMode policy = !relatch && startup ?
                                          startup->ubershader.policy :
                                      valid ? requested->ubershader_mode :
                                              XEMU_VK_UBERSHADER_OFF;
    bool installed = env->renderer == XEMU_TWEAK_RENDERER_VULKAN &&
                     env->ubershader_installed;
    bool operational =
        policy == XEMU_VK_UBERSHADER_OFF || env->ubershader_operational;
    XemuVulkanUbershaderRuntimeState state = {
        .requested = requested->ubershader_mode,
        .policy = policy,
        .active = installed && operational ? policy : XEMU_VK_UBERSHADER_OFF,
        .available = installed && operational && valid,
        .restart_pending = valid && requested->ubershader_mode != policy,
    };
    if (!valid) {
        state.reason = "Unknown Vulkan ubershader mode.";
    } else if (!installed) {
        state.reason = "Available when the Vulkan renderer is installed.";
    } else if (!operational) {
        state.reason =
            "Ubershader setup failed; Vulkan is using specialized shaders.";
    } else {
        state.reason = "Active.";
    }
    return state;
}

static XemuTweakAvailability
availability(XemuTweak tweak, const XemuTweakEnvironment *env,
             const XemuVulkanUbershaderRuntimeState *ubershader)
{
    bool graphics = env->renderer == XEMU_TWEAK_RENDERER_OPENGL ||
                    env->renderer == XEMU_TWEAK_RENDERER_VULKAN;
    switch (tweak) {
    case XEMU_TWEAK_CPU_SAVING_WAIT:
        return env->windows_host ? XEMU_TWEAK_AVAILABLE :
                                   XEMU_TWEAK_UNSUPPORTED_PLATFORM;
    case XEMU_TWEAK_PGRAPH_BULK_PACKETS:
    case XEMU_TWEAK_PGRAPH_FENCE_FASTPATH:
    case XEMU_TWEAK_ISSUE149_EFFECT_SUPPRESSION:
    case XEMU_TWEAK_NV20_VERTEX_ARITHMETIC:
        return graphics ? XEMU_TWEAK_AVAILABLE : XEMU_TWEAK_UNSUPPORTED_BACKEND;
    case XEMU_TWEAK_GL_NATIVE_S3TC:
        /* Successful GL installation already requires native S3TC support. */
        return env->renderer == XEMU_TWEAK_RENDERER_OPENGL ?
                   XEMU_TWEAK_AVAILABLE :
                   XEMU_TWEAK_UNSUPPORTED_BACKEND;
    case XEMU_TWEAK_VK_HYBRID_UBERSHADERS:
        return env->renderer != XEMU_TWEAK_RENDERER_VULKAN ?
                   XEMU_TWEAK_UNSUPPORTED_BACKEND :
               ubershader->available ? XEMU_TWEAK_AVAILABLE :
                                       XEMU_TWEAK_UNSUPPORTED_CAPABILITY;
    case XEMU_TWEAK_VK_SHADER_FASTPATH:
        return env->renderer != XEMU_TWEAK_RENDERER_VULKAN ?
                   XEMU_TWEAK_UNSUPPORTED_BACKEND :
               ubershader->active != XEMU_VK_UBERSHADER_OFF ?
                   XEMU_TWEAK_AVAILABLE :
                   XEMU_TWEAK_BLOCKED_DEPENDENCY;
    default:
        return env->renderer == XEMU_TWEAK_RENDERER_VULKAN ?
                   XEMU_TWEAK_AVAILABLE :
                   XEMU_TWEAK_UNSUPPORTED_BACKEND;
    }
}

XemuTweakResolution
xemu_tweaks_resolve(const XemuTweakRequestedState *requested,
                    const XemuTweakEnvironment *environment,
                    const XemuTweakResolution *startup_state,
                    bool apply_restart_latched)
{
    assert(requested && environment);
    XemuTweakEnvironment env = *environment;
    if ((unsigned int)env.renderer > XEMU_TWEAK_RENDERER_VULKAN) {
        env.renderer = XEMU_TWEAK_RENDERER_NONE;
    }
    XemuTweakResolution result = {
        .renderer = env.renderer,
        .ubershader = resolve_ubershader(requested, &env, startup_state,
                                         apply_restart_latched),
    };
    for (unsigned int i = 0; i < XEMU_TWEAK_COUNT; i++) {
        uint64_t bit = UINT64_C(1) << i;
        XemuTweakPolicy policy = requested->policy[i];
        bool recommended = recommended_policy(i);
        if (i == XEMU_TWEAK_VK_HYBRID_UBERSHADERS) {
            policy = requested->ubershader_mode != XEMU_VK_UBERSHADER_OFF ?
                         XEMU_TWEAK_POLICY_ENABLED :
                         XEMU_TWEAK_POLICY_DISABLED;
        }
        XemuTweakAvailability available =
            availability(i, &env, &result.ubershader);
        XemuTweakPolicyResolution resolved = xemu_tweak_policy_resolve(
            policy, recommended,
            startup_state && (startup_state->selected_bits & bit), true,
            startup_state && !apply_restart_latched &&
                xemu_tweak_requires_restart(i),
            available);
        XemuTweakRuntimeState *state = &result.state[i];
        state->requested = policy == XEMU_TWEAK_POLICY_ENABLED ||
                           (policy == XEMU_TWEAK_POLICY_AUTO && recommended);
        state->policy_requested = policy;
        state->selected = resolved.selected;
        state->effective = resolved.effective;
        state->restart_pending = resolved.restart_pending;
        state->available = available == XEMU_TWEAK_AVAILABLE;
        state->availability = available;
        state->reason = resolved.reason;
        if (i == XEMU_TWEAK_VK_HYBRID_UBERSHADERS) {
            state->selected =
                result.ubershader.policy != XEMU_VK_UBERSHADER_OFF;
            state->effective = state->selected && state->available;
            state->restart_pending = result.ubershader.restart_pending;
            if (!state->available) {
                state->reason = result.ubershader.reason;
            }
        } else if (policy == XEMU_TWEAK_POLICY_AUTO && state->available &&
                   !state->restart_pending) {
            state->reason = state->effective ?
                                "Recommended policy selected." :
                                "Disabled by recommended policy.";
        }
        if (state->selected) {
            result.selected_bits |= bit;
        }
        if (state->effective) {
            result.effective_bits |= bit;
        }
    }
    return result;
}

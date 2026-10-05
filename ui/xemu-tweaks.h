/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_TWEAKS_H
#define XEMU_TWEAKS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qemu/atomic.h"
#include "xemu-tweak-policy.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum XemuTweak {
    XEMU_TWEAK_CPU_SAVING_WAIT,
    XEMU_TWEAK_PGRAPH_BULK_PACKETS,
    XEMU_TWEAK_PGRAPH_FENCE_FASTPATH,
    XEMU_TWEAK_VK_COLOR_DOWNLOAD_FOLDING,
    XEMU_TWEAK_VK_BOUNDED_VERTEX_UPLOADS,
    XEMU_TWEAK_VK_VERTEX_COPY_SHORTCUTS,
    XEMU_TWEAK_VK_TRANSIENT_BUFFER_GROWTH,
    XEMU_TWEAK_GL_NATIVE_S3TC,
    XEMU_TWEAK_VK_HYBRID_UBERSHADERS,
    XEMU_TWEAK_VK_SHADER_FASTPATH,
    XEMU_TWEAK_NV20_VERTEX_ARITHMETIC,
    XEMU_TWEAK_COUNT,
} XemuTweak;

#ifdef __cplusplus
static_assert(XEMU_TWEAK_COUNT <= 64,
              "Advanced tweak mask is full");
#else
_Static_assert(XEMU_TWEAK_COUNT <= 64,
               "Advanced tweak mask is full");
#endif

typedef aligned_uint64_t XemuTweakBits;

typedef enum XemuTweakRenderer {
    XEMU_TWEAK_RENDERER_NONE,
    XEMU_TWEAK_RENDERER_OPENGL,
    XEMU_TWEAK_RENDERER_VULKAN,
} XemuTweakRenderer;

typedef struct XemuTweakRuntimeState {
    bool requested;
    bool selected;
    bool effective;
    bool available;
    bool restart_pending;
    bool overridden;
    XemuTweakPolicy policy_requested;
    XemuTweakAvailability availability;
    const char *reason;
} XemuTweakRuntimeState;

typedef enum XemuVulkanUbershaderMode {
    XEMU_VK_UBERSHADER_OFF = 0,
    XEMU_VK_UBERSHADER_FALLBACK,
    XEMU_VK_UBERSHADER_PREWARM,
    XEMU_VK_UBERSHADER_ALWAYS,
} XemuVulkanUbershaderMode;

typedef struct XemuVulkanUbershaderRuntimeState {
    XemuVulkanUbershaderMode requested;
    XemuVulkanUbershaderMode policy;
    XemuVulkanUbershaderMode active;
    bool available;
    bool restart_pending;
    bool overridden;
    const char *reason;
} XemuVulkanUbershaderRuntimeState;

typedef struct XemuTweakRequestedState {
    XemuTweakPolicy policy[XEMU_TWEAK_COUNT];
    /* The hybrid Boolean permission is derived from this mode. */
    XemuVulkanUbershaderMode ubershader_mode;
    /* Persistent-cache policy is independent of the hot permission mask. */
    XemuTweakPolicy cache_policy;
} XemuTweakRequestedState;

typedef struct XemuTweakEnvironment {
    XemuTweakRenderer renderer;
    bool windows_host;
    bool ubershader_installed;
    bool ubershader_operational;
    /* Owner publication describes initialized cache permission, not a hit or
     * proof that any persistent artifact exists or was accepted. */
    XemuTweakRenderer cache_renderer;
    bool cache_installed;
    bool cache_session_eligible;
} XemuTweakEnvironment;

typedef struct XemuTweakResolution {
    uint64_t selected_bits;
    uint64_t effective_bits;
    /* Zero for a pure resolution; publication assigns a sequence. */
    uint64_t sequence;
    XemuTweakRenderer renderer;
    XemuTweakRuntimeState state[XEMU_TWEAK_COUNT];
    XemuVulkanUbershaderRuntimeState ubershader;
    XemuTweakRuntimeState cache;
    bool cache_installed;
    bool cache_session_eligible;
} XemuTweakResolution;

/* Pure: no configuration reads, publication or platform adapter calls.
 * Startup selections survive backend unavailability and live edits. */
XemuTweakResolution
xemu_tweaks_resolve(const XemuTweakRequestedState *requested,
                    const XemuTweakEnvironment *environment,
                    const XemuTweakResolution *startup_state,
                    bool apply_restart_latched);

/* Workers read complete effective permissions, never mutable configuration.
 * Existing correctness guards still decide whether eligible work can skip. */
extern XemuTweakBits xemu_tweaks_active;

/* C++ cannot expand qatomic_read_u64's C-only _Generic expression. */
uint64_t xemu_tweaks_active_snapshot(void);

static inline bool xemu_tweak_enabled(XemuTweak tweak)
{
#ifdef __cplusplus
    return (xemu_tweaks_active_snapshot() & (UINT64_C(1) << tweak)) != 0;
#else
    return (qatomic_read_u64(&xemu_tweaks_active) &
            (UINT64_C(1) << tweak)) != 0;
#endif
}

static inline bool xemu_tweak_requires_restart(XemuTweak tweak)
{
    return tweak == XEMU_TWEAK_VK_TRANSIENT_BUFFER_GROWTH ||
           tweak == XEMU_TWEAK_GL_NATIVE_S3TC ||
           tweak == XEMU_TWEAK_VK_HYBRID_UBERSHADERS;
}

/* UI thread only. startup=true is only valid before workers are created. */
void xemu_tweaks_apply(bool startup);
/* Atomic owned request; usable by initialization before renderer publication.
 * Actual cache users retain their own capability/session correctness guards. */
bool xemu_tweaks_cache_requested_enabled(void);
/* Low-frequency owner lifecycle; renderer tag prevents stale backend state. */
void xemu_tweaks_publish_cache(XemuTweakRenderer owner, bool installed,
                              bool session_eligible);
/* Lifecycle and status APIs are synchronized and low-frequency, never hot-path.
 * Returned values own one published generation; reasons have static lifetime.
 */
void xemu_tweaks_publish_renderer(XemuTweakRenderer renderer);
XemuTweakResolution xemu_tweaks_snapshot(void);
XemuTweakRuntimeState xemu_tweak_runtime_state(XemuTweak tweak);
/* Low-frequency; never called from a worker/hot path. Returns the complete
 * length excluding NUL, like snprintf. NULL is allowed only with size == 0.
 * Output is a diagnostic profile, not the complete #94 session artifact. */
size_t xemu_tweaks_format_effective_profile(char *buffer, size_t size);
XemuVulkanUbershaderMode xemu_vulkan_ubershader_migrate_mode(
    bool mode_present, XemuVulkanUbershaderMode mode, bool legacy_enabled);
bool xemu_vulkan_ubershader_mode_selectable(XemuVulkanUbershaderMode mode);
XemuVulkanUbershaderMode xemu_vulkan_ubershader_policy(void);
XemuVulkanUbershaderRuntimeState
xemu_vulkan_ubershader_runtime_state(void);
/* Renderer lifecycle publication; never called from the draw path. */
void xemu_vulkan_ubershader_publish_runtime(
    bool vulkan_installed, bool ubershader_operational);

#ifdef __cplusplus
}
#endif
#endif

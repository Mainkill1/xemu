/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_DLSS_CONFIG_HH
#define XEMU_DLSS_CONFIG_HH

#include "xemu-settings.h"
#include "xemu-dlss.h"
#include <SDL3/SDL_filesystem.h>
#include <cstdlib>

/* Used only by the settings/UI thread. The renderer reads the owned launch
 * snapshot, never mutable g_config strings or process environment variables. */
static inline void xemu_dlss_apply_settings(bool startup)
{
    static_assert(int(CONFIG_DISPLAY_DLSS_MODE_OFF) == XEMU_DLSS_OFF);
    static_assert(int(CONFIG_DISPLAY_DLSS_MODE_VALIDATION) == XEMU_DLSS_VALIDATION);
    static_assert(int(CONFIG_DISPLAY_DLSS_MODE_NEURAL_RENDERING) == XEMU_DLSS_NEURAL_RENDERING);
    XemuDLSSRequest request{
        int(g_config.display.dlss.mode),
        int(g_config.display.dlss.adapter_selection),
        int(g_config.display.dlss.processing_resolution),
        g_config.display.dlss.adapter_path,
    };
    if (startup) {
        XemuDLSSEnvironment environment{
            std::getenv("XEMU_EXPERIMENTAL_NEURAL_PRESENT"),
            std::getenv("XEMU_NEURAL_PRESENT_PLUGIN"),
            std::getenv("XEMU_NEURAL_PRESENT_WORK_WIDTH"),
            std::getenv("XEMU_NEURAL_PRESENT_WORK_HEIGHT"),
        };
#ifdef _WIN32
        const bool windows_host = true;
#else
        const bool windows_host = false;
#endif
        xemu_dlss_initialize(&request, &environment, SDL_GetBasePath(), windows_host);
    } else {
        xemu_dlss_set_requested(&request);
    }
}
#endif

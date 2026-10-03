/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "common.hh"
#include "main-menu.hh"
#include "widgets.hh"
#include "../xemu-dlss-config.hh"
#include "../xemu-notifications.h"
#include "hw/xbox/nv2a/pgraph/neural-present-state.h"

static void DLSSSettingsChanged()
{
    xemu_dlss_apply_settings(false);
    xemu_settings_save();
}

void MainMenuDLSSView::Draw()
{
    SectionTitle("DLSS");
    ImGui::TextWrapped(
        "Experimental host-side presentation. This draft includes validation "
        "adapters, not working Neural Rendering. RTX 3090 Neural Rendering "
        "must be verified with the selected runtime.");

    SectionTitle("Configuration");
    if (ChevronCombo("Mode", &g_config.display.dlss.mode,
                     "Off\0Adapter Validation\0Neural Rendering (experimental)\0",
                     "Validation runs a declared validation-only adapter. "
                     "Neural Rendering remains inactive until genuine NR "
                     "output is produced. Changes require restarting xemu.")) {
        DLSSSettingsChanged();
    }
    if (ChevronCombo("Adapter", &g_config.display.dlss.adapter_selection,
                     "Auto-detect\0Custom DLL\0",
                     "Auto-detect uses xemu-neural-present.dll beside the "
                     "xemu executable. No DLL is downloaded or loaded by this page.")) {
        DLSSSettingsChanged();
    }
    if (g_config.display.dlss.adapter_selection == CONFIG_DISPLAY_DLSS_ADAPTER_SELECTION_CUSTOM) {
        static const SDL_DialogFileFilter filters[] = {
            { "Adapter DLL", "dll" },
        };
        FilePicker("Adapter DLL", g_config.display.dlss.adapter_path,
                   filters, 1, false, [](const char *path) {
                       xemu_settings_set_string(&g_config.display.dlss.adapter_path, path);
                       DLSSSettingsChanged();
                   });
        if (ImGui::Button("Clear adapter path")) {
            xemu_settings_set_string(&g_config.display.dlss.adapter_path, "");
            DLSSSettingsChanged();
        }
    }
    if (ChevronCombo("Processing resolution", &g_config.display.dlss.processing_resolution,
                     "Automatic\0Up to 1280 x 720\0Up to 1920 x 1080\0Display resolution\0",
                     "Automatic caps processing at 1280 x 720. Presets preserve "
                     "aspect ratio and never upscale. Display resolution means "
                     "xemu's rendered display image, not the desktop window. "
                     "An adapter must implement resampling to honor a smaller input.")) {
        DLSSSettingsChanged();
    }

    XemuDLSSSnapshot snapshot{};
    xemu_dlss_get_snapshot(&snapshot);
    if (snapshot.restart_required) {
        ImGui::TextWrapped("Restart xemu to apply the saved changes. Resetting the Xbox is not enough.");
    } else {
        ImGui::TextDisabled("Mode, adapter and resolution changes apply on restart.");
    }
    if (snapshot.next_launch.environment_overrides) {
        ImGui::TextWrapped(
            "Startup environment overrides are active%s%s%s. Saved preferences "
            "are not overwritten; remove the overrides before restarting to use them.",
            snapshot.next_launch.environment_overrides & XEMU_DLSS_OVERRIDE_ENABLE ? " [mode]" : "",
            snapshot.next_launch.environment_overrides & XEMU_DLSS_OVERRIDE_PATH ? " [adapter]" : "",
            snapshot.next_launch.environment_overrides & XEMU_DLSS_OVERRIDE_EXTENT ? " [resolution]" : "");
    }
    if (!snapshot.next_launch.valid && snapshot.next_launch.reason[0]) {
        ImGui::TextWrapped("Configuration: %s", snapshot.next_launch.reason);
    }

    SectionTitle("Runtime status");
    ImGui::TextWrapped("Saved mode: %s", xemu_dlss_mode_name(snapshot.requested_mode));
    ImGui::TextWrapped("This launch: %s", xemu_dlss_mode_name(snapshot.startup.mode));
    const char *renderer = g_config.display.renderer == CONFIG_DISPLAY_RENDERER_VULKAN ?
        "Vulkan" : g_config.display.renderer == CONFIG_DISPLAY_RENDERER_OPENGL ? "OpenGL" : "Null";
    ImGui::Text("Selected renderer: %s", renderer);
    ImGui::TextWrapped("Adapter state: %s", xemu_dlss_runtime_state_name(snapshot.runtime.state));
    ImGui::TextWrapped("Actually produced: %s", xemu_neural_present_feature_string(
        static_cast<XemuNeuralPresentFeature>(snapshot.runtime.feature)));
    ImGui::TextWrapped("GPU: %s", snapshot.runtime.gpu_name[0] ? snapshot.runtime.gpu_name : "Not reported");
    ImGui::TextWrapped("Adapter: %s %s",
        snapshot.runtime.adapter_name[0] ? snapshot.runtime.adapter_name : "Not loaded",
        snapshot.runtime.adapter_version);
    if (snapshot.runtime.processing_width && snapshot.runtime.processing_height) {
        ImGui::Text("Requested processing extent: %u x %u",
                    snapshot.runtime.processing_width, snapshot.runtime.processing_height);
    } else {
        ImGui::TextDisabled("Processing extent: no frame submitted");
    }
    ImGui::Text("Processed / bypassed / failed: %llu / %llu / %llu",
        static_cast<unsigned long long>(snapshot.runtime.processed_frames),
        static_cast<unsigned long long>(snapshot.runtime.bypassed_frames),
        static_cast<unsigned long long>(snapshot.runtime.failed_frames));
    ImGui::Text("NR frames: %llu", static_cast<unsigned long long>(snapshot.runtime.nr_frames));
    if (snapshot.runtime.gpu_time_ns) {
        ImGui::Text("Last adapter-reported GPU time: %.3f ms",
                    snapshot.runtime.gpu_time_ns / 1000000.0);
    } else {
        ImGui::TextDisabled("GPU processing time: not reported");
    }
    if (snapshot.runtime.message[0]) {
        ImGui::TextWrapped("Last status: %s", snapshot.runtime.message);
    }

    SectionTitle("Requirements and help");
#ifndef _WIN32
    ImGui::TextWrapped("External adapters currently require Windows. DLSS settings remain visible on this platform.");
#endif
#ifdef CONFIG_VULKAN
    if (g_config.display.renderer != CONFIG_DISPLAY_RENDERER_VULKAN) {
        ImGui::TextWrapped("DLSS requires Vulkan. Selecting it here takes effect after restarting xemu.");
        if (ImGui::Button("Switch renderer to Vulkan")) {
            g_config.display.renderer = CONFIG_DISPLAY_RENDERER_VULKAN;
            xemu_settings_save();
            xemu_queue_notification("Renderer changed. Restart xemu to use Vulkan.");
        }
    }
#else
    ImGui::TextWrapped("This build has no Vulkan backend. Use a Vulkan-enabled build to run an adapter.");
#endif
    if (snapshot.next_launch.adapter_path[0]) {
        ImGui::TextWrapped("Next-launch adapter path: %s", snapshot.next_launch.adapter_path);
        if (ImGui::Button("Copy adapter path")) {
            ImGui::SetClipboardText(snapshot.next_launch.adapter_path);
        }
    }
    Hyperlink("Adapter setup and build instructions",
        "https://github.com/Mainkill1/xemu/blob/feature/dlss5-neural-present-scaffold/docs/devel/dlss-settings.md");
    Hyperlink("DLSS integration tracking issue",
        "https://github.com/Mainkill1/xemu/issues/261");
}

//
// xemu User Interface
//
// Copyright (C) 2020-2022 Matt Borgerson
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#include <SDL3/SDL.h>
#include <epoxy/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <functional>
#include <assert.h>
#include <fpng.h>

#include <deque>
#include <cstring>
#include <vector>
#include <string>
#include <memory>

#include "actions.hh"
#include "common.hh"
#include "xemu-hud.h"
#include "misc.hh"
#include "gl-helpers.hh"
#include "input-manager.hh"
#include "snapshot-manager.hh"
#include "viewport-manager.hh"
#include "font-manager.hh"
#include "scene.hh"
#include "scene-manager.hh"
#include "main-menu.hh"
#include "popup-menu.hh"
#include "notifications.hh"
#include "monitor.hh"
#include "debug.hh"
#include "shader-browser.hh"
#include "shader-browser-session-provider.hh"
#include "welcome.hh"
#include "menubar.hh"
#include "compat.hh"
#if defined(_WIN32)
#include "update.hh"
#endif
#include "../xemu-settings.h"
#include "../xemu-gpu-info.h"
#include "xemu-xbe.h"
#include "xemu-version.h"
#include "hw/xbox/nv2a/pgraph/shader-browser-flush.h"

bool g_screenshot_pending;
const char *g_snapshot_pending_load_name;

float g_main_menu_height;

static ImGuiStyle g_base_style;
static float g_last_scale;
static int g_vsync;
static GLuint g_tex;
static bool g_flip_req;
static XemuShaderBrowserScope g_shader_browser_scope{};
static std::string g_shader_browser_performance_session;
static std::string g_shader_browser_session_key;
static uint64_t g_shader_browser_next_scope_poll_ms;

static void ShaderBrowserEndPerformanceSessionLocked(void *)
{
    if (g_shader_browser_performance_session.empty()) {
        return;
    }
    char error[256] = {};
    if (!xemu_shader_browser_performance_session_end(
            g_shader_browser_performance_session.c_str(),
            static_cast<uint64_t>(g_get_real_time() / 1000), 1,
            error, sizeof(error))) {
        fprintf(stderr, "Shader Browser session end failed: %s\n",
                error[0] ? error : "unknown error");
    }
    g_shader_browser_performance_session.clear();
    g_shader_browser_session_key.clear();
}

void ShaderBrowserEndPerformanceSession()
{
    if (!g_shader_browser_performance_session.empty()) {
        pgraph_shader_browser_transition(
            ShaderBrowserEndPerformanceSessionLocked, nullptr);
    }
}

struct ShaderBrowserScopeTransition {
    XemuShaderBrowserScope scope;
    std::string session_key;
    bool scope_changed;
};

static void ShaderBrowserApplyScopeTransition(void *opaque)
{
    auto *transition = static_cast<ShaderBrowserScopeTransition *>(opaque);
    bool record_sessions = g_config.shader_browser.database.enabled &&
                           g_config.shader_browser.database.record_performance_sessions;
    if (transition->scope_changed || !record_sessions ||
        transition->session_key != g_shader_browser_session_key) {
        ShaderBrowserEndPerformanceSessionLocked(nullptr);
    }
    if (transition->scope_changed) {
        g_shader_browser_scope = transition->scope;
        xemu_shader_browser_set_current_scope(&transition->scope);
        xemu_shader_browser_session_clear_live();
    }
    if (!transition->scope.title_id || !record_sessions ||
        !g_shader_browser_performance_session.empty()) {
        return;
    }

    char *uuid = g_uuid_string_random();
    XemuShaderBrowserPerformanceSession session{};
    session.session_id = uuid;
    session.title_id = transition->scope.title_id;
    session.executable_fingerprint_version =
        transition->scope.executable_fingerprint_version;
    std::memcpy(session.executable_fingerprint,
                transition->scope.executable_fingerprint,
                sizeof(session.executable_fingerprint));
    session.started_unix_ms =
        static_cast<uint64_t>(g_get_real_time() / 1000);
    session.xemu_revision = xemu_version;
    session.renderer =
        g_config.display.renderer == CONFIG_DISPLAY_RENDERER_VULKAN ?
            "Vulkan" : "OpenGL";
#if defined(_WIN32)
    session.host_os = "Windows";
#elif defined(__APPLE__)
    session.host_os = "macOS";
#else
    session.host_os = "Linux";
#endif
    session.internal_resolution_scale =
        g_config.display.quality.surface_scale;
    session.ubershader_mode =
        g_config.tweaks.vk_hybrid_ubershaders ? "enabled" : "off";
    session.shader_cache_enabled = g_config.perf.cache_shaders;
    std::string gpu_driver;
    const PGRAPHVkDeviceRecord *gpu = xemu_gpu_info_get_actual_device();
    if (gpu && g_config.display.renderer ==
                   CONFIG_DISPLAY_RENDERER_VULKAN) {
        session.gpu_name = gpu->name;
        session.gpu_vendor_id = gpu->vendor_id;
        session.gpu_device_id = gpu->device_id;
        gpu_driver = std::to_string(gpu->driver_version);
        session.gpu_driver = gpu_driver.c_str();
    } else {
        const GLubyte *renderer = glGetString(GL_RENDERER);
        const GLubyte *driver = glGetString(GL_VERSION);
        session.gpu_name = reinterpret_cast<const char *>(renderer);
        session.gpu_driver = reinterpret_cast<const char *>(driver);
    }
    session.telemetry_version = 1;
    char error[256] = {};
    if (xemu_shader_browser_performance_session_begin(
            &session, error, sizeof(error))) {
        g_shader_browser_performance_session = uuid;
        g_shader_browser_session_key = transition->session_key;
    } else {
        fprintf(stderr, "Shader Browser session begin failed: %s\n",
                error[0] ? error : "unknown error");
    }
    g_free(uuid);
}

static void ShaderBrowserRefreshScope(uint64_t now_ms)
{
    if (now_ms < g_shader_browser_next_scope_poll_ms) {
        return;
    }
    g_shader_browser_next_scope_poll_ms = now_ms + 1000;

    XemuShaderBrowserScope scope{};
    struct xbe *xbe = xemu_get_xbe_info();
    if (xbe && xbe->cert && xbe->headers && xbe->headers_len) {
        scope.title_id = GUINT32_FROM_LE(xbe->cert->m_titleid);
        if (scope.title_id) {
            GChecksum *checksum = g_checksum_new(G_CHECKSUM_SHA256);
            g_checksum_update(checksum, xbe->headers, xbe->headers_len);
            gsize digest_size = sizeof(scope.executable_fingerprint);
            g_checksum_get_digest(checksum, scope.executable_fingerprint,
                                  &digest_size);
            g_checksum_free(checksum);
            scope.executable_fingerprint_version = 1;
        }
    }

    std::string session_key =
        std::to_string(g_config.display.renderer) + ":" +
        std::to_string(g_config.display.quality.surface_scale) + ":" +
        std::to_string(g_config.tweaks.vk_hybrid_ubershaders) + ":" +
        std::to_string(g_config.perf.cache_shaders);
    bool scope_changed =
        std::memcmp(&scope, &g_shader_browser_scope, sizeof(scope)) != 0;
    bool record_sessions = g_config.shader_browser.database.enabled &&
                           g_config.shader_browser.database.record_performance_sessions;
    bool session_changed = !g_shader_browser_performance_session.empty() &&
        (!record_sessions || session_key != g_shader_browser_session_key);
    bool begin_session = scope.title_id && record_sessions &&
        g_shader_browser_performance_session.empty();
    if (scope_changed || session_changed || begin_session) {
        ShaderBrowserScopeTransition transition{scope, session_key,
                                                 scope_changed};
        pgraph_shader_browser_transition(ShaderBrowserApplyScopeTransition,
                                         &transition);
    }
}

static void InitializeStyle()
{
    g_font_mgr.Rebuild();

    ImGui::StyleColorsDark();
    ImVec4 *c = ImGui::GetStyle().Colors;
    c[ImGuiCol_Text]                  = ImVec4(0.94f, 0.94f, 0.94f, 1.00f);
    c[ImGuiCol_TextDisabled]          = ImVec4(0.86f, 0.93f, 0.89f, 0.28f);
    c[ImGuiCol_WindowBg]              = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    c[ImGuiCol_ChildBg]               = ImVec4(0.06f, 0.06f, 0.06f, 0.98f);
    c[ImGuiCol_PopupBg]               = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    c[ImGuiCol_Border]                = ImVec4(0.11f, 0.11f, 0.11f, 0.60f);
    c[ImGuiCol_BorderShadow]          = ImVec4(0.16f, 0.16f, 0.16f, 0.00f);
    c[ImGuiCol_FrameBg]               = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    c[ImGuiCol_FrameBgHovered]        = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    c[ImGuiCol_FrameBgActive]         = ImVec4(0.28f, 0.71f, 0.25f, 1.00f);
    c[ImGuiCol_TitleBg]               = ImVec4(0.20f, 0.51f, 0.18f, 1.00f);
    c[ImGuiCol_TitleBgActive]         = ImVec4(0.26f, 0.66f, 0.23f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.16f, 0.16f, 0.16f, 0.75f);
    c[ImGuiCol_MenuBarBg]             = ImVec4(0.14f, 0.14f, 0.14f, 0.00f);
    c[ImGuiCol_ScrollbarBg]           = ImVec4(0.16f, 0.16f, 0.16f, 0.00f);
    c[ImGuiCol_ScrollbarGrab]         = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.24f, 0.60f, 0.00f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.24f, 0.60f, 0.00f, 1.00f);
    c[ImGuiCol_CheckMark]             = ImVec4(0.26f, 0.66f, 0.23f, 1.00f);
    c[ImGuiCol_SliderGrab]            = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
    c[ImGuiCol_SliderGrabActive]      = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    c[ImGuiCol_Button]                = ImVec4(0.17f, 0.17f, 0.17f, 1.00f);
    c[ImGuiCol_ButtonHovered]         = ImVec4(0.24f, 0.60f, 0.00f, 1.00f);
    c[ImGuiCol_ButtonActive]          = ImVec4(0.26f, 0.66f, 0.23f, 1.00f);
    c[ImGuiCol_Header]                = ImVec4(0.24f, 0.60f, 0.00f, 1.00f);
    c[ImGuiCol_HeaderHovered]         = ImVec4(0.24f, 0.60f, 0.00f, 1.00f);
    c[ImGuiCol_HeaderActive]          = ImVec4(0.24f, 0.60f, 0.00f, 1.00f);
    c[ImGuiCol_Separator]             = ImVec4(1.00f, 1.00f, 1.00f, 0.25f);
    c[ImGuiCol_SeparatorHovered]      = ImVec4(0.13f, 0.87f, 0.16f, 0.78f);
    c[ImGuiCol_SeparatorActive]       = ImVec4(0.25f, 0.75f, 0.10f, 1.00f);
    c[ImGuiCol_ResizeGrip]            = ImVec4(0.47f, 0.83f, 0.49f, 0.04f);
    c[ImGuiCol_ResizeGripHovered]     = ImVec4(0.28f, 0.71f, 0.25f, 0.78f);
    c[ImGuiCol_ResizeGripActive]      = ImVec4(0.28f, 0.71f, 0.25f, 1.00f);
    c[ImGuiCol_Tab]                   = ImVec4(0.26f, 0.67f, 0.23f, 0.95f);
    c[ImGuiCol_TabHovered]            = ImVec4(0.24f, 0.60f, 0.00f, 1.00f);
    c[ImGuiCol_TabActive]             = ImVec4(0.24f, 0.60f, 0.00f, 1.00f);
    c[ImGuiCol_TabUnfocused]          = ImVec4(0.21f, 0.54f, 0.19f, 0.99f);
    c[ImGuiCol_TabUnfocusedActive]    = ImVec4(0.24f, 0.60f, 0.21f, 1.00f);
    c[ImGuiCol_PlotLines]             = ImVec4(0.86f, 0.93f, 0.89f, 0.63f);
    c[ImGuiCol_PlotLinesHovered]      = ImVec4(0.28f, 0.71f, 0.25f, 1.00f);
    c[ImGuiCol_PlotHistogram]         = ImVec4(0.86f, 0.93f, 0.89f, 0.63f);
    c[ImGuiCol_PlotHistogramHovered]  = ImVec4(0.28f, 0.71f, 0.25f, 1.00f);
    c[ImGuiCol_TextSelectedBg]        = ImVec4(0.26f, 0.66f, 0.23f, 1.00f);
    c[ImGuiCol_DragDropTarget]        = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
    c[ImGuiCol_NavHighlight]          = ImVec4(0.28f, 0.71f, 0.25f, 1.00f);
    c[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    c[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    c[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.16f, 0.16f, 0.16f, 0.73f);

    ImGuiStyle &s = ImGui::GetStyle();
    s.WindowRounding = 6.0;
    s.FrameRounding = 6.0;
    s.PopupRounding = 6.0;
    g_base_style = s;
}

void xemu_hud_init(SDL_Window* window, void* sdl_gl_context)
{
    xemu_monitor_init();
    g_vsync = g_config.display.window.vsync;

    InitCustomRendering();

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.IniFilename = NULL;

    // Setup Platform/Renderer bindings
    ImGui_ImplSDL3_InitForOpenGL(window, sdl_gl_context);
    ImGui_ImplOpenGL3_Init("#version 150");
    ImPlot::CreateContext();

#if defined(_WIN32)
    if (!g_config.general.show_welcome && g_config.general.updates.check) {
        update_window.CheckForUpdates();
    }
#endif
    g_last_scale = g_viewport_mgr.m_scale;
    InitializeStyle();

    g_shader_browser_scope = {};
    g_shader_browser_next_scope_poll_ms = 0;
    char *shader_config_dir = g_path_get_dirname(xemu_settings_get_path());
    if (xemu_shader_browser_session_install(shader_config_dir)) {
        char error[256] = {};
        if (!xemu_shader_browser_database_configure(
                g_config.shader_browser.database.enabled,
                g_config.shader_browser.database.record_performance_sessions,
                g_config.shader_browser.database.save_external_artifacts,
                error, sizeof(error))) {
            fprintf(stderr, "Shader Browser persistence: %s\n", error);
        }
    } else {
        fprintf(stderr, "Unable to install Shader Browser provider\n");
    }
    g_free(shader_config_dir);

    g_main_menu.SetNextViewIndex(g_config.general.last_viewed_menu_index);
    first_boot_window.is_open = g_config.general.show_welcome;
}

void xemu_hud_cleanup(void)
{
    ShaderBrowserEndPerformanceSession();
    xemu_shader_browser_set_current_scope(nullptr);
    shader_browser_window.m_is_open = false;
    xemu_shader_browser_session_uninstall();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

void xemu_hud_process_sdl_events(SDL_Event *event)
{
    // Ignore inputs that are consumed by rebinding
    if (g_main_menu.ConsumeRebindEvent(event)) {
        return;
    }

    ImGui_ImplSDL3_ProcessEvent(event);
}

void xemu_hud_should_capture_kbd_mouse(int *kbd, int *mouse)
{
    ImGuiIO& io = ImGui::GetIO();
    if (kbd) *kbd = io.WantCaptureKeyboard;
    if (mouse) *mouse = io.WantCaptureMouse;
}

void xemu_hud_set_framebuffer_texture(GLuint tex, bool flip)
{
    g_tex = tex;
    g_flip_req = flip;
}

void xemu_hud_update(void)
{
    ImGuiIO& io = ImGui::GetIO();
    uint32_t now = SDL_GetTicks();
    if (g_config.shader_browser.database.enabled ||
        shader_browser_window.m_is_open) {
        ShaderBrowserRefreshScope(SDL_GetTicks());
    }

    g_viewport_mgr.Update();
    g_font_mgr.Update();
    if (g_last_scale != g_viewport_mgr.m_scale) {
        ImGuiStyle &style = ImGui::GetStyle();
        style = g_base_style;
        style.ScaleAllSizes(g_viewport_mgr.m_scale);
        g_last_scale = g_viewport_mgr.m_scale;
    }

    if (!first_boot_window.is_open) {
        int ww, wh;
        SDL_GetWindowSizeInPixels(xemu_get_window(), &ww, &wh);
        RenderFramebuffer(g_tex, ww, wh, g_flip_req);
    }

    ImGui_ImplOpenGL3_NewFrame();
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
    ImGui_ImplSDL3_NewFrame();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    g_input_mgr.Update();

    ImGui::NewFrame();
    ProcessKeyboardShortcuts();

#if defined(CONFIG_RENDERDOC)
    if (g_capture_renderdoc_frame) {
        nv2a_dbg_renderdoc_capture_frames(1, false);
        g_capture_renderdoc_frame = false;
    }
#endif

    if (g_config.display.ui.show_menubar && !first_boot_window.is_open) {
        // Auto-hide main menu after 5s of inactivity
        static uint32_t last_check = 0;
        float alpha = 1.0;
        const uint32_t timeout = 5000;
        const float fade_duration = 1000.0;
        bool menu_wakeup = g_input_mgr.MouseMoved();
        if (menu_wakeup) {
            last_check = now;
        }
        if ((now-last_check) > timeout) {
            if (g_config.display.ui.use_animations) {
                float t = fmin((float)((now-last_check)-timeout)/fade_duration, 1);
                alpha = 1-t;
                if (t >= 1) {
                    alpha = 0;
                }
            } else {
                alpha = 0;
            }
        }
        if (alpha > 0.0) {
            ImVec4 tc = ImGui::GetStyle().Colors[ImGuiCol_Text];
            tc.w = alpha;
            ImGui::PushStyleColor(ImGuiCol_Text, tc);
            ImGui::SetNextWindowBgAlpha(alpha);
            ShowMainMenu();
            ImGui::PopStyleColor();
        } else {
            g_main_menu_height = 0;
        }
    }

    static uint32_t last_mouse_move = 0;
    if (g_input_mgr.MouseMoved()) {
        last_mouse_move = now;
    }

    // FIXME: Handle time wrap around
    if (g_config.display.ui.hide_cursor && (now - last_mouse_move) > 3000) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    }

    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow) &&
        !g_scene_mgr.IsDisplayingScene()) {

        // If the guide button is pressed, wake the ui
        bool menu_button = false;
        uint32_t buttons = g_input_mgr.CombinedButtons();
        if (buttons & CONTROLLER_BUTTON_GUIDE) {
            menu_button = true;
        }

        // Allow controllers without a guide button to also work
        if ((buttons & CONTROLLER_BUTTON_BACK) &&
            (buttons & CONTROLLER_BUTTON_START)) {
            menu_button = true;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_F1)) {
            g_scene_mgr.PushScene(g_main_menu);
        } else if (ImGui::IsKeyPressed(ImGuiKey_F2)) {
            g_scene_mgr.PushScene(g_popup_menu);
        } else if (menu_button ||
                   (ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
                    !ImGui::IsAnyItemFocused() && !ImGui::IsAnyItemHovered())) {
            g_scene_mgr.PushScene(g_popup_menu);
        } else if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            xemu_toggle_fullscreen();
        }

        bool mod_key_down = ImGui::IsKeyDown(ImGuiKey_ModShift);
        for (int f_key = 0; f_key < 4; ++f_key) {
            if (ImGui::IsKeyPressed((enum ImGuiKey)(ImGuiKey_F5 + f_key))) {
                ActionActivateBoundSnapshot(f_key, mod_key_down);
                break;
            }
        }
    }

    first_boot_window.Draw();
    monitor_window.Draw();
    apu_window.Draw();
    video_window.Draw();
    shader_browser_window.Draw();
    compatibility_reporter_window.Draw();
#if defined(_WIN32)
    update_window.Draw();
#endif
    g_scene_mgr.Draw();
    if (!first_boot_window.is_open) notification_manager.Draw();
    g_snapshot_mgr.Draw();

    // static bool show_demo = true;
    // if (show_demo) ImGui::ShowDemoWindow(&show_demo);
}

void xemu_hud_render()
{
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    if (g_vsync != g_config.display.window.vsync) {
        g_vsync = g_config.display.window.vsync;
        SDL_GL_SetSwapInterval(g_vsync ? 1 : 0);
    }

    if (g_screenshot_pending) {
        SaveScreenshot(g_tex, g_flip_req);
        g_screenshot_pending = false;
    }
}

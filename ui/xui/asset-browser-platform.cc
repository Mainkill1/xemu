// SPDX-License-Identifier: GPL-2.0-or-later
#include "common.hh"
#include "asset-browser.hh"
#include "shader-browser.hh"
#include "shader-browser-session-provider.hh"
#include "xemu-hud.h"
#include "ui/xemu-settings.h"
extern "C" {
#include "hw/xbox/nv2a/debug.h"
}
namespace {
xemu::shader_browser::CaptureSessionContext AssetContext()
{
    xemu::shader_browser::CaptureSessionContext context;
    XemuShaderBrowserScope scope{};
    context.scope_generation = xemu_shader_browser_copy_current_scope(&scope);
    context.scope.title_id = scope.title_id;
    context.scope.executable_fingerprint_version =
        scope.executable_fingerprint_version;
    std::copy(std::begin(scope.executable_fingerprint),
              std::end(scope.executable_fingerprint),
              context.scope.executable_fingerprint.begin());
    context.session_epoch = xemu_shader_browser_live_epoch();
    context.renderer_epoch = nv2a_profile_preview_renderer_epoch();
    context.backend =
        g_config.display.renderer == CONFIG_DISPLAY_RENDERER_VULKAN ? 2 : 1;
    return context;
}
} // namespace
AssetBrowserWindow::AssetBrowserWindow()
    : AssetBrowserWindow(
          xemu::shader_browser::GetCaptureSession(), AssetContext,
          [](auto event, const auto &context) {
              if (shader_browser_window.InspectCapturedOccurrence(
                      std::move(event), context))
                  xemu_hud_request_shader_browser_window();
          })
{
}
AssetBrowserWindow asset_browser_window;

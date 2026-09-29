// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "asset-browser-live.hh"
#include <functional>

class AssetBrowserWindow {
public:
    using ContextSource =
        std::function<xemu::shader_browser::CaptureSessionContext()>;
    using PausedSource = std::function<bool()>;
    using ShaderSink = std::function<std::string(
        std::shared_ptr<const xemu::shader_browser::CaptureOccurrence>,
        const xemu::shader_browser::CaptureSessionContext &,
        std::shared_ptr<const xemu::shader_browser::CaptureSessionSnapshot>)>;
    bool m_is_open = false;
    AssetBrowserWindow();
    AssetBrowserWindow(xemu::shader_browser::CaptureSession &, ContextSource,
                       ShaderSink, PausedSource = {});
    ~AssetBrowserWindow();
    void Draw();
    void Shutdown();
    bool InspectCatalog(xemu::asset_browser::AssetCatalog);
    bool InspectOccurrence(
        std::shared_ptr<const xemu::shader_browser::CaptureOccurrence>,
        const xemu::shader_browser::CaptureSessionContext &,
        std::shared_ptr<
            const xemu::shader_browser::CaptureSessionSnapshot> = {});
    std::shared_ptr<const xemu::asset_browser::AssetAssembly> Selected() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
extern AssetBrowserWindow asset_browser_window;

// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "asset-browser-model.hh"

namespace xemu::asset_browser {
struct AssetCamera {
    float yaw = 0, pitch = 0, zoom = 1, pan_x = 0, pan_y = 0;
};
struct AssetViewportFrame {
    uint32_t texture = 0, width = 0, height = 0;
    size_t drawn_parts = 0, textured_parts = 0;
    uint64_t gpu_bytes = 0;
    std::string message;
};
// All methods touching GPU resources require the owning HUD GL context.
class AssetViewport {
public:
    AssetViewport();
    ~AssetViewport();
    AssetViewportFrame Render(std::shared_ptr<const AssetAssembly>,
                              const AssetCamera &, uint32_t width,
                              uint32_t height, int texture_slot = -1,
                              bool wireframe = false);
    AssetViewportFrame Thumbnail(std::shared_ptr<const AssetAssembly>);
    size_t ThumbnailCount() const;
    void Shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace xemu::asset_browser

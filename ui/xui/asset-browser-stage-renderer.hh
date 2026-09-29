// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "asset-browser-model.hh"
namespace xemu::asset_browser {
// Uses the caller's inspection framebuffer. All inputs are owned snapshots;
// cache entries own GPU storage and never retain renderer handles.
class AssetStageRenderer {
public:
    AssetStageRenderer();
    ~AssetStageRenderer();
    bool DrawPart(const AssetPart &, uint32_t backend,
                  const AssetMatrix &inspection_from_clip, uint32_t width,
                  uint32_t height, std::string *error,
                  bool projected_output = false);
    bool OutputBounds(const AssetPart &, uint32_t backend, capture::Bounds3 *,
                      std::string *error);
    uint64_t GpuBytes() const;
    bool Configure(uint64_t budget);
    void Shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace xemu::asset_browser

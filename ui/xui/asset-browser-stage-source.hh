// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "asset-browser-model.hh"
namespace xemu::asset_browser {
struct AssetStageSource {
    std::string text, error;
    std::vector<std::string> uniforms;
};
// Adapt the owned generated host interface; guest instructions remain intact.
AssetStageSource BuildAssetStageSource(const std::string &, uint32_t stage);
std::string AssetStageUniformName(uint32_t stage, const std::string &name);
AssetMatrix BuildAssetCameraMatrix(const capture::Bounds3 &, float yaw,
                                   float pitch, float zoom, float pan_x,
                                   float pan_y, float aspect);
} // namespace xemu::asset_browser

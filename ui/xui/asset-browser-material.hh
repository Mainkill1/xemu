// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "asset-browser-model.hh"
namespace xemu::asset_browser {
struct AssetTexture {
    uint32_t width = 0, height = 0, slot = 0;
    uint32_t min_filter = 0x2600, mag_filter = 0x2600;
    uint32_t wrap_s = 0x812f, wrap_t = 0x812f;
    std::vector<uint8_t> rgba;
    std::string reason;
};
// Base-level 2D diagnostic material, never an original-stage replay claim.
AssetTexture DecodeAssetTexture(const AssetPart &, uint32_t backend,
                                int slot = -1,
                                uint64_t byte_budget = 16U * 1024U * 1024U);
} // namespace xemu::asset_browser

// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "asset-browser-model.hh"
namespace xemu::asset_browser {
AssetPlacement DecodeAssetPlacement(const capture::CaptureOccurrence &);
AssetMatrix MultiplyAssetMatrices(const AssetMatrix &, const AssetMatrix &);
bool InvertAssetMatrix(const AssetMatrix &, AssetMatrix *);
std::vector<uint64_t> SuggestRelatedAssetParts(const AssetCatalog &,
                                               uint64_t anchor);
bool TransformAssetPoint(const AssetMatrix &, const std::array<float, 3> &,
                         std::array<float, 3> *);
} // namespace xemu::asset_browser

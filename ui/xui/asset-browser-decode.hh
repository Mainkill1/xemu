// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "asset-browser-model.hh"

namespace xemu::asset_browser {
using AssetDecodeResult = AssetPart;
AssetDecodeResult
DecodeAssetPart(std::shared_ptr<const capture::CaptureOccurrence>,
                uint32_t backend, const AssetLimits & = {});
// Host format metadata is retained; values are diagnostic decoded inputs.
bool DecodeAssetAttribute(const capture::CaptureOwnedBlob &, uint32_t backend,
                          size_t element, std::array<float, 4> *);
} // namespace xemu::asset_browser

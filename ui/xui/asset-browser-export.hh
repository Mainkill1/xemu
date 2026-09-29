// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "asset-browser-model.hh"
namespace xemu::asset_browser {
bool SaveAssetRecording(const AssetCatalog &, const AssetAssembly *,
                        const std::filesystem::path &,
                        std::string *error = nullptr,
                        capture::CaptureFileControl *control = nullptr);
bool SaveAssetAssembly(const AssetAssembly &, const std::filesystem::path &,
                       std::string *error = nullptr,
                       capture::CaptureFileControl *control = nullptr);
bool ReopenAssetRecording(const std::filesystem::path &, AssetCatalog *,
                          std::shared_ptr<const AssetAssembly> *selected,
                          std::string *error = nullptr,
                          capture::CaptureFileControl *control = nullptr);
bool ReopenAssetAssembly(const std::filesystem::path &, AssetAssembly *,
                         std::string *error = nullptr);
bool ExportAssetGlb(const AssetAssembly &, const std::filesystem::path &,
                    std::string *error = nullptr,
                    capture::CaptureFileControl *control = nullptr,
                    bool replace_scratch = false);
} // namespace xemu::asset_browser

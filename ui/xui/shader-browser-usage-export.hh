// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "shader-browser-provider.hh"

#include <string>
#include <filesystem>

namespace xemu::shader_browser {

// Format one already-owned provider snapshot. Missing observations and timing
// measurements stay empty in CSV and null in JSON.
std::string QuoteUsageCsvField(const std::string &value);
std::string SerializeUsageCsv(const Snapshot &snapshot);
std::string SerializeUsageJson(const Snapshot &snapshot);
bool ExportUsageSnapshot(const Snapshot &snapshot,
                         const std::filesystem::path &config_directory,
                         bool json, std::filesystem::path *exported_path,
                         std::string *error);

} // namespace xemu::shader_browser

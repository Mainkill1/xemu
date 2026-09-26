// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "shader-browser-details-model.hh"

#include <filesystem>
#include <string>

namespace xemu::shader_browser {

// Generated host text has a different provenance and format from a guest
// canonical recipe. This formatter preserves the GLSL bytes verbatim.
bool SerializeGeneratedGlsl(const Entry &entry, const HostSource &source,
                            std::string *glsl, std::string *metadata,
                            std::string *error);
bool ExportGeneratedGlsl(const Entry &entry, const HostSource &source,
                         const std::filesystem::path &config_directory,
                         std::filesystem::path *exported_path,
                         std::string *error);

} // namespace xemu::shader_browser

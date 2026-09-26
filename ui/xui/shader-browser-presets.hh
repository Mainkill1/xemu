// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "shader-browser-override-store.hh"

#include <string>
#include <vector>

namespace xemu::shader_browser {

bool ExportOverridePreset(const std::string &path,
                          const std::vector<OverrideRule> &rules,
                          std::string *error);
bool ImportOverridePreset(const std::string &path, OverrideStore *store,
                          size_t *imported_count, std::string *error);

} // namespace xemu::shader_browser

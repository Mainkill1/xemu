// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>

namespace xemu::shader_browser {

class OverrideStore;
class ReplacementLibrary;
class SavedOverrideRules;
struct OverrideRule;

bool LoadShaderOverrides(const std::string &config_dir,
                         bool saved_rules_enabled, OverrideStore *store,
                         ReplacementLibrary *library,
                         SavedOverrideRules *saved_rules, std::string *error);
bool ApplyShaderOverrideRule(const OverrideRule &rule, OverrideStore *store,
                             SavedOverrideRules *saved_rules,
                             std::string *error);

} // namespace xemu::shader_browser

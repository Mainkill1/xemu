// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-override-lifecycle.hh"

#include "shader-browser-replacement-library.hh"
#include "shader-browser-saved-rules.hh"

namespace xemu::shader_browser {

bool LoadShaderOverrides(const std::string &config_dir,
                         bool saved_rules_enabled, OverrideStore *store,
                         ReplacementLibrary *library,
                         SavedOverrideRules *saved_rules, std::string *error)
{
    if (!store || !library || !saved_rules) {
        if (error)
            *error = "Shader override services are unavailable";
        return false;
    }
    library->Configure(config_dir);
    std::string package_error;
    bool packages_ok = library->Reload(store, &package_error);
    std::string saved_error;
    bool saved_ok = saved_rules->Configure(config_dir, saved_rules_enabled,
                                           store, &saved_error);
    if (error) {
        *error = !saved_ok    ? saved_error :
                 !packages_ok ? package_error :
                                std::string{};
    }
    return packages_ok && saved_ok;
}

bool ApplyShaderOverrideRule(const OverrideRule &rule, OverrideStore *store,
                             SavedOverrideRules *saved_rules,
                             std::string *error)
{
    if (!store || !saved_rules) {
        if (error)
            *error = "Shader override services are unavailable";
        return false;
    }
    OverrideStoreSnapshot snapshot;
    store->CopySnapshot(&snapshot);
    if (!IsOverrideActionSupported(rule.action, snapshot.context.backend,
                                   error)) {
        return false;
    }
    return rule.origin == OverrideOrigin::Saved ?
               saved_rules->Save(rule, store, error) :
               store->UpsertRule(rule, error);
}

} // namespace xemu::shader_browser

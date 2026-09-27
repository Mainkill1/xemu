// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "shader-browser-override-store.hh"

#include <string>

struct sqlite3;

namespace xemu::shader_browser {

bool EncodeSavedOverrideRule(const OverrideRule &rule,
                             std::string *text, std::string *error);
bool DecodeSavedOverrideRule(const std::string &text,
                             OverrideRule *rule, std::string *error);

class SavedOverrideRules
{
public:
    ~SavedOverrideRules();
    bool Configure(const std::string &base_path, bool enabled,
                   OverrideStore *store, std::string *error);
    bool Save(const OverrideRule &rule, OverrideStore *store,
              std::string *error);
    bool Remove(uint64_t rule_id, OverrideStore *store,
                std::string *error);
    void Close();
    bool Enabled() const { return db_ != nullptr; }

private:
    bool Load(OverrideStore *store, std::string *error);
    sqlite3 *db_ = nullptr;
    std::string base_path_;
};

SavedOverrideRules &GetSavedOverrideRules();

} // namespace xemu::shader_browser

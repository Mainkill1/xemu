// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "shader-browser-override-store.hh"
#include "shader-browser-replacement-library.hh"

#include <cstdint>
#include <string>

namespace xemu::shader_browser {

struct ShaderOverrideRowPresentation {
    bool replacement_selected = false;
    bool compatible = false;
    std::string reason;
};

class ShaderOverrideUi
{
public:
    void Refresh(uint32_t current_title_id);
    ShaderOverrideRowPresentation EvaluateRow(const Entry &entry,
                                              uint32_t title_id) const;
    void DrawRowDragSource(const Entry &entry, uint32_t title_id,
                           const std::string &display_id);
    void DrawRowContextMenu(const Entry &entry, uint32_t title_id,
                            std::string *message);
    void DrawPanel(const Entry &entry, uint32_t title_id,
                   std::string *message);
    void DrawSettings(std::string *message);

private:
    const ReplacementPackageInfo *SelectedPackage() const;
    OverrideAction SelectedAction() const;
    bool ApplyRule(const ShaderKey &key, uint32_t title_id,
                   const Entry *entry, OverrideAction action,
                   uint64_t replacement_id, std::string *message);
    bool ApplyReplacement(const ShaderKey &key, uint32_t title_id,
                          const Entry *entry, std::string *message);
    void RefreshSnapshots(uint32_t current_title_id);

    bool configured_ = false;
    int action_index_ = static_cast<int>(OverrideAction::Normal);
    uint64_t selected_replacement_id_ = 0;
    bool restrict_build_ = false;
    uint64_t next_rule_revision_ = 1;
    OverrideStoreSnapshot store_snapshot_;
    ReplacementLibrarySnapshot library_snapshot_;
};

ShaderOverrideUi &GetShaderOverrideUi();

} // namespace xemu::shader_browser

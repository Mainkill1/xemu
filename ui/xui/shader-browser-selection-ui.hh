// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <imgui.h>
#include <imgui_internal.h>
#include <cstring>
#include <vector>

namespace xemu::shader_browser {

// Keyboard navigation previews the focused option immediately. Selectable
// still supplies ordinary click/Enter activation and popup closing behavior.
inline bool ShaderOptionSelected(const char *label, bool selected,
                                 ImGuiSelectableFlags flags = 0)
{
    const bool activated = ImGui::Selectable(label, selected, flags);
    const ImGuiContext &context = *ImGui::GetCurrentContext();
    return activated || (!selected && ImGui::IsItemFocused() &&
                         context.NavJustMovedToId == context.LastItemData.ID);
}

inline bool ShaderCombo(const char *label, int *current,
                        const char *const items[], int count)
{
    bool changed = false;
    const char *preview =
        *current >= 0 && *current < count ? items[*current] : "";
    if (ImGui::BeginCombo(label, preview)) {
        // Freeze initial selection so moving focus does not also move the
        // popup's default-focus item during this frame.
        const int initial = *current;
        for (int i = 0; i < count; ++i) {
            ImGui::PushID(i);
            if (ShaderOptionSelected(items[i], initial == i) && *current != i) {
                *current = i;
                changed = true;
            }
            if (initial == i)
                ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return changed;
}

inline bool ShaderCombo(const char *label, int *current, const char *items)
{
    // The existing small option lists use ImGui's double-NUL encoding.
    std::vector<const char *> options;
    for (const char *item = items; *item; item += std::strlen(item) + 1)
        options.push_back(item);
    return ShaderCombo(label, current, options.data(),
                       static_cast<int>(options.size()));
}

} // namespace xemu::shader_browser

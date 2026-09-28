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
    ImGuiContext &context = *ImGui::GetCurrentContext();
    ImGuiStorage *storage = ImGui::GetStateStorage();
    const ImGuiID id = ImGui::GetID(label);
    const ImGuiID frame_key = ImHashStr("##shader_combo_frame", 0, id);
    const ImGuiID opening_key = ImHashStr("##shader_combo_opening", 0, id);
    const bool was_open =
        storage->GetInt(frame_key, -1) == context.FrameCount - 1;
    // ImGui closes the popup during NewFrame, before its rows are submitted.
    if (was_open && ImGui::IsKeyPressed(ImGuiKey_Escape, false) &&
        !ImGui::IsPopupOpen(ImHashStr("##ComboPopup", 0, id),
                            ImGuiPopupFlags_None)) {
        const int opening = storage->GetInt(opening_key, *current);
        if (*current != opening) {
            *current = opening;
            changed = true;
        }
    }
    const char *preview =
        *current >= 0 && *current < count ? items[*current] : "";
    if (ImGui::BeginCombo(label, preview)) {
        if (!was_open)
            storage->SetInt(opening_key, *current);
        storage->SetInt(frame_key, context.FrameCount);
        int typed = -1;
        if (ImGui::IsWindowFocused()) {
            auto *request = ImGui::GetTypingSelectRequest(
                ImGuiTypingSelectFlags_AllowBackspace |
                ImGuiTypingSelectFlags_AllowSingleCharMode);
            typed = ImGui::TypingSelectFindMatch(
                request, count,
                [](void *data, int index) {
                    return (*static_cast<const char *const **>(data))[index];
                },
                &items, *current);
            if (typed >= 0 && typed != *current) {
                *current = typed;
                changed = true;
            }
        }
        // Freeze initial selection so moving focus does not also move the
        // popup's default-focus item during this frame.
        const int initial = *current;
        for (int i = 0; i < count; ++i) {
            ImGui::PushID(i);
            if (ShaderOptionSelected(items[i], initial == i) &&
                (typed < 0 || typed == i) && *current != i) {
                *current = i;
                changed = true;
            }
            if (initial == i)
                ImGui::SetItemDefaultFocus();
            if (typed == i)
                ImGui::FocusItem();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    } else {
        storage->SetInt(frame_key, -1);
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

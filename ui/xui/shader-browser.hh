// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "shader-browser-model.hh"
#include "shader-browser-provider.hh"
#include "shader-browser-details-store.hh"
#include "shader-browser-preview-health.hh"
#include "shader-browser-preview-model.hh"

#include <cstdint>
#include <array>
#include <string>
#include <vector>

class ShaderBrowserWindow
{
public:
    bool m_is_open;

    ShaderBrowserWindow();
    void Draw();

private:
    void RefreshSnapshot();
    void RebuildVisibleOrder();
    void SelectEntry(const xemu::shader_browser::Entry &entry);
    void ExportRecipe(const xemu::shader_browser::Entry &entry);
    void ApplyPersistenceSettings();

    void DrawSelector();
    void DrawRightPane();
    void DrawShaderDetails();
    void DrawOverview();
    void DrawGuest();
    void DrawHost();
    void DrawLifecycle();
    void DrawSettings();
    void DrawLivePreview();

    const xemu::shader_browser::Entry *SelectedEntry() const;

    xemu::shader_browser::Snapshot m_snapshot;
    std::vector<size_t> m_visible_order;
    xemu::shader_browser::ShaderKey m_selected_key;
    xemu::shader_browser::CanonicalRecipe m_selected_recipe;
    xemu::shader_browser::RecipeInspection m_recipe_inspection;
    xemu::shader_browser::DetailSnapshot m_detail_snapshot;
    xemu::shader_browser::PreviewHealthMonitor m_preview_health_monitor;
    xemu::shader_browser::PreviewSelection m_preview_packet_selection;
    uint64_t m_preview_packet_detail_generation = 0;
    uint64_t m_preview_packet_override_generation = 0;
    uint64_t m_preview_input_revision = 1;
    std::array<std::array<float, 4>, 4> m_preview_colors{};
    std::array<std::array<float, 4>, 4> m_preview_texture_colors{};
    std::array<float, 2> m_preview_uv_scale{1.0f, 1.0f};
    std::array<float, 2> m_preview_uv_offset{0.0f, 0.0f};
    std::array<float, 4> m_preview_constant_color{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4> m_preview_fog_color{0.0f, 0.0f, 0.0f, 0.0f};
    int m_preview_alpha_reference = 0;
    bool m_preview_linear_filter = true;
    bool m_preview_repeat_wrap = false;
    bool m_preview_packet_attempted = false;
    std::string m_preview_packet_message;
    std::vector<size_t> m_source_line_offsets;
    size_t m_selected_source = 0;
    uint64_t m_source_offsets_generation = 0;
    bool m_has_recipe = false;
    std::string m_recipe_error;

    std::string m_search;
    std::string m_action_message;
    uint64_t m_next_poll_ms;
    bool m_has_selection;
    bool m_filter_dirty;
    bool m_window_was_open;
    bool m_collection_requested;
    bool m_current_title_only;

    int m_stage_filter;
    int m_source_filter;
    int m_sort_key;
    bool m_sort_descending;
};

extern ShaderBrowserWindow shader_browser_window;

// Called before a user disables session recording or closes the HUD.
void ShaderBrowserEndPerformanceSession();

// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "shader-browser-model.hh"
#include "shader-browser-provider.hh"

#include <cstdint>
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
    void ApplyPersistenceSettings();

    void DrawSelector();
    void DrawRightPane();
    void DrawShaderDetails();
    void DrawSettings();
    void DrawLivePreview();

    const xemu::shader_browser::Entry *SelectedEntry() const;

    xemu::shader_browser::Snapshot m_snapshot;
    std::vector<size_t> m_visible_order;
    xemu::shader_browser::ShaderKey m_selected_key;

    std::string m_search;
    std::string m_action_message;
    uint64_t m_next_poll_ms;
    bool m_has_selection;
    bool m_filter_dirty;
    bool m_window_was_open;
    bool m_collection_requested;

    int m_stage_filter;
    int m_source_filter;
    int m_sort_key;
    bool m_sort_descending;
};

extern ShaderBrowserWindow shader_browser_window;

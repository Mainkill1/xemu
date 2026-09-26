
void ShaderBrowserWindow::SelectEntry(const Entry &entry)
{
    m_selected_key = entry.key;
    m_has_selection = true;
    m_action_message.clear();
    m_selected_recipe = {};
    m_recipe_inspection = {};
    m_recipe_error.clear();
    m_has_recipe = GetProvider().CopyCanonicalRecipe(entry.key,
                                                     &m_selected_recipe);
    if (m_has_recipe && !InspectCanonicalRecipe(m_selected_recipe,
                                                 &m_recipe_inspection,
                                                 &m_recipe_error)) {
        m_has_recipe = false;
    }
    if (!m_has_recipe && m_recipe_error.empty()) {
        m_recipe_error = "Canonical recipe unavailable";
    }
    m_detail_snapshot = {};
    m_selected_source = 0;
    m_source_line_offsets.clear();
    m_source_offsets_generation = 0;
    GetDetailStore().Request(entry.key, DetailBackend::Active);
    pgraph_shader_browser_request_details();
}

void ShaderBrowserWindow::ExportRecipe(const Entry &entry)
{
    CanonicalRecipe recipe{};
    std::string error;
    if (!GetProvider().CopyCanonicalRecipe(entry.key, &recipe)) {
        m_action_message = "Canonical recipe unavailable";
        return;
    }
    const char *base = xemu_settings_get_base_path();
    std::filesystem::path path;
    if (!base || !ExportPortableRecipe(recipe, std::filesystem::u8path(base),
                                       &path, &error)) {
        m_action_message = error.empty() ?
            "Shader recipe export path unavailable" : error;
        return;
    }
    m_action_message = "Exported recipe to " + path.u8string();
}

const Entry *ShaderBrowserWindow::SelectedEntry() const
{
    if (!m_has_selection) {
        return nullptr;
    }
    int index = FindEntryByKey(m_snapshot.entries, m_selected_key);
    return index >= 0 ? &m_snapshot.entries[static_cast<size_t>(index)] :
                        nullptr;
}

void ShaderBrowserWindow::ApplyPersistenceSettings()
{
    bool database_enabled = g_config.shader_browser.database.enabled;
    bool record_sessions = database_enabled &&
        g_config.shader_browser.database.record_performance_sessions;
    bool save_artifacts = database_enabled &&
        g_config.shader_browser.database.save_external_artifacts;
    if (!record_sessions) {
        ShaderBrowserEndPerformanceSession();
    }
    std::string error;
    if (!GetProvider().ConfigurePersistence(database_enabled, record_sessions,
                                            save_artifacts, &error)) {
        m_action_message = error.empty() ?
            "Unable to update Shader Browser persistence" : error;
    } else {
        m_action_message = database_enabled ?
            "Shader database settings applied" :
            "Persistent shader database disabled; existing files were kept";
    }
    xemu_settings_save();
    char *config_dir = g_path_get_dirname(xemu_settings_get_path());
    std::string saved_error;
    if (!GetSavedOverrideRules().Configure(
            config_dir ? config_dir : "", database_enabled,
            &GetOverrideStore(), &saved_error)) {
        m_action_message = saved_error;
    }
    g_free(config_dir);
    m_next_poll_ms = 0;
    RefreshSnapshot();
}

void ShaderBrowserWindow::DrawSelector()
{
    GetShaderOverrideUi().Refresh(m_snapshot.current_title_id);

    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##shader-search", "Search shaders...",
                                 &m_search)) {
        m_filter_dirty = true;
    }

    float width = ImGui::GetContentRegionAvail().x;
    float gap = ImGui::GetStyle().ItemSpacing.x;
    float combo_width = std::max(90.0f * g_viewport_mgr.m_scale,
                                 (width - gap * 2.0f) / 3.0f);
    ImGui::SetNextItemWidth(combo_width);
    if (ImGui::Combo("##stage", &m_stage_filter,
                     "All stages\0Vertex\0Pixel\0Geometry\0"
                     "Fixed function\0Unknown\0")) {
        m_filter_dirty = true;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(combo_width);
    if (ImGui::Combo("##source", &m_source_filter,
                     "All sources\0This session\0Database catalog (write-behind)\0")) {
        m_filter_dirty = true;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(combo_width);
    if (ImGui::Combo("##sort", &m_sort_key,
                     "Draws (high first)\0Last used (recent first)\0"
                     "Compile time (slow first)\0ID (A-Z)\0")) {
        m_sort_descending = m_sort_key != 3;
        m_filter_dirty = true;
    }
    if (m_snapshot.current_title_id &&
        ImGui::Checkbox("Current title only", &m_current_title_only)) {
        m_filter_dirty = true;
    }

    if (m_filter_dirty) {
        RebuildVisibleOrder();
    }

    ImGuiTableFlags flags = ImGuiTableFlags_Borders |
                            ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_Resizable |
                            ImGuiTableFlags_ScrollX |
                            ImGuiTableFlags_ScrollY |
                            ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("##shader-table", 6, flags,
                          ImVec2(0, -ImGui::GetTextLineHeightWithSpacing() * 2),
                          690.0f * g_viewport_mgr.m_scale)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_None,
                                145.0f * g_viewport_mgr.m_scale, 1);
        ImGui::TableSetupColumn("Stage", ImGuiTableColumnFlags_None,
                                48.0f * g_viewport_mgr.m_scale, 2);
        ImGui::TableSetupColumn("Draws", ImGuiTableColumnFlags_None,
                                72.0f * g_viewport_mgr.m_scale, 3);
        ImGui::TableSetupColumn("Last Used", ImGuiTableColumnFlags_None,
                                92.0f * g_viewport_mgr.m_scale, 4);
        ImGui::TableSetupColumn("Compile", ImGuiTableColumnFlags_None,
                                120.0f * g_viewport_mgr.m_scale, 5);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_None,
                                105.0f * g_viewport_mgr.m_scale, 6);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(m_visible_order.size()));
        while (clipper.Step()) {
            for (int visible = clipper.DisplayStart;
                 visible < clipper.DisplayEnd; ++visible) {
                const Entry &entry =
                    m_snapshot.entries[m_visible_order[visible]];
                std::string short_id = ShortDisplayId(
                    entry, m_snapshot.current_title_id);
                std::string full_id = FullDisplayId(
                    entry, m_snapshot.current_title_id);
                std::string row_id = PortableShaderHash(entry.key.hash) + "-" +
                                     StageLabel(entry.key.stage);
                bool selected = m_has_selection &&
                                m_selected_key == entry.key;
                ShaderOverrideRowPresentation override_row =
                    GetShaderOverrideUi().EvaluateRow(
                        entry, m_snapshot.current_title_id);

                ImGui::PushID(row_id.c_str());
                ImGui::TableNextRow();
                if (override_row.compatible) {
                    ImGui::TableSetBgColor(
                        ImGuiTableBgTarget_RowBg0,
                        IM_COL32(66, 104, 72, selected ? 115 : 72));
                }
                ImGui::TableSetColumnIndex(0);
                if (ImGui::Selectable(
                        short_id.c_str(), selected,
                        ImGuiSelectableFlags_SpanAllColumns |
                            ImGuiSelectableFlags_AllowOverlap)) {
                    SelectEntry(entry);
                }
                if (ImGui::BeginPopupContextItem("##shader-context")) {
                    if (ImGui::MenuItem("Copy game shader ID")) {
                        ImGui::SetClipboardText(full_id.c_str());
                        m_action_message = "Copied game shader ID";
                    }
                    if (ImGui::MenuItem("Copy canonical shader hash")) {
                        std::string canonical = PortableShaderHash(entry.key.hash);
                        ImGui::SetClipboardText(canonical.c_str());
                        m_action_message = "Copied canonical shader hash";
                    }
                    GetShaderOverrideUi().DrawRowContextMenu(
                        entry, m_snapshot.current_title_id,
                        &m_action_message);
                    if (ImGui::MenuItem("Export recipe", nullptr, false,
                                        entry.recipe_size > 0)) {
                        ExportRecipe(entry);
                    }
                    ImGui::EndPopup();
                }
                GetShaderOverrideUi().DrawRowDragSource(
                    entry, m_snapshot.current_title_id, full_id);

                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(StageLabel(entry.key.stage));
                ImGui::TableSetColumnIndex(2);
                if (entry.observed_in_session) {
                    ImGui::Text("%llu", static_cast<unsigned long long>(
                        entry.draw_count));
                } else {
                    ImGui::TextDisabled("-");
                }
                ImGui::TableSetColumnIndex(3);
                std::string last = FormatLastUsed(
                    entry.last_frame, m_snapshot.current_frame,
                    entry.observed_in_session);
                ImGui::TextUnformatted(last.c_str());
                ImGui::TableSetColumnIndex(4);
                std::string compile = FormatDurationNs(entry.compile_cpu);
                ImGui::TextUnformatted(compile.c_str());
                ImGui::TableSetColumnIndex(5);
                ImGui::TextUnformatted(StatusLabel(entry.status));
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }

    size_t session_count = std::count_if(
        m_snapshot.entries.begin(), m_snapshot.entries.end(),
        [](const Entry &entry) { return entry.observed_in_session; });
    ImGui::TextDisabled("%zu visible | %zu catalogued | %zu this session",
                        m_visible_order.size(),
                        m_snapshot.stored_shader_count, session_count);
}

void ShaderBrowserWindow::DrawRightPane()
{
    if (ImGui::BeginTabBar("##shader-browser-tabs")) {
        if (ImGui::BeginTabItem("Shader Details")) {
            DrawShaderDetails();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Settings")) {
            DrawSettings();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Live Preview")) {
            DrawLivePreview();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

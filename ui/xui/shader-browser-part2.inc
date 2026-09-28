
void ShaderBrowserWindow::SelectEntry(const Entry &entry)
{
    m_preview_packet_attempted = false;
    m_preview_packet_message.clear();
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

    ImGui::TextUnformatted("Shader catalog");
    ImGui::SameLine();
    ImGui::TextDisabled("%zu", m_snapshot.entries.size());
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##shader-search", "Search ID or hash...",
                                 &m_search)) {
        m_filter_dirty = true;
    }

    const float width = ImGui::GetContentRegionAvail().x;
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetNextItemWidth(std::max(85.0f, (width - gap * 2.0f -
                                             42.0f) * 0.5f));
    if (ImGui::Combo("##stage", &m_stage_filter,
                     "All stages\0Vertex\0Pixel\0Geometry\0"
                     "Fixed function\0Unknown\0")) {
        m_filter_dirty = true;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(std::max(85.0f, (width - gap * 2.0f -
                                             42.0f) * 0.5f));
    if (ImGui::Combo("##recent", &table_view.recent_index,
                     "Any time\0Last 30 frames\0Last 120 frames\0"
                     "Last 600 frames\0")) {
        m_filter_dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("...##catalog-filters")) {
        ImGui::OpenPopup("##catalog-filters-popup");
    }
    if (ImGui::BeginPopup("##catalog-filters-popup")) {
        ImGui::TextDisabled("Catalog filters");
        if (ImGui::Checkbox("Show hidden rows", &table_view.show_hidden))
            m_filter_dirty = true;
        if (m_snapshot.current_title_id &&
            ImGui::Checkbox("Current title only", &m_current_title_only))
            m_filter_dirty = true;
        ImGui::SetNextItemWidth(200.0f * g_viewport_mgr.m_scale);
        if (ImGui::Combo("Source", &m_source_filter,
                         "All sources\0This session\0Database catalog\0"))
            m_filter_dirty = true;
        ImGui::SetNextItemWidth(200.0f * g_viewport_mgr.m_scale);
        if (ImGui::Combo("Sort", &m_sort_key,
                         "Draws (high first)\0Last used (recent first)\0"
                         "Compile time (slow first)\0ID (A-Z)\0")) {
            m_sort_descending = m_sort_key != 3;
            m_filter_dirty = true;
        }
        ImGui::EndPopup();
    }
    if (table_view.show_hidden || !m_current_title_only ||
        m_source_filter != 0 || m_sort_key != 0) {
        ImGui::TextDisabled("Filtered catalog");
    }
    if (m_filter_dirty) RebuildVisibleOrder();

    ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV |
                            ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_ScrollY |
                            ImGuiTableFlags_SizingStretchProp;
    const float footer = ImGui::GetTextLineHeightWithSpacing() * 1.5f;
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,
                        ImVec2(2.0f * g_viewport_mgr.m_scale,
                               ImGui::GetStyle().CellPadding.y));
    if (ImGui::BeginTable("##shader-table", 5, flags, ImVec2(0, -footer))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Shader", ImGuiTableColumnFlags_WidthStretch,
                                1.0f);
        ImGui::TableSetupColumn("Draws", ImGuiTableColumnFlags_WidthFixed,
                                45.0f * g_viewport_mgr.m_scale);
        ImGui::TableSetupColumn("Last", ImGuiTableColumnFlags_WidthFixed,
                                40.0f * g_viewport_mgr.m_scale);
        ImGui::TableSetupColumn("P", ImGuiTableColumnFlags_WidthFixed,
                                20.0f * g_viewport_mgr.m_scale);
        ImGui::TableSetupColumn("H", ImGuiTableColumnFlags_WidthFixed,
                                20.0f * g_viewport_mgr.m_scale);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(m_visible_order.size()));
        while (clipper.Step()) {
            for (int visible = clipper.DisplayStart;
                 visible < clipper.DisplayEnd; ++visible) {
                const Entry &entry =
                    m_snapshot.entries[m_visible_order[visible]];
                const std::string short_id =
                    ShortDisplayId(entry, m_snapshot.current_title_id);
                const size_t separator = short_id.find('-');
                const std::string catalog_id =
                    (entry.key.stage == Stage::Pixel ? "PS " :
                     entry.key.stage == Stage::Vertex ? "VS " :
                     entry.key.stage == Stage::Geometry ? "GS " : "SH ") +
                    (separator == std::string::npos ? short_id :
                     short_id.substr(separator + 1));
                const std::string full_id =
                    FullDisplayId(entry, m_snapshot.current_title_id);
                const std::string row_id =
                    PortableShaderHash(entry.key.hash) + "-" +
                    StageLabel(entry.key.stage);
                const bool selected = m_has_selection &&
                                      m_selected_key == entry.key;
                const bool pinned = table_view.pinned.count(entry.key) != 0;
                const bool hidden = table_view.hidden.count(entry.key) != 0;
                const ShaderOverrideRowPresentation override_row =
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
                if (ShaderOptionSelected(
                        catalog_id.c_str(), selected,
                        ImGuiSelectableFlags_SpanAllColumns |
                            ImGuiSelectableFlags_AllowOverlap)) {
                    SelectEntry(entry);
                }
                GetShaderOverrideUi().DrawRowDragSource(
                    entry, m_snapshot.current_title_id, full_id);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s | %s\n%s", full_id.c_str(),
                                      StageLabel(entry.key.stage),
                                      hidden ? "Hidden from catalog" :
                                               StatusLabel(entry.status));
                }
                if (ImGui::BeginPopupContextItem("##shader-context")) {
                    if (ImGui::MenuItem(pinned ? "Unpin" : "Pin")) {
                        if (pinned) table_view.pinned.erase(entry.key);
                        else table_view.pinned.insert(entry.key);
                        m_filter_dirty = true;
                    }
                    if (ImGui::MenuItem(hidden ? "Unhide" : "Hide")) {
                        if (hidden) table_view.hidden.erase(entry.key);
                        else table_view.hidden.insert(entry.key);
                        m_filter_dirty = true;
                    }
                    if (ImGui::MenuItem("Copy game shader ID")) {
                        ImGui::SetClipboardText(full_id.c_str());
                        m_action_message = "Copied game shader ID";
                    }
                    if (ImGui::MenuItem("Copy canonical shader hash")) {
                        const std::string canonical =
                            PortableShaderHash(entry.key.hash);
                        ImGui::SetClipboardText(canonical.c_str());
                        m_action_message = "Copied canonical shader hash";
                    }
                    GetShaderOverrideUi().DrawRowContextMenu(
                        entry, m_snapshot.current_title_id,
                        &m_action_message);
                    if (ImGui::MenuItem("Export recipe", nullptr, false,
                                        entry.recipe_size > 0))
                        ExportRecipe(entry);
                    ImGui::EndPopup();
                }

                ImGui::TableSetColumnIndex(1);
                if (entry.observed_in_session)
                    ImGui::Text("%llu", static_cast<unsigned long long>(
                        entry.draw_count));
                else ImGui::TextDisabled("-");
                ImGui::TableSetColumnIndex(2);
                if (entry.observed_in_session) {
                    const uint64_t age = m_snapshot.current_frame >
                        entry.last_frame ?
                        m_snapshot.current_frame - entry.last_frame : 0;
                    if (age) ImGui::Text("%lluf",
                        static_cast<unsigned long long>(age));
                    else ImGui::TextUnformatted("now");
                } else {
                    ImGui::TextDisabled("-");
                }
                ImGui::TableSetColumnIndex(3);
                if (ImGui::SmallButton(pinned ? "*##pin" : "+##pin")) {
                    if (pinned) table_view.pinned.erase(entry.key);
                    else table_view.pinned.insert(entry.key);
                    m_filter_dirty = true;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(pinned ? "Unpin shader" : "Pin shader");
                ImGui::TableSetColumnIndex(4);
                if (ImGui::SmallButton(hidden ? "S##hide" : "H##hide")) {
                    if (hidden) table_view.hidden.erase(entry.key);
                    else table_view.hidden.insert(entry.key);
                    m_filter_dirty = true;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(hidden ? "Show hidden row" :
                                              "Hide catalog row only");
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();

    const size_t session_count = std::count_if(
        m_snapshot.entries.begin(), m_snapshot.entries.end(),
        [](const Entry &entry) { return entry.observed_in_session; });
    ImGui::TextDisabled("%zu visible | %zu this session",
                        m_visible_order.size(), session_count);
}

void ShaderBrowserWindow::DrawRightPane()
{
    bool preview_visible = false;
    if (ImGui::BeginTabBar("##shader-browser-tabs")) {
        ImGuiTabItemFlags details_flags = m_jump_to_overrides ?
            ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        if (ImGui::BeginTabItem("Shader Details", nullptr, details_flags)) {
            DrawSelectedHeader();
            DrawShaderDetails();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Settings")) {
            DrawSelectedHeader();
            if (ImGui::BeginChild("##shader-settings-body", ImVec2(0, 0)))
                DrawSettings();
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGuiTabItemFlags preview_flags = m_select_preview_tab ?
            ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        if (ImGui::BeginTabItem("Live Preview", nullptr, preview_flags)) {
            preview_visible = true;
            DrawSelectedHeader();
            PreviewStatus preview_status{};
            GetPreviewService().CopyStatus(&preview_status);
            if (!preview_status.visible) {
                m_preview_packet_attempted = false;
            }
            DrawLivePreview();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    m_select_preview_tab = false;
    if (!preview_visible) CancelWorkbenchCaptureWhenHidden();
    GetPreviewService().SetVisible(preview_visible, PreviewNowNs());
}

void ShaderBrowserWindow::DrawSelectedHeader()
{
    const Entry *entry = SelectedEntry();
    if (!entry) {
        ImGui::TextDisabled("Select a shader to inspect or preview it.");
        ImGui::Separator();
        return;
    }

    const std::string full_id = FullDisplayId(
        *entry, m_snapshot.current_title_id);
    const std::string short_id = ShortDisplayId(
        *entry, m_snapshot.current_title_id);
    ImGui::Text("%s  |  %s", short_id.c_str(),
                StageLabel(entry->key.stage));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", full_id.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy full ID")) {
        ImGui::SetClipboardText(full_id.c_str());
        m_action_message = "Copied game shader ID";
    }
    ImGui::SameLine();
    const OverrideResolution resolution =
        GetOverrideStore().Resolve(entry->key);
    const std::string action =
        resolution.status == OverrideResolutionStatus::Matched ?
            OverrideActionLabel(resolution.policy.action) :
            OverrideResolutionLabel(resolution.status);
    const std::string label = "Game rule: " + action + "##open-overrides";
    if (ImGui::SmallButton(label.c_str())) m_jump_to_overrides = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Requested rule; see Overrides for renderer state");

    const bool detail_current =
        m_detail_snapshot.request.key == entry->key &&
        (m_detail_snapshot.state == DetailState::Complete ||
         m_detail_snapshot.state == DetailState::Partial);
    const HostSource *source = detail_current &&
        !m_detail_snapshot.sources.empty() ?
            &m_detail_snapshot.sources.front() : nullptr;
    ImGui::TextDisabled("Scope: %s | Source: %s",
                        ScopeSummary(*entry).c_str(),
                        source ? (source->exact_runtime_source ?
                            "resident" : "reconstructed") :
                            "unavailable");
    ImGui::Separator();
}

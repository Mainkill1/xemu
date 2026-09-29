
void ShaderBrowserWindow::DrawOverview()
{
    const Entry *entry = SelectedEntry();
    if (!entry) return;

    const std::string full_id =
        FullDisplayId(*entry, m_snapshot.current_title_id);
    const std::string canonical = PortableShaderHash(entry->key.hash);
    const float scale = g_viewport_mgr.m_scale;
    if (ImGui::BeginTable("##overview-summary", 3,
                          ImGuiTableFlags_BordersInnerV |
                          ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("OBSERVED DRAWS");
        ImGui::Text("%s", entry->observed_in_session ?
            UnsignedText(entry->draw_count).c_str() : "Not observed");
        ImGui::TableNextColumn();
        ImGui::TextDisabled("LAST USED");
        ImGui::Text("%s", FormatLastUsed(entry->last_frame,
            m_snapshot.current_frame, entry->observed_in_session).c_str());
        ImGui::TableNextColumn();
        ImGui::TextDisabled("CURRENT ROUTE");
        ImGui::Text("%s", RouteLabel(entry->route));
        ImGui::EndTable();
    }
    ImGui::Spacing();
    const bool overview_columns =
        ImGui::GetContentRegionAvail().x >= 900.0f * scale;
    if (overview_columns) ImGui::Columns(2, "##overview-columns", false);
    ImGui::SeparatorText("Identity & provenance");
    if (ImGui::BeginTable("##shader-identity", 2,
                          ImGuiTableFlags_BordersInnerV |
                          ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("##name", ImGuiTableColumnFlags_WidthFixed,
                                150.0f * g_viewport_mgr.m_scale);
        ImGui::TableSetupColumn("##value",
                                ImGuiTableColumnFlags_WidthStretch);
        DrawKeyValue("Game shader ID", full_id);
        DrawKeyValue("Canonical hash", canonical);
        DrawKeyValue("Stage", StageLabel(entry->key.stage));
        DrawKeyValue("Title/build scope", ScopeSummary(*entry));
        DrawKeyValue("Canonical recipe",
                     entry->recipe_format_version ?
                         "Format v" +
                             std::to_string(entry->recipe_format_version) +
                             " | " + BytesText(entry->recipe_size) :
                         "Identity only");
        DrawKeyValue("Catalog status",
                     m_snapshot.database_write_failed ?
                         "Unconfirmed after database writer error" :
                         entry->stored_in_database ?
                             "Catalogued (write-behind)" :
                             "Not catalogued");
        const bool detail_current =
            m_detail_snapshot.request.key == entry->key &&
            (m_detail_snapshot.state == DetailState::Complete ||
             m_detail_snapshot.state == DetailState::Partial);
        DrawKeyValue("Generated host source",
                     detail_current && !m_detail_snapshot.sources.empty() ?
                         (m_detail_snapshot.sources.front().
                              exact_runtime_source ?
                              "Resident renderer source" :
                              "Reconstructed source; build association "
                              "unverified") :
                         "Not available in selected details");
        ImGui::EndTable();
    }

    if (overview_columns) ImGui::NextColumn();
    ImGui::SeparatorText("Observation window");
    if (ImGui::BeginTable("##shader-observation", 2,
                          ImGuiTableFlags_BordersInnerV |
                          ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("##name", ImGuiTableColumnFlags_WidthFixed,
                                150.0f * g_viewport_mgr.m_scale);
        ImGui::TableSetupColumn("##value",
                                ImGuiTableColumnFlags_WidthStretch);
        DrawKeyValue("Session draws",
                     entry->observed_in_session ?
                         UnsignedText(entry->draw_count) :
                         "Not observed this session");
        DrawKeyValue("First guest frame",
                     entry->observed_in_session ?
                         UnsignedText(entry->first_frame) :
                         "Not observed");
        DrawKeyValue("Last used",
                     FormatLastUsed(entry->last_frame,
                                    m_snapshot.current_frame,
                                    entry->observed_in_session));
        DrawKeyValue("Current guest frame",
                     UnsignedText(m_snapshot.current_frame));
        DrawKeyValue("Current route", RouteLabel(entry->route));
        DrawKeyValue("Readiness", ReadinessLabel(entry->readiness));
        ImGui::EndTable();
    }
    if (overview_columns) ImGui::Columns(1);
    ImGui::TextDisabled(
        "Session observations are separate from private preview rendering.");

    ImGui::SeparatorText("Availability");
    ImGui::Text("CPU compile samples: %llu | pipeline samples: %llu",
                static_cast<unsigned long long>(
                    entry->compile_cpu.samples),
                static_cast<unsigned long long>(
                    entry->pipeline_cpu.samples));
    ImGui::TextDisabled(
        "GPU duration, invocations, visible pixels, and per-draw capture: "
        "not collected.");
    if (!entry->scopes.empty() &&
        ImGui::CollapsingHeader("Known title/build associations")) {
        for (const ShaderScope &scope : entry->scopes) {
            const std::string title = scope.title_id ?
                FormatTitleId(scope.title_id) : "Unknown";
            ImGui::BulletText(
                "Title %s | executable fingerprint v%u | %02X%02X%02X%02X...",
                title.c_str(), scope.executable_fingerprint_version,
                scope.executable_fingerprint[0],
                scope.executable_fingerprint[1],
                scope.executable_fingerprint[2],
                scope.executable_fingerprint[3]);
        }
    }
}

void ShaderBrowserWindow::DrawUsageTiming()
{
    const Entry *entry = SelectedEntry();
    if (!entry) return;
    ImGui::SeparatorText("Observed logical draws");
    ImGui::Text("This session: %s",
                entry->observed_in_session ?
                    UnsignedText(entry->draw_count).c_str() :
                    "Not observed");
    ImGui::TextDisabled(
        "Current route: %s. Route counters overlap and do not partition "
        "the draw total.",
        RouteLabel(entry->route));
    if (entry->observed_in_session) {
        ImGui::Text("Uber-path counter: %llu",
                    static_cast<unsigned long long>(entry->uber_draws));
        ImGui::Text("Specialized-path counter: %llu",
                    static_cast<unsigned long long>(
                        entry->specialized_draws));
        ImGui::TextDisabled(
            "The specialized-path counter also includes some skipped and "
            "replacement observations. Separate skip/replacement totals "
            "are not collected.");
    }
    ImGui::Text("First / last guest frame: %s / %s",
                entry->observed_in_session ?
                    UnsignedText(entry->first_frame).c_str() :
                    "Not observed",
                entry->observed_in_session ?
                    UnsignedText(entry->last_frame).c_str() :
                    "Not observed");

    ImGui::SeparatorText("Host preparation timings");
    if (ImGui::BeginTable("##shader-cpu-timing", 5,
                          ImGuiTableFlags_Borders |
                          ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Operation", ImGuiTableColumnFlags_WidthFixed,
                                175.0f * g_viewport_mgr.m_scale);
        ImGui::TableSetupColumn("Samples", ImGuiTableColumnFlags_WidthFixed,
                                75.0f * g_viewport_mgr.m_scale);
        ImGui::TableSetupColumn("Total ns", ImGuiTableColumnFlags_WidthFixed,
                                95.0f * g_viewport_mgr.m_scale);
        ImGui::TableSetupColumn("Mean ns", ImGuiTableColumnFlags_WidthFixed,
                                85.0f * g_viewport_mgr.m_scale);
        ImGui::TableSetupColumn("Min / max ns");
        ImGui::TableHeadersRow();
        const struct {
            const char *name;
            const DurationStats *stats;
        } rows[] = {
            {"Compile CPU", &entry->compile_cpu},
            {"Pipeline creation CPU", &entry->pipeline_cpu},
            {"Prepare CPU", &entry->prepare_cpu},
            {"Foreground stall CPU", &entry->foreground_stall_cpu},
        };
        for (const auto &row : rows) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(row.name);
            ImGui::TableNextColumn();
            if (row.stats->samples)
                ImGui::Text("%llu", static_cast<unsigned long long>(
                    row.stats->samples));
            else ImGui::TextDisabled("Not sampled");
            ImGui::TableNextColumn();
            if (row.stats->samples)
                ImGui::Text("%llu", static_cast<unsigned long long>(
                    row.stats->total_ns));
            else ImGui::TextDisabled("-");
            ImGui::TableNextColumn();
            if (row.stats->samples)
                ImGui::Text("%.0f", row.stats->AverageNs());
            else ImGui::TextDisabled("-");
            ImGui::TableNextColumn();
            if (row.stats->samples)
                ImGui::Text("%llu / %llu",
                    static_cast<unsigned long long>(row.stats->min_ns),
                    static_cast<unsigned long long>(row.stats->max_ns));
            else ImGui::TextDisabled("-");
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled(
        "Completed CPU samples only. These are not GPU execution or "
        "gameplay frame-time measurements.");
    ImGui::SeparatorText("Execution measurements");
    ImGui::TextDisabled(
        "GPU duration, percentiles, invocations, and visible pixels: "
        "not collected.");
}

void ShaderBrowserWindow::DrawOverrides()
{
    const Entry *entry = SelectedEntry();
    if (!entry) return;
    ImGui::TextDisabled(
        "Rules affect matching game draws only after explicit Apply. "
        "Hiding a catalog row and editing a preview draft do not apply a rule.");
    ImGui::TextWrapped("Scope: %s | %s",
                       FullDisplayId(*entry,
                                     m_snapshot.current_title_id).c_str(),
                       StageLabel(entry->key.stage));
    GetShaderOverrideUi().DrawPanel(
        *entry, m_snapshot.entries, m_snapshot.current_title_id,
        &m_action_message);
    ImGui::SeparatorText("Rule status");
    if (!m_action_message.empty())
        ImGui::TextWrapped("%s", m_action_message.c_str());
    ImGui::TextDisabled(
        "Requested and effective renderer actions are reported separately "
        "above. Rules may match every use of this shader identity.");
}

void ShaderBrowserWindow::DrawSettings()
{
    const bool settings_columns =
        ImGui::GetContentRegionAvail().x >=
        900.0f * g_viewport_mgr.m_scale;
    if (settings_columns) ImGui::Columns(2, "##settings-columns", false);
    ImGui::SeparatorText("Usage collection");
    if (ImGui::Checkbox("Collect live usage while this window is open",
                        &m_collection_requested)) {
        GetProvider().SetLiveCollectionEnabled(m_collection_requested);
        m_snapshot.live_collection_enabled = m_collection_requested;
    }
    DrawMutedWrapped(
        "Provider collection is independent of catalog visibility and "
        "private preview work.");
    ImGui::SetNextItemWidth(180.0f * g_viewport_mgr.m_scale);
    if (ImGui::Combo("Recent catalog window", &table_view.recent_index,
                     "Any time\0Last 30 frames\0Last 120 frames\0"
                     "Last 600 frames\0")) {
        m_filter_dirty = true;
    }
    DrawMutedWrapped(
        "The selected shader remains selected when it ages out of the list.");

    ImGui::SeparatorText("Preview & editing");
    DrawWorkbenchSharedSettings();

    ImGui::SeparatorText("Exports");
    DrawMutedWrapped(
        "Usage exports contain provider observations, not preview draws or "
        "unmeasured GPU data.");
    if (ImGui::Button("Usage CSV")) {
        const char *base = xemu_settings_get_base_path();
        const std::filesystem::path root = base ?
            std::filesystem::u8path(base) : std::filesystem::path();
        std::filesystem::path path;
        std::string error;
        m_action_message = ExportUsageSnapshot(m_snapshot, root, false,
                                               &path, &error) ?
            "Exported usage CSV to " + path.u8string() : error;
    }
    ImGui::SameLine();
    if (ImGui::Button("Usage JSON")) {
        const char *base = xemu_settings_get_base_path();
        const std::filesystem::path root = base ?
            std::filesystem::u8path(base) : std::filesystem::path();
        std::filesystem::path path;
        std::string error;
        m_action_message = ExportUsageSnapshot(m_snapshot, root, true,
                                               &path, &error) ?
            "Exported usage JSON to " + path.u8string() : error;
    }
    const Entry *entry = SelectedEntry();
    if (entry && entry->recipe_size) {
        ImGui::SameLine();
        if (ImGui::Button("Selected recipe")) ExportRecipe(*entry);
    }
    DrawMutedWrapped(
        "Generated GLSL and synthetic test exports are in their source "
        "and Live Preview views.");

    if (settings_columns) ImGui::NextColumn();
    ImGui::SeparatorText("Session overrides & replacement library");

    GetShaderOverrideUi().DrawSettings(&m_action_message);

    ImGui::SeparatorText("Persistence");
    bool database_enabled = g_config.shader_browser.database.enabled;
    if (ImGui::Checkbox("Enable persistent shader database",
                        &database_enabled)) {
        g_config.shader_browser.database.enabled = database_enabled;
        ApplyPersistenceSettings();
    }
    DrawMutedWrapped(
        "Stores shader identity, canonical guest recipes, title/build links, "
        "and lightweight metadata beside the xemu configuration. Disabling "
        "this setting does not delete existing data.");

    ImGui::BeginDisabled(!g_config.shader_browser.database.enabled);
    bool record_sessions =
        g_config.shader_browser.database.record_performance_sessions;
    if (ImGui::Checkbox("Record per-session performance history",
                        &record_sessions)) {
        g_config.shader_browser.database.record_performance_sessions =
            record_sessions;
        ApplyPersistenceSettings();
    }
    DrawMutedWrapped(
        "Keeps each game run separate. Min/max/count/total are stored; "
        "averages are derived per session instead of across the shader's life.");

    bool save_artifacts =
        g_config.shader_browser.database.save_external_artifacts;
    if (ImGui::Checkbox("Save generated/compiled shader artifacts externally",
                        &save_artifacts)) {
        g_config.shader_browser.database.save_external_artifacts =
            save_artifacts;
        ApplyPersistenceSettings();
    }
    DrawMutedWrapped(
        "Large GLSL/SPIR-V/compiler artifacts are files under the artifact "
        "folder. SQLite stores only their metadata and relative path so the "
        "files can be inspected or deleted manually.");
    ImGui::EndDisabled();

    if (ImGui::BeginTable("##database-settings", 2,
                          ImGuiTableFlags_BordersInnerV |
                          ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("##name", ImGuiTableColumnFlags_WidthFixed,
                                155.0f * g_viewport_mgr.m_scale);
        ImGui::TableSetupColumn("##value",
                                ImGuiTableColumnFlags_WidthStretch);
        DrawKeyValue("Database status", m_snapshot.database_status.empty() ?
                     "Disabled" : m_snapshot.database_status);
        DrawKeyValue("Database path", m_snapshot.database_path.empty() ?
                     "Not opened" : m_snapshot.database_path);
        DrawKeyValue("Artifact folder", m_snapshot.artifact_root.empty() ?
                     "Not configured" : m_snapshot.artifact_root);
        DrawKeyValue("Schema version",
                     UnsignedText(m_snapshot.database_schema_version));
        DrawKeyValue("Journal", m_snapshot.database_journal_mode.empty() ?
                     "-" : m_snapshot.database_journal_mode);
        DrawKeyValue("Catalogued shaders",
                     UnsignedText(m_snapshot.stored_shader_count));
        DrawKeyValue("Known titles", UnsignedText(m_snapshot.title_count));
        DrawKeyValue("Performance sessions",
                     UnsignedText(m_snapshot.session_count));
        DrawKeyValue("Session shader rows",
                     UnsignedText(m_snapshot.session_stat_count));
        DrawKeyValue("External artifacts",
                     UnsignedText(m_snapshot.artifact_count));
        DrawKeyValue("Rejected artifacts",
                     UnsignedText(m_snapshot.rejected_artifact_count));
        DrawKeyValue("Failed artifact writes",
                     UnsignedText(m_snapshot.failed_artifact_count));
        if (m_snapshot.failed_artifact_count) {
            DrawKeyValue("Last artifact error",
                         m_snapshot.last_artifact_error);
        }
        DrawKeyValue("Database size", BytesText(m_snapshot.database_bytes));
        DrawKeyValue("WAL size", BytesText(m_snapshot.database_wal_bytes));
        DrawKeyValue("Artifact size",
                     BytesText(m_snapshot.external_artifact_bytes));
        DrawKeyValue("Pending DB writes",
                     UnsignedText(m_snapshot.pending_database_writes));
        DrawKeyValue("Pending live observations",
                     UnsignedText(m_snapshot.pending_observation_count));
        DrawKeyValue("Live session epoch",
                     UnsignedText(m_snapshot.session_epoch));
        ImGui::EndTable();
    }

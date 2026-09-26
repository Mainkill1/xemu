
void ShaderBrowserWindow::DrawOverview()
{
    const Entry *entry = SelectedEntry();
    if (!entry) {
        ImGui::TextDisabled("Select a shader to inspect it.");
        return;
    }

    std::string short_id = ShortDisplayId(
        *entry, m_snapshot.current_title_id);
    std::string full_id = FullDisplayId(
        *entry, m_snapshot.current_title_id);
    std::string canonical = PortableShaderHash(entry->key.hash);
    ImGui::Text("Shader: %s", short_id.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy ID")) {
        ImGui::SetClipboardText(full_id.c_str());
        m_action_message = "Copied game shader ID";
    }

    if (ImGui::BeginTable("##shader-summary", 2,
                          ImGuiTableFlags_BordersInnerV |
                          ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("##name", ImGuiTableColumnFlags_WidthFixed,
                                135.0f * g_viewport_mgr.m_scale);
        ImGui::TableSetupColumn("##value",
                                ImGuiTableColumnFlags_WidthStretch);
        DrawKeyValue("Stage", StageLabel(entry->key.stage));
        DrawKeyValue("Game shader ID", full_id);
        DrawKeyValue("Shader hash", canonical);
        DrawKeyValue("Database catalog",
                     m_snapshot.database_write_failed ?
                         "Unconfirmed after writer error" :
                     entry->stored_in_database ?
                         "Catalogued (write-behind)" : "Not catalogued");
        DrawKeyValue("Recipe format",
                     entry->recipe_format_version ?
                         std::to_string(entry->recipe_format_version) :
                         "Identity only");
        DrawKeyValue("Recipe size", BytesText(entry->recipe_size));
        DrawKeyValue("Title/build scope", ScopeSummary(*entry));
        DrawKeyValue("Session draws",
                     entry->observed_in_session ?
                         UnsignedText(entry->draw_count) : "Not observed");
        DrawKeyValue("First used",
                     entry->observed_in_session ?
                         UnsignedText(entry->first_frame) : "Not observed");
        DrawKeyValue("Last used", FormatLastUsed(
            entry->last_frame, m_snapshot.current_frame,
            entry->observed_in_session));
        DrawDurationRows(*entry);
        DrawKeyValue("Pipeline variants",
                     UnsignedText(entry->pipeline_variant_count));
        DrawKeyValue("Specialized draws",
                     UnsignedText(entry->specialized_draws));
        DrawKeyValue("Uber draws", UnsignedText(entry->uber_draws));
        DrawKeyValue("Replacement draws",
                     UnsignedText(entry->replacement_draws));
        DrawKeyValue("Compile failures",
                     UnsignedText(entry->compile_failures));
        DrawKeyValue("Status", StatusLabel(entry->status));
        DrawKeyValue("Route", RouteLabel(entry->route));
        DrawKeyValue("Readiness", ReadinessLabel(entry->readiness));
        ImGui::EndTable();
    }

    ImGui::SeparatorText("Shared binding variants");
    ImGui::TextDisabled("Shared values include all member shaders; do not sum across stages.");
    bool has_binding = false;
    for (const BindingVariant &variant : m_snapshot.binding_variants) {
        if (std::find(variant.members.begin(), variant.members.end(),
                      entry->key) == variant.members.end()) continue;
        has_binding = true;
        ImGui::PushID(&variant);
        ImGui::Text("%s / %s / variant %llu",
                    variant.backend == XEMU_SHADER_BROWSER_BACKEND_GL ?
                        "OpenGL" : "Vulkan",
                    RouteLabel(variant.route),
                    static_cast<unsigned long long>(variant.variant_id));
        std::string members;
        for (const ShaderKey &member : variant.members) {
            if (!members.empty()) members += ", ";
            members += StageLabel(member.stage);
        }
        ImGui::TextDisabled("Members: %s", members.c_str());
        static const char *names[] = {
            "", "", "", "", "Link / pipeline CPU", "Foreground stall",
            "Draw submit CPU", "Sampled GPU draw interval",
        };
        for (size_t metric = 4; metric < variant.timing.size(); ++metric) {
            if (!variant.timing[metric].samples) continue;
            ImGui::Text("%s: %s", names[metric],
                        FormatDurationNs(variant.timing[metric]).c_str());
        }
        ImGui::TextDisabled("Sampled draws represented: %llu",
            static_cast<unsigned long long>(variant.sampled_draws));
        ImGui::PopID();
    }
    if (!has_binding) ImGui::TextDisabled("No shared sample in this session.");

    if (!entry->scopes.empty() &&
        ImGui::CollapsingHeader("Known title/build associations")) {
        for (const ShaderScope &scope : entry->scopes) {
            std::string title = scope.title_id ?
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

    ImGui::Separator();
    ImGui::TextDisabled(
        "Canonical recipes stay in the catalog; selected host details "
        "remain transient. Runtime overrides arrive in Stage 3.");
}

void ShaderBrowserWindow::DrawSettings()
{
    const bool monitoring_off =
        g_config.shader_browser.profiling.monitoring_level == 0;
    ImGui::BeginDisabled(monitoring_off);
    if (ImGui::Checkbox("Collect live usage while this window is open",
                        &m_collection_requested)) {
        GetProvider().SetLiveCollectionEnabled(m_collection_requested);
        m_snapshot.live_collection_enabled = m_collection_requested;
    }
    ImGui::EndDisabled();
    if (monitoring_off) {
        ImGui::TextDisabled(
            "Live monitoring is Off. Change it in the Advanced menu.");
    }
    ImGui::TextDisabled(
        "Live browsing works without database persistence. Persistent storage "
        "is opt-in and never runs on the draw hot path.");

    ImGui::SeparatorText("Persistence");
    bool database_enabled = g_config.shader_browser.database.enabled;
    if (ImGui::Checkbox("Enable persistent shader database",
                        &database_enabled)) {
        g_config.shader_browser.database.enabled = database_enabled;
        ApplyPersistenceSettings();
    }
    ImGui::TextDisabled(
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
    ImGui::TextDisabled(
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
    ImGui::TextDisabled(
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


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
    ImGui::TextDisabled("Timing source: %s",
        m_snapshot.timing_session_id.empty() ? "Live" : "Recorded session");
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
        DrawDurationRows(*entry, !m_snapshot.timing_session_id.empty());
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
    ImGui::TextDisabled("Foreground stall classifies link/pipeline work and overlaps that cost.");
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
        ImGui::TextDisabled("Provenance: %s%s%s%s",
            variant.flags & XEMU_SHADER_BROWSER_SAMPLE_FOREGROUND ?
                "foreground " : "",
            variant.flags & XEMU_SHADER_BROWSER_SAMPLE_BACKGROUND ?
                "background " : "",
            variant.flags & XEMU_SHADER_BROWSER_SAMPLE_CACHED ?
                "cached " : "",
            variant.flags & XEMU_SHADER_BROWSER_SAMPLE_SAMPLED ?
                "sampled" : "");
        static const char *names[] = {
            "", "", "", "", "Link / pipeline CPU", "Foreground stall",
            "Draw submit CPU", "Sampled GPU draw interval",
            "Binding preparation CPU",
        };
        for (size_t metric = 4; metric < variant.timing.size(); ++metric) {
            if (!variant.timing[metric].samples) continue;
            ImGui::Text("%s: %s", names[metric],
                        FormatDurationNs(variant.timing[metric]).c_str());
        }
        ImGui::TextDisabled("Sampled CPU/GPU draws: %llu / %llu",
            static_cast<unsigned long long>(variant.sampled_cpu_draws),
            static_cast<unsigned long long>(variant.sampled_gpu_draws));
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

    ImGui::SeparatorText("Profiling state");
    const bool vulkan =
        g_config.display.renderer == CONFIG_DISPLAY_RENDERER_VULKAN;
    const bool supported = vulkan ? m_snapshot.vk_gpu_supported :
                                    m_snapshot.gl_gpu_supported;
    const uint32_t pending = vulkan ? m_snapshot.pending_vk_gpu_queries :
                                      m_snapshot.pending_gl_gpu_queries;
    ImGui::Text("Active backend: %s", vulkan ? "Vulkan" : "OpenGL");
    ImGui::Text("GPU timestamps: %s",
                supported ? "Supported" : "Unsupported by active backend/device");
    ImGui::Text("Pending GPU queries: %u", pending);
    ImGui::Text("Dropped profiling samples: %llu",
                static_cast<unsigned long long>(
                    m_snapshot.dropped_performance_samples));
    ImGui::TextDisabled(
        "Monitoring level and sample budgets are in Advanced settings.");

    ImGui::SeparatorText("Timing source");
    const SessionSummary *selected_session = nullptr;
    for (const SessionSummary &session : m_snapshot.timing_sessions) {
        if (session.session_id == m_snapshot.timing_session_id) {
            selected_session = &session;
            break;
        }
    }
    std::string source_label = selected_session ?
        (selected_session->ended_unix_ms ? "Previous session: " :
                                           "Current recorded session: ") +
        selected_session->session_id : "Live session";
    if (ImGui::BeginCombo("Timing source", source_label.c_str())) {
        if (ImGui::Selectable("Live session", !selected_session)) {
            GetProvider().SetTimingSession("");
            m_next_poll_ms = 0;
        }
        for (const SessionSummary &session : m_snapshot.timing_sessions) {
            std::string label =
                (session.ended_unix_ms ? "Previous: " : "Current: ") +
                session.session_id + " | " + session.renderer;
            if (ImGui::Selectable(label.c_str(),
                                  selected_session == &session)) {
                GetProvider().SetTimingSession(session.session_id);
                m_next_poll_ms = 0;
            }
        }
        ImGui::EndCombo();
    }
    if (selected_session) {
        ImGui::TextDisabled("Host: %s | CPU: %s",
            selected_session->host_os.empty() ? "Unknown" :
                selected_session->host_os.c_str(),
            selected_session->cpu_model.empty() ? "Unknown" :
                selected_session->cpu_model.c_str());
        ImGui::TextDisabled("GPU: %s | driver: %s | vendor/device: %04X/%04X",
            selected_session->gpu_name.empty() ? "Unknown" :
                selected_session->gpu_name.c_str(),
            selected_session->gpu_driver.empty() ? "Unknown" :
                selected_session->gpu_driver.c_str(),
            selected_session->gpu_vendor_id,
            selected_session->gpu_device_id);
        ImGui::TextDisabled(
            "Revision: %s | resolution scale: %u | uber: %s | cache: %s",
            selected_session->xemu_revision.c_str(),
            selected_session->internal_resolution_scale,
            selected_session->ubershader_mode.c_str(),
            selected_session->shader_cache_enabled ? "On" : "Off");
    }

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

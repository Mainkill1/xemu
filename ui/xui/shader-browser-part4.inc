    if (!m_snapshot.database_path.empty()) {
        if (ImGui::SmallButton("Copy database path")) {
            ImGui::SetClipboardText(m_snapshot.database_path.c_str());
            m_action_message = "Copied database path";
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy artifact folder")) {
            ImGui::SetClipboardText(m_snapshot.artifact_root.c_str());
            m_action_message = "Copied artifact folder";
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Open artifact folder")) {
            std::string error;
            if (!OpenDirectory(m_snapshot.artifact_root, &error)) {
                m_action_message = error;
            }
        }
    }

    ImGui::SeparatorText("Management");
    if (ImGui::Button("Refresh now")) {
        m_next_poll_ms = 0;
        RefreshSnapshot();
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear live session")) {
        pgraph_shader_browser_transition(
            [](void *) { GetProvider().ClearLiveSession(); }, nullptr);
        m_has_selection = false;
        m_next_poll_ms = 0;
        RefreshSnapshot();
    }

    ImGui::BeginDisabled(!m_snapshot.database_open);
    if (ImGui::Button("Flush database")) {
        std::string error;
        if (GetProvider().FlushDatabase(&error)) {
            m_action_message = "Shader database flushed";
        } else {
            m_action_message = error;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Quick check")) {
        std::string result;
        std::string error;
        if (GetProvider().QuickCheckDatabase(&result, &error)) {
            m_action_message = "SQLite quick_check: " + result;
        } else {
            m_action_message = error;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Optimize")) {
        std::string error;
        if (GetProvider().OptimizeDatabase(&error)) {
            m_action_message = "Shader database optimized";
        } else {
            m_action_message = error;
        }
    }
    if (ImGui::Button("Reconcile deleted artifact files")) {
        size_t removed = 0;
        std::string error;
        if (GetProvider().ReconcileArtifacts(&removed, &error)) {
            m_action_message = "Removed " + std::to_string(removed) +
                               " missing artifact references";
        } else {
            m_action_message = error;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete performance history...")) {
        ImGui::OpenPopup("Delete shader performance history?");
    }
    ImGui::EndDisabled();

    if (ImGui::BeginPopupModal("Delete shader performance history?",
                               nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped(
            "This deletes per-session shader performance rows only. "
            "Shader identities, canonical recipes, title/build links, and "
            "external shader artifacts are kept.");
        if (ImGui::Button("Delete history")) {
            std::string error;
            if (GetProvider().ClearPerformanceHistory(&error)) {
                m_action_message = "Deleted shader performance history";
                m_next_poll_ms = 0;
                RefreshSnapshot();
                ImGui::CloseCurrentPopup();
            } else {
                m_action_message = error;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::TextDisabled(
        "Deleting files inside the artifact folder is supported. Use "
        "Reconcile after manual cleanup. Canonical recipes remain in the "
        "database and can regenerate host artifacts later.");

    if (m_snapshot.database_write_failed) {
        ImGui::Separator();
        ImGui::TextWrapped(
            "Database writer reported an error. Live Shader Browser data "
            "remains available; inspect the database status before relying "
            "on persistent records.");
    }
    if (!m_action_message.empty()) {
        ImGui::Separator();
        ImGui::TextDisabled("%s", m_action_message.c_str());
    }
}

void ShaderBrowserWindow::DrawLivePreview()
{
    PreviewService &service = GetPreviewService();
    const uint64_t now_ns =
        static_cast<uint64_t>(g_get_monotonic_time()) * UINT64_C(1000);
    service.SetVisible(true, now_ns);

    PreviewStatus status{};
    service.CopyStatus(&status);

    bool enabled = status.enabled;
    if (ImGui::Checkbox("Enable preview", &enabled)) {
        service.SetEnabled(enabled);
    }
    ImGui::SameLine();

    int mode = static_cast<int>(status.requested_mode);
    ImGui::SetNextItemWidth(185.0f * g_viewport_mgr.m_scale);
    if (ImGui::Combo("##preview-mode", &mode,
                     "Normal\0Uber Shader\0Replacement Shader\0"
                     "Visualize\0")) {
        service.SetRequestedMode(static_cast<PreviewMode>(mode));
    }

    const Entry *entry = SelectedEntry();
    if (entry) {
        PreviewSelection selection{};
        selection.shader = entry->key;
        selection.session_epoch = m_snapshot.session_epoch;
        selection.renderer_epoch = 0;
        selection.backend =
            g_config.display.renderer == CONFIG_DISPLAY_RENDERER_VULKAN ?
                PreviewBackend::Vulkan : PreviewBackend::OpenGL;
        selection.mode = static_cast<PreviewMode>(mode);

        uint32_t preferred_title = m_snapshot.current_title_id ?
            m_snapshot.current_title_id : PrimaryTitleId(*entry);
        auto scope = std::find_if(
            entry->scopes.begin(), entry->scopes.end(),
            [preferred_title](const ShaderScope &candidate) {
                return candidate.title_id == preferred_title;
            });
        if (scope != entry->scopes.end()) {
            selection.scope = *scope;
        } else if (!entry->scopes.empty()) {
            selection.scope = entry->scopes.front();
        } else {
            selection.scope.title_id = preferred_title;
        }
        service.SetSelection(selection, now_ns);
    } else {
        service.ClearSelection();
    }

    service.CopyStatus(&status);
    ImGui::SameLine();
    ImGui::BeginDisabled(!status.has_packet || !status.guest_paused ||
                         status.work_active || status.prepared);
    if (ImGui::Button("Prepare while paused")) {
        std::string error;
        if (!service.RequestPreparation(&error)) {
            m_action_message = error;
        }
    }
    ImGui::EndDisabled();

    if (ImGui::BeginTable("##preview-status", 2,
                          ImGuiTableFlags_BordersInnerV |
                          ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("##name", ImGuiTableColumnFlags_WidthFixed,
                                112.0f * g_viewport_mgr.m_scale);
        ImGui::TableSetupColumn("##value",
                                ImGuiTableColumnFlags_WidthStretch);
        DrawKeyValue("State", PreviewStateLabel(status.state));
        DrawKeyValue("Mode", PreviewModeLabel(status.requested_mode));
        DrawKeyValue("Pressure", PreviewPressureLabel(status.pressure));
        DrawKeyValue("Update ceiling",
                     status.update_hz ?
                         std::to_string(status.update_hz) + " Hz" : "Frozen");
        DrawKeyValue("Output slots",
                     std::to_string(status.free_slots) + " free / " +
                     std::to_string(status.ready_slots) + " ready / " +
                     std::to_string(status.leased_slots) + " leased");
        ImGui::EndTable();
    }

    ImGui::TextWrapped("%s", status.message.c_str());
    if (entry && entry->key.stage != Stage::Pixel) {
        ImGui::TextWrapped(
            "Stage 4 v1 intentionally supports pixel/fragment shaders first. "
            "The selected %s row remains available for inspection.",
            StageLabel(entry->key.stage));
    }

    ImVec2 available = ImGui::GetContentRegionAvail();
    float side = std::max(1.0f, std::min(available.x, available.y));
    if (ImGui::BeginChild("##preview-output", ImVec2(side, side), true)) {
        ImGui::TextDisabled("Synthetic inputs — not an in-game draw");
        ImGui::Separator();
        ImGui::TextWrapped(
            "The bounded Stage 4 request, preparation, throttling, and output "
            "lifetime foundation is active. Native OpenGL/Vulkan execution "
            "and the immutable recipe/input adapter are intentionally not "
            "connected in this draft, so enabling this panel cannot invoke "
            "the game renderer or create GPU work.");
        if (entry) {
            ImGui::Spacing();
            ImGui::Text("Selected: %s", StageLabel(entry->key.stage));
            ImGui::Text("Backend target: %s",
                        PreviewBackendLabel(
                            g_config.display.renderer ==
                                    CONFIG_DISPLAY_RENDERER_VULKAN ?
                                PreviewBackend::Vulkan :
                                PreviewBackend::OpenGL));
        }
    }
    ImGui::EndChild();
}

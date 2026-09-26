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
    bool enabled = false;
    int mode = 0;
    ImGui::BeginDisabled();
    ImGui::Checkbox("Enable preview", &enabled);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(185.0f * g_viewport_mgr.m_scale);
    ImGui::Combo("##preview-mode", &mode,
                 "Normal\0Uber Shader\0Replacement Shader\0"
                 "Visualize\0");
    ImGui::EndDisabled();

    ImVec2 available = ImGui::GetContentRegionAvail();
    float side = std::max(1.0f, std::min(available.x, available.y));
    if (ImGui::BeginChild("##preview-placeholder", ImVec2(side, side), true)) {
        ImGui::TextWrapped(
            "Live Preview is intentionally unavailable in Stage 1. Stage 4 "
            "adds isolated synthetic rendering and supported captured-draw "
            "replay without automatic screenshots or synchronous framebuffer "
            "readback.");
    }
    ImGui::EndChild();
}

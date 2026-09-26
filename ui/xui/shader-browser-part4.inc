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
    const uint64_t now_ns = PreviewNowNs();
    const bool guest_paused = runstate_check(RUN_STATE_PAUSED);
    service.SetGuestPaused(guest_paused);
    uint64_t completed_flips = 0;
    uint64_t flip_interval_ns = 0;
    nv2a_profile_preview_flip_snapshot(&completed_flips, &flip_interval_ns);
    service.UpdateHealth(m_preview_health_monitor.Sample(
        now_ns, completed_flips, flip_interval_ns, runstate_is_running()));

    PreviewStatus status{};
    service.CopyStatus(&status);

    bool enabled = status.enabled;
    if (ImGui::Checkbox("Enable preview", &enabled)) {
        service.SetEnabled(enabled);
        m_preview_packet_attempted = false;
    }
    ImGui::SameLine();

    int mode = static_cast<int>(status.requested_mode);
    ImGui::SetNextItemWidth(185.0f * g_viewport_mgr.m_scale);
    if (ImGui::Combo("##preview-mode", &mode,
                     "Normal\0Uber Shader\0Replacement Shader\0"
                     "Visualize\0")) {
        service.SetRequestedMode(static_cast<PreviewMode>(mode));
    }

    if (ImGui::CollapsingHeader("Synthetic fixture colors")) {
        static const char *labels[] = {
            "Top left", "Top right", "Bottom left", "Bottom right"
        };
        for (size_t i = 0; i < m_preview_colors.size(); ++i) {
            if (ImGui::ColorEdit4(labels[i], m_preview_colors[i].data())) {
                ++m_preview_input_revision;
                m_preview_packet_attempted = false;
            }
        }
    }

    const Entry *entry = SelectedEntry();
    PreviewSelection selection{};
    if (entry) {
        selection.shader = entry->key;
        selection.session_epoch = m_snapshot.session_epoch;
        selection.renderer_epoch = nv2a_profile_preview_renderer_epoch();
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

        const uint64_t detail_generation = m_detail_snapshot.generation;
        const uint64_t replacement_generation =
            selection.mode == PreviewMode::Replacement ?
                GetOverrideStore().Generation() : 0;
        if (enabled && entry->key.stage == Stage::Pixel &&
            (!m_preview_packet_attempted ||
             m_preview_packet_selection != selection ||
             m_preview_packet_detail_generation != detail_generation ||
             m_preview_packet_override_generation != replacement_generation)) {
            m_preview_packet_attempted = true;
            m_preview_packet_selection = selection;
            m_preview_packet_detail_generation = detail_generation;
            m_preview_packet_override_generation = replacement_generation;
            m_preview_packet_message.clear();
            if (!m_has_recipe || m_selected_recipe.key != entry->key) {
                m_preview_packet_message = "Canonical recipe is unavailable";
            } else {
                PreviewPacketInputs inputs{};
                inputs.selection = selection;
                inputs.recipe = m_selected_recipe;
                inputs.fixture_bytes.reserve(16);
                for (const auto &color : m_preview_colors) {
                    for (float component : color) {
                        inputs.fixture_bytes.push_back(static_cast<uint8_t>(
                            std::clamp(static_cast<int>(component * 255.0f +
                                                        0.5f), 0, 255)));
                    }
                }
                inputs.generator_abi = 1;
                inputs.interface_abi = 1;
                inputs.input_revision = m_preview_input_revision;
                inputs.view_revision = 1;
                bool source_ready = true;
                if (selection.mode == PreviewMode::Replacement) {
                    const OverrideBackend backend =
                        selection.backend == PreviewBackend::OpenGL ?
                            OverrideBackend::OpenGL : OverrideBackend::Vulkan;
                    auto replacement = GetShaderOverrideUi().
                        AcquireSelectedReplacement(backend);
                    if (!replacement) {
                        m_preview_packet_message =
                            "Selected replacement payload is unavailable";
                        source_ready = false;
                    } else if (!IsReplacementCompatible(
                                   entry->key, replacement->descriptor,
                                   backend, &m_preview_packet_message)) {
                        source_ready = false;
                    } else {
                        inputs.replacement_id = replacement->descriptor.id;
                        inputs.replacement_revision =
                            replacement->descriptor.content_revision;
                        inputs.interface_abi =
                            replacement->descriptor.interface_version;
                        inputs.source = backend == OverrideBackend::OpenGL ?
                            replacement->opengl_source :
                            replacement->vulkan_source;
                    }
                } else {
                    source_ready = CopyPreviewFragmentSource(
                        selection, m_detail_snapshot, &inputs.source,
                        &m_preview_packet_message);
                }
                if (source_ready) {
                    inputs.partner_source = BuildPreviewSyntheticVertexSource(
                        inputs.source, selection.backend);
                    PreviewPacket packet{};
                    if (BuildPreviewPacket(inputs, &packet,
                                           &m_preview_packet_message)) {
                        service.SubmitPacket(std::move(packet), now_ns,
                                             &m_preview_packet_message);
                    }
                }
            }
        }
    } else {
        service.ClearSelection();
        m_preview_packet_attempted = false;
    }

    service.CopyStatus(&status);
    ImGui::BeginDisabled(!status.has_packet || !status.guest_paused ||
                         status.work_active || status.prepared);
    if (ImGui::Button("Prepare while paused")) {
        std::string error;
        if (!service.RequestPreparation(&error)) {
            m_action_message = error;
        } else if (selection.backend == PreviewBackend::OpenGL &&
                   !GetPreviewGlExecutor().StartWhilePaused(&error)) {
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
    if (!m_preview_packet_message.empty()) {
        ImGui::TextWrapped("Preview inputs: %s",
                           m_preview_packet_message.c_str());
    }
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
        if (entry && selection.backend == PreviewBackend::OpenGL) {
            GetPreviewGlExecutor().DrawImage(side - 16.0f,
                                             enabled ? &selection : nullptr,
                                             now_ns);
        } else {
            ImGui::TextWrapped(
                "Private Vulkan execution and presentation are still being "
                "built; no Vulkan preview GPU work is submitted.");
        }
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

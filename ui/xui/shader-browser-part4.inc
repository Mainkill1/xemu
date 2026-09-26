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

    const bool visualize = mode == static_cast<int>(PreviewMode::Visualize);
    if (visualize) {
        auto channel = static_cast<PreviewChannel>(m_preview_view_channel);
        if (ImGui::BeginCombo("Visualize channel",
                              PreviewChannelLabel(channel))) {
            for (int i = 0; i < static_cast<int>(PreviewChannel::Count); ++i) {
                auto option = static_cast<PreviewChannel>(i);
                ImGui::BeginDisabled(!PreviewChannelAvailable(option));
                if (ImGui::Selectable(PreviewChannelLabel(option),
                                      channel == option))
                    m_preview_view_channel = i;
                ImGui::EndDisabled();
            }
            ImGui::EndCombo();
        }
        ImGui::TextWrapped(
            "%s", PreviewChannelProvenance(
                      static_cast<PreviewChannel>(m_preview_view_channel)));
        ImGui::TextDisabled(
            "Exact shader discard mask: Unsupported (coverage unavailable).");
    }
    service.EditChannel(
        visualize ? static_cast<PreviewChannel>(m_preview_view_channel) :
                    PreviewChannel::FinalRGBA);

    int mesh = static_cast<int>(m_preview_scene.mesh);
    bool scene_changed = ImGui::Combo("Mesh", &mesh, "Quad\0Sphere\0Cube\0");
    m_preview_scene.mesh = static_cast<PreviewMesh>(mesh);
    scene_changed |= ImGui::SliderFloat("Orbit yaw", &m_preview_scene.yaw, -180, 180);
    scene_changed |= ImGui::SliderFloat("Orbit pitch", &m_preview_scene.pitch, -85, 85);
    scene_changed |= ImGui::SliderFloat("Zoom distance", &m_preview_scene.distance, 2.5f, 12);
    scene_changed |= ImGui::SliderFloat2("Pan", m_preview_scene.pan.data(), -2, 2);
    if (ImGui::Button("Reset camera")) {
        auto mesh_kind = m_preview_scene.mesh;
        m_preview_scene = {};
        m_preview_scene.mesh = mesh_kind;
        scene_changed = true;
    }
    if (scene_changed) service.EditScene(m_preview_scene);

    int policy = static_cast<int>(m_preview_update_policy);
    if (ImGui::Combo("Update policy", &policy, "On dirty\0Continuous\0")) {
        m_preview_update_policy = static_cast<PreviewUpdatePolicy>(policy);
        m_preview_packet_attempted = false;
    }
    if (m_preview_update_policy == PreviewUpdatePolicy::Continuous) {
        ImGui::TextWrapped("Synthetic animation cycles vertex colors and UVs. "
                           "It does not supply the game's time uniforms.");
        if (ImGui::Button(status.clock.playing ? "Pause preview" : "Play preview")) {
            service.EditClock(status.clock.playing ? PreviewClockAction::Pause :
                              PreviewClockAction::Play, 0, now_ns);
        }
        ImGui::SameLine();
        if (ImGui::Button("Restart"))
            service.EditClock(PreviewClockAction::Restart, 0, now_ns);
        float time = static_cast<float>(status.clock.time_seconds);
        if (ImGui::DragFloat("Time (seconds)", &time, 0.02f, 0.0f,
                             status.clock.loop ? status.clock.loop_seconds : 1.0e9f))
            service.EditClock(PreviewClockAction::Scrub, time, now_ns);
        float speed = static_cast<float>(status.clock.speed);
        if (ImGui::SliderFloat("Speed", &speed, 0.05f, 8.0f, "%.2fx"))
            service.EditClock(PreviewClockAction::Speed, speed, now_ns);
        bool loop = status.clock.loop;
        if (ImGui::Checkbox("Loop", &loop))
            service.EditClock(PreviewClockAction::Loop, loop, now_ns);
        float length = static_cast<float>(status.clock.loop_seconds);
        if (ImGui::DragFloat("Loop length (seconds)", &length, 0.1f, 0.1f, 3600.0f))
            service.EditClock(PreviewClockAction::LoopLength, length, now_ns);
        ImGui::Text("Clock sample: %llu", static_cast<unsigned long long>(status.clock.frame));
    }

    auto fixture_changed = [this] {
        ++m_preview_input_revision;
        m_preview_packet_attempted = false;
    };

    auto apply_fixture_profile = [this](PreviewFixtureProfile profile) {
        m_preview_fixture = MakePreviewFixture(profile);
        for (size_t i = 0; i < 4; ++i)
            for (size_t c = 0; c < 4; ++c) {
                m_preview_colors[i][c] =
                    m_preview_fixture.corner_colors[i][c] / 255.0f;
                m_preview_texture_colors[i][c] =
                    m_preview_fixture.texture_texels[i][c] / 255.0f;
            }
        m_preview_fog_color = m_preview_fixture.fog_color;
        m_preview_alpha_reference = m_preview_fixture.alpha_reference;
        m_preview_uv_scale = m_preview_fixture.uv_scale;
        m_preview_uv_offset = m_preview_fixture.uv_offset;
        m_preview_constant_color = m_preview_fixture.constant_color;
    };
    if (ImGui::CollapsingHeader("Fixture profile and inputs",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        const char *names[] = {
            "Recipe suggestion",     "Flat Color",          "UV Gradient",
            "Checker / UV Grid",     "Alpha Gradient",      "Multi-texture",
            "Normal-like Direction", "Cubemap / Direction", "Fog / Depth Ramp",
            "All Inputs Diagnostic"
        };
        int choice = m_preview_profile + 1;
        if (ImGui::Combo("Profile", &choice, names, 10)) {
            m_preview_profile = choice - 1;
            m_preview_fixture_suggested = false;
            if (choice)
                apply_fixture_profile(
                    static_cast<PreviewFixtureProfile>(choice - 1));
            fixture_changed();
        }
        if (m_preview_profile < 0)
            ImGui::TextDisabled(
                "Recipe profile: %s",
                m_preview_fixture_suggested ?
                    PreviewFixtureProfileName(m_preview_fixture.profile) :
                    "Unavailable; explicit inputs shown");
        const char *colors[] = { "D0", "D1", "B0", "B1" };
        for (int i = 0; i < 4; ++i) {
            if (ImGui::ColorEdit4(colors[i],
                                  m_preview_fixture.colors[i].data()))
                fixture_changed();
            int texture = static_cast<int>(m_preview_fixture.textures[i]);
            std::string label = "T" + std::to_string(i) + " texture";
            if (ImGui::Combo(label.c_str(), &texture, names + 1, 9)) {
                m_preview_fixture.textures[i] =
                    static_cast<PreviewFixtureProfile>(texture);
                fixture_changed();
            }
        }
        if (ImGui::SliderFloat("Fog scalar", &m_preview_fixture.fog, 0, 1))
            fixture_changed();
        if (ImGui::SliderFloat3("Cube direction",
                                m_preview_fixture.cube_direction.data(), -1, 1))
            fixture_changed();
        const char *faces[] = { "+X", "-X", "+Y", "-Y", "+Z", "-Z" };
        for (int face = 0; face < 6; ++face) {
            if (face)
                ImGui::SameLine();
            if (ImGui::Button(faces[face])) {
                m_preview_fixture.cube_direction = {};
                m_preview_fixture.cube_direction[face / 2] = face % 2 ? -1 : 1;
                fixture_changed();
            }
        }
        ImGui::TextWrapped(
            "Preview-owned inputs only. Unused source inputs have no effect. "
            "Arrays, integer, shadow and storage textures are Unsupported.");
    }
    if (ImGui::CollapsingHeader("Synthetic fixture colors")) {
        static const char *labels[] = {
            "Top left", "Top right", "Bottom left", "Bottom right"
        };
        for (size_t i = 0; i < m_preview_colors.size(); ++i) {
            if (ImGui::ColorEdit4(labels[i], m_preview_colors[i].data())) {
                fixture_changed();
            }
        }
    }
    if (ImGui::CollapsingHeader("Synthetic texture and UV")) {
        static const char *texel_labels[] = {
            "Texture bottom left", "Texture bottom right",
            "Texture top left", "Texture top right"
        };
        for (size_t i = 0; i < m_preview_texture_colors.size(); ++i) {
            if (ImGui::ColorEdit4(texel_labels[i],
                                  m_preview_texture_colors[i].data())) {
                fixture_changed();
            }
        }
        if (ImGui::DragFloat2("UV scale", m_preview_uv_scale.data(),
                              0.01f, -4.0f, 4.0f)) {
            fixture_changed();
        }
        if (ImGui::DragFloat2("UV offset", m_preview_uv_offset.data(),
                              0.01f, -4.0f, 4.0f)) {
            fixture_changed();
        }
        if (ImGui::Checkbox("Linear texture filter",
                            &m_preview_linear_filter)) fixture_changed();
        if (ImGui::Checkbox("Repeat texture wrap",
                            &m_preview_repeat_wrap)) fixture_changed();
    }
    if (ImGui::CollapsingHeader("Synthetic constants and alpha")) {
        if (ImGui::ColorEdit4("Combiner constant",
                              m_preview_constant_color.data())) {
            fixture_changed();
        }
        if (ImGui::ColorEdit4("Fog color", m_preview_fog_color.data())) {
            fixture_changed();
        }
        if (ImGui::SliderInt("Alpha reference", &m_preview_alpha_reference,
                             0, 255)) {
            fixture_changed();
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
        XemuShaderBrowserScope live_capture_scope{};
        uint64_t live_capture_generation =
            xemu_shader_browser_copy_current_scope(&live_capture_scope);
        ShaderScope exact_capture_scope{};
        std::string capture_scope_reason;
        bool capture_scope_ready = FindCurrentCaptureScope(
            *entry, live_capture_scope, &exact_capture_scope,
            &capture_scope_reason);
        if (capture_scope_ready) selection.scope = exact_capture_scope;
        service.SetSelection(selection, now_ns);

        if (m_capture_selection != selection || !capture_scope_ready ||
            (GetCaptureStore().Armed() &&
             m_capture_scope_generation != live_capture_generation)) {
            auto prior = GetCaptureStore().Status().kind;
            if (prior != CaptureStatusKind::Idle) {
                GetCaptureStore().Cancel();
                m_capture_message = "Capture cancelled: selection changed";
            }
            m_captured_draw.reset();
        }
        if (ImGui::Button("Capture selected draw")) {
            XemuShaderCaptureRequest request{};
            request.title_id = selection.scope.title_id;
            request.fingerprint_version =
                selection.scope.executable_fingerprint_version;
            std::memcpy(request.fingerprint,
                        selection.scope.executable_fingerprint.data(),
                        sizeof(request.fingerprint));
            request.selected.stage = static_cast<uint32_t>(entry->key.stage);
            std::memcpy(request.selected.hash, entry->key.hash.bytes.data(),
                        sizeof(request.selected.hash));
            request.backend = selection.backend == PreviewBackend::OpenGL ? 1 :
                              selection.backend == PreviewBackend::Vulkan ? 2 : 0;
            request.session_epoch = selection.session_epoch;
            request.renderer_epoch = selection.renderer_epoch;
            request.scope_generation = live_capture_generation;
            request.deadline_ns = now_ns + UINT64_C(5000000000);
            m_capture_selection = selection;
            m_capture_scope_generation = live_capture_generation;
            m_captured_draw.reset();
            m_capture_message.clear();
            if (capture_scope_ready && request.backend && request.title_id &&
                request.scope_generation &&
                entry->key.hash.version == 1 &&
                entry->key.stage != Stage::Any) {
                GetCaptureStore().Request(request);
            } else {
                m_capture_message = capture_scope_ready ?
                    "Capture needs a resolved game shader" :
                    capture_scope_reason;
            }
        }
        GetCaptureStore().Expire(now_ns);
        auto capture_status = GetCaptureStore().Status();
        if (capture_status.kind == CaptureStatusKind::Captured) {
            CapturedDraw captured;
            if (GetCaptureStore().Take(&captured)) {
                if (captured.header.context.backend == 1) {
                    char *vertex = xemu_shader_capture_generate_gl_source(
                        captured.shader_state.data(),
                        captured.shader_state.size(),
                        captured.header.stages[0].stage);
                    char *pixel = xemu_shader_capture_generate_gl_source(
                        captured.shader_state.data(),
                        captured.shader_state.size(), 2);
                    char *geometry = captured.header.stage_count == 3 ?
                        xemu_shader_capture_generate_gl_source(
                            captured.shader_state.data(),
                            captured.shader_state.size(), 3) : nullptr;
                    if (vertex) captured.vertex_source = vertex;
                    if (geometry) captured.geometry_source = geometry;
                    if (pixel) captured.pixel_source = pixel;
                    g_free(vertex);
                    g_free(geometry);
                    g_free(pixel);
                }
                std::string reason;
                if (SealCapturedDraw(&captured, &reason)) {
                    m_capture_message =
                        "Approximate game draw captured (" +
                        std::to_string(capture_status.duration_ns / 1000) +
                        " us draw-boundary copy). " +
                        captured.substitution;
                    m_captured_draw = std::move(captured);
                } else {
                    m_capture_message = "Unsupported game draw: " + reason;
                }
            }
        } else if (capture_status.kind == CaptureStatusKind::Unsupported ||
                   capture_status.kind == CaptureStatusKind::Expired) {
            m_capture_message = capture_status.reason;
        } else if (capture_status.kind == CaptureStatusKind::Armed) {
            m_capture_message = capture_status.reason;
        }
        if (!m_capture_message.empty())
            ImGui::TextWrapped("%s", m_capture_message.c_str());

        PreviewCompileKey attempted{};
        attempted.selection = selection;
        const uint64_t detail_generation = m_detail_snapshot.generation;
        const uint64_t replacement_generation =
            selection.mode == PreviewMode::Replacement ?
                GetOverrideStore().Generation() : 0;
        const bool scope_available = std::find(
            entry->scopes.begin(), entry->scopes.end(),
            selection.scope) != entry->scopes.end();
        if (enabled && entry->key.stage == Stage::Pixel &&
            (!m_preview_packet_attempted ||
             m_preview_packet_selection != selection ||
             m_preview_packet_detail_generation != detail_generation ||
             m_preview_packet_override_generation != replacement_generation ||
             m_preview_packet_scope_available != scope_available)) {
            const bool new_selection = m_preview_packet_selection != selection;
            if (new_selection)
                m_preview_fixture_suggested = false;
            if (m_preview_profile < 0 &&
                (new_selection || !m_preview_fixture_suggested) &&
                m_has_recipe && m_selected_recipe.key == entry->key) {
                PreviewFixtureProfile suggested;
                std::string reason;
                if (SuggestPreviewFixture(m_selected_recipe, &suggested,
                                          &reason)) {
                    apply_fixture_profile(suggested);
                    m_preview_fixture_suggested = true;
                }
            }
            m_preview_packet_attempted = true;
            m_preview_packet_selection = selection;
            m_preview_packet_detail_generation = detail_generation;
            m_preview_packet_override_generation = replacement_generation;
            m_preview_packet_scope_available = scope_available;
            m_preview_packet_message.clear();
            if (!m_has_recipe || m_selected_recipe.key != entry->key) {
                m_preview_packet_message = "Canonical recipe is unavailable";
            } else {
                PreviewPacketInputs inputs{};
                inputs.selection = selection;
                inputs.recipe = m_selected_recipe;
                bool scope_ready = AttachPreviewSelectionScope(
                    *entry, selection, &inputs.recipe,
                    &m_preview_packet_message);
                PreviewSyntheticFixture fixture = m_preview_fixture;
                auto encode_color = [](const std::array<float, 4> &color) {
                    std::array<uint8_t, 4> encoded{};
                    for (size_t i = 0; i < encoded.size(); ++i) {
                        encoded[i] = static_cast<uint8_t>(std::clamp(
                            static_cast<int>(color[i] * 255.0f + 0.5f),
                            0, 255));
                    }
                    return encoded;
                };
                for (size_t i = 0; i < m_preview_colors.size(); ++i) {
                    fixture.corner_colors[i] = encode_color(m_preview_colors[i]);
                    fixture.texture_texels[i] =
                        encode_color(m_preview_texture_colors[i]);
                }
                fixture.uv_scale = m_preview_uv_scale;
                fixture.uv_offset = m_preview_uv_offset;
                fixture.constant_color = m_preview_constant_color;
                fixture.fog_color = m_preview_fog_color;
                fixture.alpha_reference = static_cast<uint8_t>(
                    m_preview_alpha_reference);
                fixture.linear_filter = m_preview_linear_filter ? 1 : 0;
                fixture.repeat_wrap = m_preview_repeat_wrap ? 1 : 0;
                inputs.fixture_bytes = EncodePreviewSyntheticFixture(fixture);
                inputs.generator_abi = 1;
                inputs.interface_abi = 1;
                inputs.update_policy = m_preview_update_policy;
                inputs.input_revision = m_preview_input_revision;
                inputs.scene = m_preview_scene;
                bool source_ready = scope_ready;
                if (scope_ready && selection.mode == PreviewMode::Replacement) {
                    const OverrideBackend backend =
                        selection.backend == PreviewBackend::OpenGL ?
                            OverrideBackend::OpenGL : OverrideBackend::Vulkan;
                    auto replacement = GetShaderOverrideUi().
                        AcquireSelectedReplacement(backend);
                    if (!replacement) {
                        m_preview_packet_message =
                            "Selected replacement payload is unavailable";
                        source_ready = false;
                    } else {
                        attempted.replacement_id = replacement->descriptor.id;
                        attempted.replacement_revision =
                            replacement->descriptor.content_revision;
                        source_ready = IsReplacementCompatible(
                            entry->key, replacement->descriptor, backend,
                            &m_preview_packet_message);
                        inputs.replacement_id = replacement->descriptor.id;
                        inputs.replacement_revision =
                            replacement->descriptor.content_revision;
                        inputs.interface_abi =
                            replacement->descriptor.interface_version;
                        inputs.source = backend == OverrideBackend::OpenGL ?
                            replacement->opengl_source :
                            replacement->vulkan_source;
                    }
                } else if (scope_ready) {
                    source_ready = CopyPreviewFragmentSource(
                        selection, m_detail_snapshot, &inputs.source,
                        &m_preview_packet_message);
                }
                attempted.source_digest = ComputePreviewDigest(
                    reinterpret_cast<const uint8_t *>(inputs.source.data()),
                    inputs.source.size());
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
            if (!m_preview_packet_message.empty()) {
                service.ReportInputFailure(attempted,
                    "INPUT FAILED (source snapshot " +
                    std::to_string(detail_generation) + ", replacement store " +
                    std::to_string(replacement_generation) + "): " +
                    m_preview_packet_message);
            }
        }
    } else {
        if (GetCaptureStore().Armed()) GetCaptureStore().Cancel();
        service.ClearSelection();
        m_preview_packet_attempted = false;
    }

    if (service.RequestAutomaticPreparation(now_ns)) {
        std::string error;
        if (!GetPreviewGlExecutor().StartWhilePaused(&error)) {
            m_action_message = error;
        }
    }
    service.CopyStatus(&status);
    ImGui::BeginDisabled(!status.has_packet || !status.guest_paused ||
                         status.work_active || status.prepared);
    if (ImGui::Button("Prepare while paused")) {
        std::string error;
        if (!service.RequestPreparation(&error)) {
            m_action_message = error;
        } else if (!GetPreviewGlExecutor().StartWhilePaused(&error)) {
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

    if (status.has_attempt) {
        ImGui::TextWrapped("Attempted %s: %s",
            PreviewModeLabel(status.attempted_compile.selection.mode),
            PreviewSourceIdentity(status.attempted_compile).c_str());
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

    if (ImGui::CollapsingHeader("View and comparison")) {
        ImGui::SliderFloat("Zoom", &m_preview_view_zoom, 1.0f, 8.0f,
                           "%.1fx");
        ImGui::SliderFloat2("Pan center", m_preview_view_center.data(),
                            0.0f, 1.0f);
        PreviewGlExecutor &executor = GetPreviewGlExecutor();
        ImGui::BeginDisabled(!executor.HasDisplayed());
        if (ImGui::Button("Freeze Reference")) {
            executor.FreezeDisplayed();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!executor.HasFrozen());
        if (ImGui::Button("Clear Reference")) executor.ClearFrozen();
        ImGui::EndDisabled();
        if (executor.HasFrozen()) {
            ImGui::TextDisabled("Change mode or inputs to compare Current with Reference.");
        }
    }

    ImVec2 available = ImGui::GetContentRegionAvail();
    float side = std::max(1.0f, available.x - 40.0f);
    float output_height = std::max(340.0f, available.y);
    if (ImGui::BeginChild("##preview-output",
                          ImVec2(0.0f, output_height), true)) {
        ImGui::TextDisabled("Synthetic inputs — not an in-game draw");
        ImGui::Separator();
        if (entry) {
            PreviewViewSettings view{};
            view.zoom = m_preview_view_zoom;
            view.center = m_preview_view_center;
            GetPreviewGlExecutor().DrawImage(side - 16.0f,
                                             enabled ? &selection : nullptr,
                                             now_ns, &view);
            m_preview_view_center = view.center;
        } else {
            ImGui::TextWrapped(
                "Select a pixel shader to prepare a private preview.");
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

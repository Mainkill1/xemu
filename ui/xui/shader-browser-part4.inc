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
    if (settings_columns) ImGui::Columns(1);
}

namespace {

struct ShaderWorkbenchSession {
    bool controls_open = true;
    bool compact_inspector_pending = false;
    float editor_height = 210.0f;
    float compact_editor_height = 170.0f;
    bool start_requested = false;
    PreviewSelection start_selection;
    std::string start_error;
    bool generated_cache_valid = false;
    bool has_generated = false;
    uint64_t generated_detail_generation = 0;
    PreviewSelection generated_selection;
    ShaderScope generated_active_scope;
    GeneratedSourceSnapshot generated;
    std::string generated_error;
    WorkbenchDraft draft;
    GlslEditorModel editor;
    WorkbenchEditorDockState dock;
    PreviewSourceVariant last_variant = PreviewSourceVariant::Original;
    std::optional<FrozenDraftCompile> submitted_draft;
    std::optional<PreviewPacket> last_packet;
    std::shared_ptr<const WorkbenchExperiment> reopened_test;
    std::shared_ptr<const WorkbenchExperiment> running_test;
    bool editing_test = false;
    bool comparison_locked = false;
    PreviewSelection comparison_selection;
    double comparison_time = 0.0;
    std::vector<std::filesystem::path> available_tests;
    int selected_test = -1;
    bool tests_scanned = false;
    std::optional<WorkbenchAppliedRule> applied_rule;
    uint64_t next_draft_id = 1;
};

ShaderWorkbenchSession &WorkbenchSession()
{
    static ShaderWorkbenchSession session;
    return session;
}

ShaderScope CurrentWorkbenchBuildScope()
{
    XemuShaderBrowserScope live{};
    xemu_shader_browser_copy_current_scope(&live);
    ShaderScope scope{};
    scope.title_id = live.title_id;
    scope.executable_fingerprint_version =
        live.executable_fingerprint_version;
    std::copy_n(live.executable_fingerprint,
                scope.executable_fingerprint.size(),
                scope.executable_fingerprint.begin());
    return scope;
}

bool WorkbenchDraftMatches(const WorkbenchDraft &draft,
                           const PreviewSelection &selection)
{
    if (!draft.Id() || selection.mode == PreviewMode::Replacement)
        return false;
    const GeneratedSourceSnapshot &base = draft.Base();
    return base.key == selection.shader && base.scope == selection.scope &&
           base.backend == selection.backend &&
           base.route == (selection.mode == PreviewMode::Uber ?
                          Route::Uber : Route::Specialized);
}

DraftGameApplyRequest WorkbenchApplyRequest(
    const WorkbenchDraft &draft, const PreviewSelection &selection,
    const PreviewStatus &preview,
    const std::optional<FrozenDraftCompile> &latest_submission)
{
    DraftGameApplyRequest request{};
    if (!draft.Id()) return request;
    const GeneratedSourceSnapshot &base = draft.Base();
    request.key = base.key;
    request.scope = base.scope;
    request.backend = base.backend == PreviewBackend::OpenGL ?
        OverrideBackend::OpenGL : base.backend == PreviewBackend::Vulkan ?
            OverrideBackend::Vulkan : OverrideBackend::Unknown;
    request.source_stage = base.stage;
    request.interface_abi = base.interface_abi;
    request.draft_id = draft.Id();
    request.draft_revision = draft.Revision();
    request.attempted_revision = draft.AttemptedRevision();
    request.successful_revision = draft.LastSuccessfulRevision();
    if (draft.LastSuccessfulCompile()) {
        request.successful_submission_id =
            draft.LastSuccessfulCompile()->submission_id;
        request.successful_source_digest =
            draft.LastSuccessfulCompile()->digest;
        request.source = draft.LastSuccessfulCompile()->source;
        request.compiled_success =
            request.attempted_revision == request.draft_revision &&
            request.successful_revision == request.draft_revision &&
            latest_submission &&
            latest_submission->submission_id ==
                request.successful_submission_id;
        const PreviewCompileKey &displayed = preview.displayed_compile;
        if (preview.has_displayed_source &&
            displayed.selection == selection &&
            displayed.source_variant == PreviewSourceVariant::Edited &&
            displayed.draft_id == request.draft_id &&
            displayed.draft_revision == request.draft_revision &&
            displayed.source_digest == request.successful_source_digest) {
            request.preview_submission_id =
                displayed.draft_submission_id;
        }
    }
    return request;
}

const char *WorkbenchGameEffectLabel(uint32_t state)
{
    switch (state) {
    case XEMU_SHADER_OVERRIDE_EFFECT_PREPARING: return "Preparing";
    case XEMU_SHADER_OVERRIDE_EFFECT_EFFECTIVE: return "Effective";
    case XEMU_SHADER_OVERRIDE_EFFECT_FALLBACK: return "Fallback";
    case XEMU_SHADER_OVERRIDE_EFFECT_FAILED: return "Failed";
    case XEMU_SHADER_OVERRIDE_EFFECT_CONDITION_NOT_MATCHED:
        return "Draw restriction did not match";
    default: return "Waiting for a draw";
    }
}

bool WorkbenchRuleMatchesToken(const OverrideRule &rule,
                               const WorkbenchAppliedRule &token)
{
    return rule.id == token.id &&
           rule.origin == OverrideOrigin::Session &&
           rule.action == OverrideAction::Replacement &&
           rule.restrict_build && rule.shader == token.key &&
           rule.title_id == token.scope.title_id &&
           rule.executable_fingerprint_version ==
               token.scope.executable_fingerprint_version &&
           rule.executable_fingerprint ==
               token.scope.executable_fingerprint &&
           rule.replacement_id == token.replacement_id;
}

std::string WorkbenchBuildFingerprint(const ShaderScope &scope)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string value;
    value.reserve(scope.executable_fingerprint.size() * 2);
    for (uint8_t byte : scope.executable_fingerprint) {
        value.push_back(digits[byte >> 4]);
        value.push_back(digits[byte & 15]);
    }
    return value;
}

bool BuildCurrentWorkbenchExperiment(
    const ShaderWorkbenchSession &session, const Entry &entry,
    const CanonicalRecipe &recipe, const PreviewSelection &selection,
    const PreviewStatus &status, const PreviewScene &scene,
    WorkbenchExperiment *experiment,
    std::string *error)
{
    if (!experiment || (!session.running_test &&
        (!session.has_generated ||
         session.generated_selection != selection))) {
        if (error) *error = "Exact generated source is unavailable for export";
        return false;
    }
    const bool edited = session.dock.variant == PreviewSourceVariant::Edited &&
        (session.running_test && !session.editing_test ?
            session.running_test->successful_compile.has_value() :
            WorkbenchDraftMatches(session.draft, selection));
    const PreviewPacket *candidate =
        status.prepared && status.has_attempt && session.last_packet &&
        session.last_packet->selection == selection &&
        BuildPreviewCompileKey(*session.last_packet) ==
            status.attempted_compile &&
        session.last_packet->source_variant ==
            (edited ? PreviewSourceVariant::Edited :
                      PreviewSourceVariant::Original) &&
        ((session.running_test && !session.editing_test) || !edited ||
         (session.draft.LastSuccessfulCompile() &&
          session.last_packet->draft_submission_id ==
              session.draft.LastSuccessfulCompile()->submission_id)) ?
            &*session.last_packet : nullptr;
    if (!candidate) {
        if (error) *error = "No matching synthetic result is ready to export";
        return false;
    }
    WorkbenchExperiment next = session.running_test ?
        *session.running_test : WorkbenchExperiment{};
    next.recipe = session.running_test ? session.running_test->recipe : recipe;
    if (!AttachPreviewSelectionScope(entry, selection, &next.recipe, error))
        return false;
    next.packet = *candidate;
    next.packet.scene = scene;
    if (!session.running_test) next.original = session.generated;
    if ((!session.running_test || session.editing_test) &&
        WorkbenchDraftMatches(session.draft, selection)) {
        next.edited_text = session.draft.Text();
        next.edit_revision = session.draft.Revision();
        next.successful_compile = session.draft.LastSuccessfulCompile();
    }
    next.clock = status.clock;
    PreviewSyntheticFixture fixture{};
    if (!DecodePreviewSyntheticFixture(next.packet.fixture_bytes,
                                       &fixture, error)) return false;
    next.dependencies.clear();
    for (size_t i = 0; i < next.owned_textures.size(); ++i) {
        const PreviewTexturePixels pixels = GeneratePreviewTexture(fixture, i);
        next.owned_textures[i].assign(pixels.begin(), pixels.end());
        next.dependencies.push_back({"T" + std::to_string(i),
            WorkbenchDependencyStatus::Synthetic,
            "Generated RGBA8 synthetic fixture"});
    }
    if (!ValidateWorkbenchExperiment(next, error)) return false;
    *experiment = std::move(next);
    return true;
}

void RefreshWorkbenchTestFiles(ShaderWorkbenchSession *session,
                               const std::filesystem::path &config_directory)
{
    if (!session) return;
    session->tests_scanned = true;
    session->available_tests.clear();
    session->selected_test = -1;
    if (config_directory.empty()) return;
    using TimedPath = std::pair<std::filesystem::file_time_type,
                                std::filesystem::path>;
    std::vector<TimedPath> newest;
    newest.reserve(513);
    auto later = [](const TimedPath &lhs, const TimedPath &rhs) {
        return lhs > rhs;
    };
    std::error_code ec;
    const auto root = config_directory / "shader-exports";
    std::filesystem::directory_iterator file(root, ec), end;
    for (; !ec && file != end; file.increment(ec)) {
        std::error_code file_error;
        if (!file->is_regular_file(file_error) || file_error) continue;
        const std::string name = file->path().filename().u8string();
        constexpr char suffix[] = ".xemu-test.json";
        if (name.size() > sizeof(suffix) - 1 &&
            name.compare(name.size() - (sizeof(suffix) - 1),
                         sizeof(suffix) - 1, suffix) == 0) {
            const auto modified = file->last_write_time(file_error);
            if (file_error) continue;
            newest.emplace_back(modified, file->path());
            std::push_heap(newest.begin(), newest.end(), later);
            if (newest.size() > 512) {
                std::pop_heap(newest.begin(), newest.end(), later);
                newest.pop_back();
            }
        }
    }
    std::sort(newest.begin(), newest.end(), later);
    for (auto &candidate : newest)
        session->available_tests.push_back(std::move(candidate.second));
    if (!session->available_tests.empty()) session->selected_test = 0;
}

} // namespace

void ShaderBrowserWindow::DrawWorkbenchSharedSettings()
{
    ShaderWorkbenchSession &workbench = WorkbenchSession();
    ImGui::SeparatorText("Private preview and editor");
    if (workbench.comparison_locked) {
        ImGui::TextDisabled("Original / Edited comparison inputs are locked.");
        if (ImGui::SmallButton("Unlock comparison"))
            workbench.comparison_locked = false;
    }
    if (workbench.comparison_locked) ImGui::BeginDisabled();
    ImGui::Checkbox("Auto-compile edited GLSL", &workbench.dock.auto_compile);
    DrawMutedWrapped("Compiles only the private draft; a game rule requires an explicit Apply action.");
    int policy = static_cast<int>(m_preview_update_policy);
    if (ImGui::Combo("Preview updates", &policy,
                     "When inputs change\0Continuous synthetic clock\0")) {
        m_preview_update_policy = static_cast<PreviewUpdatePolicy>(policy);
        m_preview_packet_attempted = false;
    }
    if (workbench.comparison_locked) ImGui::EndDisabled();
    PreviewStatus status{};
    GetPreviewService().CopyStatus(&status);
    ImGui::TextDisabled("Current preparation ceiling: %s",
        status.update_hz ?
            (std::to_string(status.update_hz) + " Hz").c_str() : "Frozen");
    DrawMutedWrapped(
        "The service adjusts this ceiling for guest pressure; it is not a user-set frame rate.");
    DrawMutedWrapped(
        "Camera: left drag to orbit, middle or Shift+left drag to pan, wheel to dolly, F to focus.");
}

void ShaderBrowserWindow::DrawLivePreview()
{
    ShaderWorkbenchSession &workbench = WorkbenchSession();
    PreviewService &service = GetPreviewService();
    const uint64_t now_ns = PreviewNowNs();
    const bool guest_paused = runstate_check(RUN_STATE_PAUSED);
    const bool offline_no_guest = workbench.running_test &&
        CurrentWorkbenchBuildScope().title_id == 0 &&
        !runstate_is_running();
    service.SetOfflineNoGuest(offline_no_guest);
    service.SetGuestPaused(guest_paused);
    uint64_t completed_flips = 0;
    uint64_t flip_interval_ns = 0;
    nv2a_profile_preview_flip_snapshot(&completed_flips, &flip_interval_ns);
    service.UpdateHealth(m_preview_health_monitor.Sample(
        now_ns, completed_flips, flip_interval_ns, runstate_is_running()));

    PreviewStatus status{};
    service.CopyStatus(&status);
    const PreviewBackend active_backend =
        g_config.display.renderer == CONFIG_DISPLAY_RENDERER_VULKAN ?
            PreviewBackend::Vulkan : PreviewBackend::OpenGL;
    if (workbench.running_test &&
        workbench.running_test->packet.selection.backend != active_backend) {
        workbench.running_test.reset();
        workbench.editing_test = false;
        workbench.comparison_locked = false;
        workbench.generated_cache_valid = false;
        m_preview_packet_attempted = false;
        m_action_message =
            "Saved test stopped because the renderer backend changed";
    }
    auto lock_comparison = [&] (const PreviewSelection &selection,
                                PreviewSourceVariant source_variant) {
        const PreviewPacket *packet = workbench.last_packet ?
            &*workbench.last_packet : nullptr;
        const PreviewResultKey &displayed = status.displayed_result;
        const PreviewChannel channel =
            selection.mode == PreviewMode::Visualize ?
                static_cast<PreviewChannel>(m_preview_view_channel) :
                PreviewChannel::FinalRGBA;
        if (!packet || !m_preview_packet_attempted ||
            !status.has_displayed_source || !status.has_attempt ||
            !status.prepared || packet->selection != selection ||
            packet->source_variant != source_variant ||
            displayed.compile != BuildPreviewCompileKey(*packet) ||
            status.attempted_compile != displayed.compile ||
            displayed.input_revision != m_preview_input_revision ||
            !SameCountedWorkbenchBindings(
                displayed, *packet, workbench.dock.bindings) ||
            !(displayed.scene == m_preview_scene) ||
            !(displayed.render_state == m_preview_render_state) ||
            displayed.fixture_digest != packet->fixture_digest ||
            displayed.channel != channel) {
            m_action_message =
                "Wait for the current preview before comparing sources";
            return false;
        }
        service.EditClock(PreviewClockAction::Pause, 0, now_ns);
        service.EditClock(PreviewClockAction::Scrub,
                          displayed.time_seconds, now_ns);
        workbench.comparison_locked = true;
        workbench.comparison_selection = selection;
        workbench.comparison_time = displayed.time_seconds;
        return true;
    };

    const ImVec2 pane_pos = ImGui::GetCursorPos();
    const ImVec2 pane_size = ImGui::GetContentRegionAvail();
    const float gap = ImGui::GetStyle().ItemSpacing.y;
    const bool desktop_inspector =
        ImGui::GetMainViewport()->WorkSize.x >=
        1280.0f * g_viewport_mgr.m_scale &&
        pane_size.x >= 830.0f * g_viewport_mgr.m_scale;
    if (!desktop_inspector && workbench.compact_inspector_pending) {
        ImGui::OpenPopup("##workbench-inspector-popup");
        workbench.compact_inspector_pending = false;
    }
    bool controls_visible = desktop_inspector ?
        workbench.controls_open :
        ImGui::IsPopupOpen("##workbench-inspector-popup");
    const bool editor_visible = workbench.dock.open;
    const float header_height = (desktop_inspector ? 98.0f : 90.0f) *
        g_viewport_mgr.m_scale;
    float &preferred_editor_height = desktop_inspector ?
        workbench.editor_height : workbench.compact_editor_height;
    const float editor_height = editor_visible ?
        std::clamp(preferred_editor_height * g_viewport_mgr.m_scale,
                   100.0f * g_viewport_mgr.m_scale,
                   std::max(100.0f * g_viewport_mgr.m_scale,
                            pane_size.y * 0.38f)) : 0.0f;
    const float top_height = std::max(1.0f, pane_size.y - editor_height -
        gap * editor_visible);
    const float inspector_width = 264.0f * g_viewport_mgr.m_scale;
    const float scene_width = desktop_inspector && controls_visible ?
        std::max(1.0f, pane_size.x - inspector_width - gap) : pane_size.x;
    const float output_height = std::max(1.0f,
        top_height - header_height - gap);
    const float image_max_width = std::max(1.0f,
        scene_width - 28.0f * g_viewport_mgr.m_scale);
    const float image_max_height = std::max(1.0f,
        output_height - 104.0f * g_viewport_mgr.m_scale);
    const uint32_t preview_packet_height = static_cast<uint32_t>(std::clamp(
        std::lround(kPreviewMaxWidth * image_max_height /
                    image_max_width / 8.0f) * 8L,
        128L, static_cast<long>(kPreviewMaxHeight)));
    // Draw the inspector first so edited inputs are included in this frame's
    // private packet. On compact windows it floats over the scene.
    if (controls_visible) {
        if (desktop_inspector) {
            ImGui::SetCursorPos(ImVec2(
                pane_pos.x + pane_size.x - inspector_width, pane_pos.y));
            ImGui::BeginChild("##workbench-controls",
                              ImVec2(inspector_width, top_height), true);
        } else {
            const ImVec2 window_pos = ImGui::GetWindowPos();
            ImGui::SetNextWindowPos(ImVec2(
                window_pos.x + pane_pos.x + pane_size.x - inspector_width,
                window_pos.y + pane_pos.y));
            ImGui::SetNextWindowSize(ImVec2(inspector_width, top_height));
            controls_visible = ImGui::BeginPopup(
                "##workbench-inspector-popup",
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        }
    }
    if (controls_visible) {
        ImGui::PushItemWidth(145.0f * g_viewport_mgr.m_scale);
        ImGui::TextUnformatted("Test configuration");
        ImGui::SameLine();
        if (ImGui::SmallButton("x##close-inspector")) {
            if (desktop_inspector) workbench.controls_open = false;
            else {
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::Separator();
    }
    bool enabled = status.enabled;
    int mode = static_cast<int>(status.requested_mode);
    const bool visualize = mode == static_cast<int>(PreviewMode::Visualize);
    if (controls_visible && visualize) {
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

    const bool generated_alpha =
        mode != static_cast<int>(PreviewMode::Replacement) &&
        workbench.dock.variant == PreviewSourceVariant::Original;
    const Entry *current_entry = SelectedEntry();
    const CanonicalRecipe *alpha_recipe = workbench.running_test ?
        &workbench.running_test->recipe :
        current_entry && m_has_recipe &&
        m_selected_recipe.key == current_entry->key ?
            &m_selected_recipe : nullptr;
    bool canonical_alpha = false;
    if (generated_alpha && alpha_recipe &&
        PreviewCanonicalAlphaTest(*alpha_recipe, &canonical_alpha) &&
        m_preview_render_state.alpha_test != canonical_alpha) {
        m_preview_render_state.alpha_test = canonical_alpha;
        m_preview_packet_attempted = false;
    }
    if (controls_visible) {
        if (workbench.comparison_locked) ImGui::BeginDisabled();
        const WorkbenchSceneUiEdits scene_edits = DrawWorkbenchSceneControls(
            &m_preview_scene, &m_preview_render_state, generated_alpha);
        if (workbench.comparison_locked) ImGui::EndDisabled();
        if (scene_edits.scene) service.EditScene(m_preview_scene);
        if (scene_edits.render_state) {
            m_preview_alpha_reference = m_preview_render_state.alpha_reference;
            m_preview_packet_attempted = false;
        }

        if (workbench.comparison_locked) ImGui::BeginDisabled();
        int policy = static_cast<int>(m_preview_update_policy);
        if (ImGui::Combo("Update policy", &policy, "On dirty\0Continuous\0")) {
            m_preview_update_policy = static_cast<PreviewUpdatePolicy>(policy);
            m_preview_packet_attempted = false;
        }
        if (m_preview_update_policy == PreviewUpdatePolicy::Continuous) {
            ImGui::TextWrapped("Synthetic animation cycles vertex colors and UVs. "
                               "It does not supply the game's time uniforms.");
            if (ImGui::Button(status.clock.playing ?
                              "Pause preview" : "Play preview")) {
                service.EditClock(status.clock.playing ?
                    PreviewClockAction::Pause : PreviewClockAction::Play,
                    0, now_ns);
            }
            ImGui::SameLine();
            if (ImGui::Button("Restart"))
                service.EditClock(PreviewClockAction::Restart, 0, now_ns);
            float time = static_cast<float>(status.clock.time_seconds);
            if (ImGui::DragFloat("Time (seconds)", &time, 0.02f, 0.0f,
                                 status.clock.loop ?
                                     status.clock.loop_seconds : 1.0e9f))
                service.EditClock(PreviewClockAction::Scrub, time, now_ns);
            float speed = static_cast<float>(status.clock.speed);
            if (ImGui::SliderFloat("Speed", &speed, 0.05f, 8.0f, "%.2fx"))
                service.EditClock(PreviewClockAction::Speed, speed, now_ns);
            bool loop = status.clock.loop;
            if (ImGui::Checkbox("Loop", &loop))
                service.EditClock(PreviewClockAction::Loop, loop, now_ns);
            float length = static_cast<float>(status.clock.loop_seconds);
            if (ImGui::DragFloat("Loop length (seconds)", &length,
                                 0.1f, 0.1f, 3600.0f))
                service.EditClock(PreviewClockAction::LoopLength,
                                  length, now_ns);
            ImGui::Text("Clock sample: %llu",
                        static_cast<unsigned long long>(status.clock.frame));
        }
        if (workbench.comparison_locked) ImGui::EndDisabled();
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
        m_preview_render_state.alpha_reference =
            m_preview_fixture.alpha_reference;
        m_preview_uv_scale = m_preview_fixture.uv_scale;
        m_preview_uv_offset = m_preview_fixture.uv_offset;
        m_preview_constant_color = m_preview_fixture.constant_color;
    };
    if (controls_visible && workbench.comparison_locked)
        ImGui::BeginDisabled();
    if (controls_visible &&
        ImGui::CollapsingHeader("Fixture profile and inputs")) {
        if (DrawWorkbenchBindingControls(&workbench.dock))
            fixture_changed();
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
    if (controls_visible &&
        ImGui::CollapsingHeader("Synthetic fixture colors")) {
        static const char *labels[] = {
            "Top left", "Top right", "Bottom left", "Bottom right"
        };
        for (size_t i = 0; i < m_preview_colors.size(); ++i) {
            if (ImGui::ColorEdit4(labels[i], m_preview_colors[i].data())) {
                fixture_changed();
            }
        }
    }
    if (controls_visible &&
        ImGui::CollapsingHeader("Synthetic texture and UV")) {
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
    if (controls_visible &&
        ImGui::CollapsingHeader("Synthetic constants and alpha")) {
        if (ImGui::ColorEdit4("Combiner constant",
                              m_preview_constant_color.data())) {
            fixture_changed();
        }
        if (ImGui::ColorEdit4("Fog color", m_preview_fog_color.data())) {
            fixture_changed();
        }
        ImGui::TextDisabled("Alpha reference is set in Synthetic render state");
    }
    if (controls_visible && workbench.comparison_locked)
        ImGui::EndDisabled();

    Entry imported_entry{};
    const Entry *entry = SelectedEntry();
    if (workbench.running_test) {
        const WorkbenchExperiment &running = *workbench.running_test;
        imported_entry.key = running.recipe.key;
        imported_entry.scopes = running.recipe.scopes;
        imported_entry.recipe_format_version =
            running.recipe.recipe_format_version;
        imported_entry.recipe_size = running.recipe.bytes.size();
        entry = &imported_entry;
    }
    PreviewSelection selection{};
    if (entry) {
        selection.session_epoch = m_snapshot.session_epoch;
        selection.renderer_epoch = nv2a_profile_preview_renderer_epoch();
        ShaderScope active_scope = CurrentWorkbenchBuildScope();
        if (workbench.running_test) {
            const PreviewSelection &saved =
                workbench.running_test->packet.selection;
            selection.shader = saved.shader;
            selection.scope = saved.scope;
            selection.backend = saved.backend;
            selection.mode = saved.mode;
        } else {
            selection.shader = entry->key;
            selection.backend =
                g_config.display.renderer == CONFIG_DISPLAY_RENDERER_VULKAN ?
                    PreviewBackend::Vulkan : PreviewBackend::OpenGL;
            selection.mode = static_cast<PreviewMode>(mode);
            auto exact_scope = std::find(entry->scopes.begin(),
                                         entry->scopes.end(), active_scope);
            uint32_t preferred_title = m_snapshot.current_title_id ?
                m_snapshot.current_title_id : PrimaryTitleId(*entry);
            auto scope = std::find_if(
                entry->scopes.begin(), entry->scopes.end(),
                [preferred_title](const ShaderScope &candidate) {
                    return candidate.title_id == preferred_title;
                });
            if (exact_scope != entry->scopes.end()) {
                selection.scope = *exact_scope;
            } else if (scope != entry->scopes.end()) {
                selection.scope = *scope;
            } else if (!entry->scopes.empty()) {
                selection.scope = entry->scopes.front();
            } else {
                selection.scope.title_id = preferred_title;
            }
        }
        service.SetSelection(selection, now_ns);
        if (workbench.comparison_locked &&
            workbench.comparison_selection != selection)
            workbench.comparison_locked = false;
        if ((!workbench.running_test || workbench.editing_test) &&
            workbench.dock.variant == PreviewSourceVariant::Edited &&
            !WorkbenchDraftMatches(workbench.draft, selection)) {
            workbench.dock.variant = PreviewSourceVariant::Original;
            m_preview_packet_attempted = false;
        }

        if ((!workbench.running_test || workbench.editing_test) &&
            workbench.submitted_draft &&
            status.has_attempt &&
            status.attempted_compile.selection == selection &&
            status.attempted_compile.source_variant ==
                PreviewSourceVariant::Edited &&
            status.attempted_compile.draft_id ==
                workbench.submitted_draft->draft_id &&
            status.attempted_compile.draft_revision ==
                workbench.submitted_draft->revision &&
            status.attempted_compile.draft_submission_id ==
                workbench.submitted_draft->submission_id &&
            status.attempted_compile.source_digest ==
                workbench.submitted_draft->digest) {
            if (status.prepared) {
                workbench.draft.MarkCompiled(
                    *workbench.submitted_draft, true, {});
            } else if (!status.work_active &&
                       (status.state == PreviewState::Failed ||
                        status.state == PreviewState::Unsupported)) {
                workbench.draft.MarkCompiled(*workbench.submitted_draft,
                                             false, status.message);
            }
        }

        if (workbench.running_test) {
            if (workbench.generated_selection != selection)
                workbench.generated = workbench.running_test->original;
            workbench.has_generated = true;
            workbench.generated_selection = selection;
            workbench.generated_cache_valid = false;
        } else if (!workbench.generated_cache_valid ||
            workbench.generated_selection != selection ||
            !(workbench.generated_active_scope == active_scope) ||
            workbench.generated_detail_generation !=
                m_detail_snapshot.generation) {
            workbench.generated_cache_valid = true;
            workbench.generated_selection = selection;
            workbench.generated_active_scope = active_scope;
            workbench.generated_detail_generation =
                m_detail_snapshot.generation;
            workbench.has_generated = CopyGeneratedSourceSnapshot(
                selection, active_scope, *entry, m_detail_snapshot, 1, 1,
                &workbench.generated, &workbench.generated_error);
        }
        const bool draft_matches = WorkbenchDraftMatches(
            workbench.draft, selection);
        if (controls_visible &&
            ((draft_matches &&
             (!workbench.running_test || workbench.editing_test)) ||
            (workbench.running_test && !workbench.editing_test &&
             workbench.running_test->successful_compile))) {
            if (workbench.comparison_locked) {
                ImGui::TextDisabled(
                    "Original/Edited inputs locked at %.3f s",
                    workbench.comparison_time);
                if (ImGui::Button("Unlock comparison inputs"))
                    workbench.comparison_locked = false;
            } else if (ImGui::Button("Lock Original/Edited inputs")) {
                lock_comparison(selection, workbench.dock.variant);
            }
        }
        if (controls_visible && workbench.applied_rule &&
            !draft_matches) {
            ImGui::TextDisabled("Workbench game rule remains active for %s",
                PortableShaderHash(workbench.applied_rule->key.hash).c_str());
            if (ImGui::Button("Restore prior workbench rule")) {
                std::string error;
                if (RestoreWorkbenchDraft(*workbench.applied_rule,
                                          &GetOverrideStore(), &error)) {
                    workbench.applied_rule.reset();
                    m_action_message = "Restored prior workbench game rule";
                } else {
                    m_action_message = error;
                }
            }
        }
        if (controls_visible &&
            WorkbenchDraftMatches(workbench.draft, selection)) {
            ImGui::SeparatorText("Explicit game action");
            const DraftGameApplyRequest request = WorkbenchApplyRequest(
                workbench.draft, selection, status,
                workbench.submitted_draft);
            OverrideStoreSnapshot store_snapshot{};
            GetOverrideStore().CopySnapshot(&store_snapshot);
            bool changed_applied_rule = false;
            if (workbench.applied_rule) {
                const auto rule = std::find_if(
                    store_snapshot.rules.begin(),
                    store_snapshot.rules.end(),
                    [&workbench](const OverrideRule &candidate) {
                        return candidate.id == workbench.applied_rule->id;
                    });
                if (rule == store_snapshot.rules.end()) {
                    workbench.applied_rule.reset();
                    m_action_message =
                        "Workbench rule was removed in Overrides";
                } else {
                    changed_applied_rule = !WorkbenchRuleMatchesToken(
                        *rule, *workbench.applied_rule);
                }
            }
            std::string apply_reason;
            const bool eligible = !workbench.running_test &&
                EvaluateDraftGameApply(
                    request, *entry, store_snapshot.context, &apply_reason);
            if (workbench.running_test)
                apply_reason = "Return to live source before applying a draft";
            const bool can_apply = eligible && !workbench.applied_rule;
            if (eligible && workbench.applied_rule) {
                apply_reason = "Restore the active workbench rule before "
                               "applying another draft";
            }
            ImGui::BeginDisabled(!can_apply);
            if (ImGui::Button("Apply draft to game")) {
                const char *base_path = xemu_settings_get_base_path();
                WorkbenchAppliedRule applied{};
                std::string error;
                if (base_path && ApplyWorkbenchDraft(
                        request, *entry, &GetOverrideStore(),
                        &GetReplacementLibrary(),
                        std::filesystem::u8path(base_path),
                        &applied, &error)) {
                    workbench.applied_rule = applied;
                    m_action_message = "Requested exact-build game replacement";
                } else {
                    m_action_message = error.empty() ?
                        "Replacement path unavailable" : error;
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!workbench.applied_rule ||
                                 changed_applied_rule);
            if (ImGui::Button("Restore game rule")) {
                std::string error;
                if (RestoreWorkbenchDraft(*workbench.applied_rule,
                                          &GetOverrideStore(), &error)) {
                    workbench.applied_rule.reset();
                    m_action_message = "Restored this workbench game rule";
                } else {
                    m_action_message = error;
                }
            }
            ImGui::EndDisabled();
            if (!can_apply) ImGui::TextWrapped("%s", apply_reason.c_str());
            if (changed_applied_rule)
                ImGui::TextWrapped("Rule changed in Overrides; remove it "
                                   "there before applying another draft");
            if (workbench.applied_rule) {
                GetOverrideStore().CopySnapshot(&store_snapshot);
                ImGui::TextWrapped("Rule scope: TitleID %08X, build v%u %s",
                    workbench.applied_rule->scope.title_id,
                    workbench.applied_rule->scope.
                        executable_fingerprint_version,
                    WorkbenchBuildFingerprint(
                        workbench.applied_rule->scope).c_str());
                ImGui::TextWrapped("Shader: %s / %s; all matching draws",
                    PortableShaderHash(workbench.applied_rule->key.hash).c_str(),
                    StageLabel(workbench.applied_rule->key.stage));
                const OverrideResolution resolution =
                    GetOverrideStore().Resolve(entry->key);
                if (resolution.status == OverrideResolutionStatus::Matched &&
                    resolution.policy.rule_id == workbench.applied_rule->id) {
                    ImGui::TextDisabled("Requested: %s, draft r%llu",
                        OverrideActionLabel(resolution.policy.action),
                        static_cast<unsigned long long>(
                            resolution.policy.rule_revision));
                    XemuShaderOverrideEffect effect{};
                    const OverrideContext &context = store_snapshot.context;
                    const bool observed = xemu_shader_override_copy_effect(
                        context.title_id,
                        context.executable_fingerprint_version,
                        context.executable_fingerprint.data(),
                        static_cast<uint32_t>(context.backend),
                        entry->key.hash.version, entry->key.hash.bytes.data(),
                        static_cast<uint32_t>(entry->key.stage),
                        store_snapshot.generation,
                        resolution.policy.rule_id,
                        resolution.policy.rule_revision, &effect);
                    ImGui::TextDisabled("Renderer: %s",
                        observed ? WorkbenchGameEffectLabel(effect.state) :
                                   "Waiting for a draw");
                    if (observed) {
                        ImGui::TextDisabled("Effective: %s, package r%llu",
                            OverrideActionLabel(static_cast<OverrideAction>(
                                effect.effective_action)),
                            static_cast<unsigned long long>(
                                effect.effective_replacement_revision));
                    }
                } else {
                    ImGui::TextDisabled("Applied rule is not currently resolved");
                }
            }
        }

        PreviewCompileKey attempted{};
        attempted.selection = selection;
        bool draft_waiting = false;
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
             m_preview_packet_scope_available != scope_available ||
             m_preview_packet_height != preview_packet_height)) {
            const bool new_selection = m_preview_packet_selection != selection;
            if (new_selection && !workbench.running_test)
                m_preview_fixture_suggested = false;
            if (!workbench.running_test && m_preview_profile < 0 &&
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
            m_preview_packet_height = preview_packet_height;
            m_preview_packet_message.clear();
            if (!workbench.running_test &&
                (!m_has_recipe || m_selected_recipe.key != entry->key)) {
                m_preview_packet_message = "Canonical recipe is unavailable";
            } else {
                PreviewPacketInputs inputs{};
                inputs.selection = selection;
                inputs.recipe = workbench.running_test ?
                    workbench.running_test->recipe : m_selected_recipe;
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
                inputs.binding_count = static_cast<uint8_t>(
                    workbench.dock.bindings.size());
                inputs.bindings = workbench.dock.bindings;
                inputs.scene = m_preview_scene;
                inputs.render_state = m_preview_render_state;
                inputs.width = kPreviewMaxWidth;
                inputs.height = preview_packet_height;
                inputs.source_route = selection.mode == PreviewMode::Uber ?
                    Route::Uber : Route::Specialized;
                if (workbench.running_test) {
                    inputs.source_route =
                        workbench.running_test->original.route;
                    inputs.generator_abi =
                        workbench.running_test->original.generator_abi;
                    inputs.interface_abi =
                        workbench.running_test->original.interface_abi;
                }
                bool source_ready = scope_ready;
                if (scope_ready && workbench.running_test &&
                    !workbench.editing_test) {
                    const PreviewPacket &saved =
                        workbench.running_test->packet;
                    inputs.source_route = saved.source_route;
                    inputs.generator_abi = saved.generator_abi;
                    inputs.interface_abi = saved.interface_abi;
                    inputs.replacement_id = saved.replacement_id;
                    inputs.replacement_revision =
                        saved.replacement_revision;
                    if (workbench.dock.variant ==
                            PreviewSourceVariant::Edited &&
                        workbench.running_test->successful_compile) {
                        const FrozenDraftCompile &compiled =
                            *workbench.running_test->successful_compile;
                        inputs.source_variant = PreviewSourceVariant::Edited;
                        inputs.source = compiled.source;
                        inputs.draft_id = compiled.draft_id;
                        inputs.draft_revision = compiled.revision;
                        inputs.draft_submission_id =
                            compiled.submission_id;
                    } else {
                        inputs.source_variant =
                            PreviewSourceVariant::Original;
                        inputs.source =
                            workbench.running_test->original.text;
                        inputs.source_resident =
                            workbench.running_test->original.resident;
                    }
                    inputs.partner_source =
                        inputs.source_variant == saved.source_variant &&
                        inputs.source == saved.source ?
                            saved.partner_source :
                            BuildPreviewSyntheticVertexSource(
                                inputs.source, selection.backend);
                    attempted.source_variant = inputs.source_variant;
                    attempted.draft_id = inputs.draft_id;
                    attempted.draft_revision = inputs.draft_revision;
                    attempted.draft_submission_id =
                        inputs.draft_submission_id;
                    attempted.replacement_id = inputs.replacement_id;
                    attempted.replacement_revision =
                        inputs.replacement_revision;
                } else if (scope_ready && workbench.running_test &&
                           workbench.editing_test &&
                           workbench.dock.variant ==
                               PreviewSourceVariant::Original) {
                    const GeneratedSourceSnapshot &saved =
                        workbench.running_test->original;
                    inputs.source_route = saved.route;
                    inputs.source_resident = saved.resident;
                    inputs.source = saved.text;
                    inputs.generator_abi = saved.generator_abi;
                    inputs.interface_abi = saved.interface_abi;
                } else if (scope_ready &&
                           selection.mode == PreviewMode::Replacement) {
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
                } else if (scope_ready &&
                           workbench.dock.variant ==
                               PreviewSourceVariant::Edited &&
                           WorkbenchDraftMatches(workbench.draft, selection)) {
                    if (workbench.submitted_draft &&
                        workbench.submitted_draft->draft_id ==
                            workbench.draft.Id()) {
                        inputs.source = workbench.submitted_draft->source;
                        inputs.source_variant = PreviewSourceVariant::Edited;
                        inputs.draft_id = workbench.submitted_draft->draft_id;
                        inputs.draft_revision =
                            workbench.submitted_draft->revision;
                        inputs.draft_submission_id =
                            workbench.submitted_draft->submission_id;
                        attempted.source_variant = inputs.source_variant;
                        attempted.draft_id = inputs.draft_id;
                        attempted.draft_revision = inputs.draft_revision;
                        attempted.draft_submission_id =
                            inputs.draft_submission_id;
                    } else {
                        m_preview_packet_message =
                            "Editable draft has no submitted source revision";
                        draft_waiting = true;
                        source_ready = false;
                    }
                } else if (scope_ready) {
                    source_ready = CopyPreviewFragmentSource(
                        selection, m_detail_snapshot, &inputs.source,
                        &m_preview_packet_message);
                    inputs.source_resident = workbench.has_generated &&
                        workbench.generated_selection == selection &&
                        workbench.generated.resident;
                }
                attempted.source_digest = ComputePreviewDigest(
                    reinterpret_cast<const uint8_t *>(inputs.source.data()),
                    inputs.source.size());
                if (source_ready) {
                    if (!workbench.running_test ||
                        workbench.editing_test)
                        inputs.partner_source =
                            BuildPreviewSyntheticVertexSource(
                                inputs.source, selection.backend);
                    PreviewPacket packet{};
                    if (BuildPreviewPacket(inputs, &packet,
                                           &m_preview_packet_message)) {
                        if (service.SubmitPacket(packet, now_ns,
                                                 &m_preview_packet_message)) {
                            workbench.last_packet = std::move(packet);
                        }
                    }
                }
            }
            if (!m_preview_packet_message.empty() && !draft_waiting) {
                service.ReportInputFailure(attempted,
                    "INPUT FAILED (source snapshot " +
                    std::to_string(detail_generation) + ", replacement store " +
                    std::to_string(replacement_generation) + "): " +
                    m_preview_packet_message);
            }
        }
    } else {
        service.ClearSelection();
        m_preview_packet_attempted = false;
    }

    if (workbench.start_requested) {
        PreviewStatus preparation{};
        service.CopyStatus(&preparation);
        if (!entry || selection != workbench.start_selection ||
            !preparation.enabled) {
            workbench.start_requested = false;
            workbench.start_error =
                "Selected shader changed before preparation";
        } else if (!preparation.guest_paused && !offline_no_guest) {
            workbench.start_requested = false;
            workbench.start_error =
                "Guest resumed before private preparation";
        } else if (preparation.state == PreviewState::Failed ||
                   preparation.state == PreviewState::Unsupported) {
            workbench.start_requested = false;
        } else if (preparation.prepared || preparation.work_active) {
            workbench.start_requested = false;
        } else if ((preparation.guest_paused || offline_no_guest) &&
                   preparation.has_packet) {
            std::string error;
            if (service.RequestPreparation(&error)) {
                workbench.start_requested = false;
                if (!GetPreviewGlExecutor().StartWhilePaused(&error)) {
                    m_action_message = error;
                    workbench.start_error = error;
                }
            } else {
                workbench.start_requested = false;
                m_action_message = error;
                workbench.start_error = error;
            }
        }
    }
    if (service.RequestAutomaticPreparation(now_ns)) {
        std::string error;
        if (!GetPreviewGlExecutor().StartWhilePaused(&error)) {
            m_action_message = error;
        }
    }
    service.CopyStatus(&status);
    if (status.prepared) workbench.start_error.clear();
    if (controls_visible &&
        ImGui::CollapsingHeader("Synthetic test file")) {
        const char *base_path = xemu_settings_get_base_path();
        const std::filesystem::path base = base_path ?
            std::filesystem::u8path(base_path) : std::filesystem::path();
        ImGui::TextDisabled("Synthetic inputs — not an in-game draw");
        ImGui::BeginDisabled(!entry || !workbench.has_generated);
        if (ImGui::Button("Export current test")) {
            WorkbenchExperiment experiment{};
            std::filesystem::path output;
            std::string error;
            if (entry && BuildCurrentWorkbenchExperiment(
                    workbench, *entry, m_selected_recipe, selection, status,
                    m_preview_scene,
                    &experiment, &error) &&
                ExportWorkbenchExperiment(experiment, base,
                                          &output, &error)) {
                m_action_message = "Exported synthetic test to " +
                    output.u8string();
                RefreshWorkbenchTestFiles(&workbench, base);
            } else {
                m_action_message = error;
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Refresh tests") || !workbench.tests_scanned)
            RefreshWorkbenchTestFiles(&workbench, base);
        const char *selection_label = "No test files";
        std::string selected_name;
        if (workbench.selected_test >= 0 &&
            static_cast<size_t>(workbench.selected_test) <
                workbench.available_tests.size()) {
            selected_name = workbench.available_tests[
                workbench.selected_test].filename().u8string();
            selection_label = selected_name.c_str();
        }
        if (ImGui::BeginCombo("Reopen test", selection_label)) {
            for (size_t i = 0; i < workbench.available_tests.size(); ++i) {
                const std::string name =
                    workbench.available_tests[i].filename().u8string();
                if (ImGui::Selectable(name.c_str(),
                                      workbench.selected_test ==
                                          static_cast<int>(i))) {
                    workbench.selected_test = static_cast<int>(i);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::BeginDisabled(workbench.selected_test < 0);
        if (ImGui::Button("Reopen selected test")) {
            WorkbenchExperiment opened{};
            std::string error;
            if (ReadWorkbenchExperiment(workbench.available_tests[
                    workbench.selected_test], &opened, &error)) {
                workbench.running_test.reset();
                workbench.editing_test = false;
                workbench.comparison_locked = false;
                workbench.last_packet.reset();
                workbench.generated_cache_valid = false;
                m_preview_packet_attempted = false;
                service.ClearSelection();
                workbench.reopened_test =
                    std::make_shared<WorkbenchExperiment>(std::move(opened));
                m_action_message = "Reopened inactive synthetic test";
            } else {
                m_action_message = error;
            }
        }
        ImGui::EndDisabled();
        if (workbench.reopened_test) {
            const WorkbenchExperiment &opened = *workbench.reopened_test;
            ImGui::TextWrapped("%s test: %s / TitleID %08X / %s",
                workbench.running_test == workbench.reopened_test ?
                    "Running" : "Inactive",
                PortableShaderHash(opened.recipe.key.hash).c_str(),
                opened.packet.selection.scope.title_id,
                PreviewBackendLabel(opened.packet.selection.backend));
            const bool backend_supported =
                opened.packet.selection.backend == active_backend;
            ImGui::BeginDisabled(!backend_supported);
            if (ImGui::Button("Run opened synthetic test")) {
                PreviewPacket packet = opened.packet;
                selection = packet.selection;
                selection.session_epoch = m_snapshot.session_epoch;
                selection.renderer_epoch =
                    nv2a_profile_preview_renderer_epoch();
                packet.selection = selection;
                std::string error;
                service.SetRequestedMode(selection.mode);
                service.SetSelection(selection, now_ns);
                if (service.SubmitPacket(packet, now_ns, &error)) {
                    workbench.running_test = workbench.reopened_test;
                    workbench.editing_test = false;
                    workbench.comparison_locked = false;
                    workbench.generated = opened.original;
                    workbench.has_generated = true;
                    workbench.generated_selection = selection;
                    workbench.generated_cache_valid = false;
                    workbench.dock.variant = packet.source_variant;
                    workbench.last_variant = packet.source_variant;
                    workbench.editor.SetSource(packet.source);
                    m_preview_scene = packet.scene;
                    service.EditScene(m_preview_scene);
                    m_preview_render_state = packet.render_state;
                    workbench.dock.bindings = ExpandWorkbenchPacketBindings(
                        packet, WorkbenchEditorDockState{}.bindings);
                    m_preview_update_policy = packet.update_policy;
                    m_preview_input_revision = packet.input_revision;
                    PreviewSyntheticFixture restored{};
                    if (DecodePreviewSyntheticFixture(packet.fixture_bytes,
                                                      &restored, &error)) {
                        m_preview_fixture = restored;
                        for (size_t i = 0; i < 4; ++i) {
                            for (size_t c = 0; c < 4; ++c) {
                                m_preview_colors[i][c] =
                                    restored.corner_colors[i][c] / 255.0f;
                                m_preview_texture_colors[i][c] =
                                    restored.texture_texels[i][c] / 255.0f;
                            }
                        }
                        m_preview_uv_scale = restored.uv_scale;
                        m_preview_uv_offset = restored.uv_offset;
                        m_preview_constant_color = restored.constant_color;
                        m_preview_fog_color = restored.fog_color;
                        m_preview_alpha_reference =
                            restored.alpha_reference;
                        m_preview_linear_filter =
                            restored.linear_filter != 0;
                        m_preview_repeat_wrap = restored.repeat_wrap != 0;
                        m_preview_profile = -1;
                        m_preview_fixture_suggested = false;
                    }
                    service.EditClock(PreviewClockAction::Pause, 0, now_ns);
                    service.EditClock(PreviewClockAction::LoopLength,
                                      opened.clock.loop_seconds, now_ns);
                    service.EditClock(PreviewClockAction::Loop,
                                      opened.clock.loop ? 1 : 0, now_ns);
                    service.EditClock(PreviewClockAction::Speed,
                                      opened.clock.speed, now_ns);
                    service.EditClock(PreviewClockAction::Scrub,
                                      opened.clock.time_seconds, now_ns);
                    if (opened.clock.playing)
                        service.EditClock(PreviewClockAction::Play, 0,
                                          now_ns);
                    workbench.last_packet = packet;
                    m_preview_packet_selection = selection;
                    m_preview_packet_detail_generation =
                        m_detail_snapshot.generation;
                    m_preview_packet_override_generation =
                        selection.mode == PreviewMode::Replacement ?
                            GetOverrideStore().Generation() : 0;
                    m_preview_packet_scope_available = true;
                    m_preview_packet_attempted = true;
                    m_preview_packet_message.clear();
                    m_action_message = "Running reopened synthetic test";
                } else {
                    m_action_message = error;
                }
            }
            ImGui::EndDisabled();
            if (!backend_supported) {
                ImGui::TextDisabled(
                    "Switch to the test's renderer backend to run");
            }
            if (ImGui::Button("Export opened test")) {
                std::filesystem::path output;
                std::string error;
                if (ExportWorkbenchExperiment(opened, base,
                                              &output, &error)) {
                    m_action_message = "Exported synthetic test to " +
                        output.u8string();
                    RefreshWorkbenchTestFiles(&workbench, base);
                } else {
                    m_action_message = error;
                }
            }
        }
        if (workbench.running_test) {
            if (ImGui::Button("Return to live source")) {
                workbench.running_test.reset();
                workbench.editing_test = false;
                workbench.comparison_locked = false;
                workbench.last_packet.reset();
                workbench.generated_cache_valid = false;
                m_preview_packet_attempted = false;
                service.ClearSelection();
                m_action_message = "Returned to selected live shader";
            }
        }
    }
    if (controls_visible &&
        ImGui::BeginTable("##preview-status", 2,
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

    if (controls_visible && status.has_attempt) {
        ImGui::TextWrapped("Attempted %s: %s",
            PreviewModeLabel(status.attempted_compile.selection.mode),
            PreviewSourceIdentity(status.attempted_compile).c_str());
    }
    if (controls_visible)
        ImGui::TextWrapped("%s", status.message.c_str());
    if (controls_visible && !m_preview_packet_message.empty()) {
        ImGui::TextWrapped("Preview inputs: %s",
                           m_preview_packet_message.c_str());
    }
    if (controls_visible && entry &&
        entry->key.stage != Stage::Pixel) {
        ImGui::TextWrapped(
            "Stage 4 v1 intentionally supports pixel/fragment shaders first. "
            "The selected %s row remains available for inspection.",
            StageLabel(entry->key.stage));
    }

    if (controls_visible && ImGui::CollapsingHeader("Comparison")) {
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
    if (controls_visible) {
        ImGui::PopItemWidth();
        if (desktop_inspector) ImGui::EndChild();
        else ImGui::EndPopup();
    }
    ImGui::SetCursorPos(pane_pos);
    ImGui::BeginChild("##workbench-viewport",
                      ImVec2(scene_width, top_height), false);
    if (ImGui::BeginChild("##workbench-header",
                          ImVec2(0.0f, header_height), true)) {
        if (desktop_inspector) {
            ImGui::TextDisabled("PRIVATE SYNTHETIC TEST | %s renderer",
                                PreviewBackendLabel(active_backend));
        }
        bool replacement_source_available = false;
        if (!workbench.running_test && !workbench.has_generated &&
            mode == static_cast<int>(PreviewMode::Replacement)) {
            const OverrideBackend backend =
                active_backend == PreviewBackend::OpenGL ?
                    OverrideBackend::OpenGL : OverrideBackend::Vulkan;
            auto replacement = GetShaderOverrideUi().
                AcquireSelectedReplacement(backend);
            replacement_source_available = replacement &&
                !(backend == OverrideBackend::OpenGL ?
                    replacement->opengl_source :
                    replacement->vulkan_source).empty();
        }
        const char *unavailable = nullptr;
        if (!entry) {
            unavailable = "Select a pixel shader";
        } else if (entry->key.stage != Stage::Pixel) {
            unavailable = "Only pixel shaders can run in this scene";
        } else if (!workbench.running_test &&
                   (!m_has_recipe || m_selected_recipe.key != entry->key)) {
            unavailable = "Canonical recipe is unavailable";
        } else if (!workbench.running_test &&
                   !workbench.has_generated &&
                   !replacement_source_available) {
            unavailable = workbench.generated_error.empty() ?
                "Selected shader source is unavailable" :
                workbench.generated_error.c_str();
        } else if (!runstate_is_running() && !guest_paused &&
                   !offline_no_guest) {
            unavailable = "Guest is not running or paused";
        } else if (status.state == PreviewState::Failed ||
                   status.state == PreviewState::Unsupported) {
            unavailable = status.message.c_str();
        }
        if (desktop_inspector) ImGui::SameLine();
        if (enabled && status.prepared) {
            if (ImGui::Button("Stop private test")) {
                service.SetEnabled(false);
                workbench.start_requested = false;
                workbench.start_error.clear();
                m_preview_packet_attempted = false;
                enabled = false;
            }
        } else {
            const char *start_label = workbench.start_requested ?
                "Preparing private test" :
                guest_paused || offline_no_guest ?
                    "Start private test" : "Pause game and start test";
            ImGui::BeginDisabled(unavailable || workbench.start_requested ||
                                 status.work_active);
            if (ImGui::Button(start_label)) {
                if (runstate_is_running()) vm_stop(RUN_STATE_PAUSED);
                const bool paused_now = runstate_check(RUN_STATE_PAUSED);
                if (paused_now || offline_no_guest) {
                    service.SetGuestPaused(paused_now);
                    service.SetEnabled(true);
                    enabled = true;
                    workbench.start_requested = true;
                    workbench.start_selection = selection;
                    workbench.start_error.clear();
                    m_preview_packet_attempted = false;
                    m_action_message =
                        "Preparing a private synthetic test";
                } else {
                    m_action_message = "Could not pause the guest";
                    workbench.start_error = m_action_message;
                }
            }
            ImGui::EndDisabled();
            if (enabled) {
                ImGui::SameLine();
                if (ImGui::Button("Stop private test")) {
                    service.SetEnabled(false);
                    workbench.start_requested = false;
                    workbench.start_error.clear();
                    m_preview_packet_attempted = false;
                    enabled = false;
                }
            }
        }
        if (runstate_check(RUN_STATE_PAUSED)) {
            ImGui::SameLine();
            if (ImGui::Button("Resume game")) {
                workbench.start_requested = false;
                workbench.start_error.clear();
                vm_start();
                service.SetGuestPaused(false);
                m_action_message = "Game resumed";
            }
        }
        const char *preview_message = unavailable ? unavailable :
            !workbench.start_error.empty() ?
                workbench.start_error.c_str() :
            !m_preview_packet_message.empty() ?
                m_preview_packet_message.c_str() : status.message.c_str();
        ImGui::Text("%s: %.64s", PreviewStateLabel(status.state),
                    preview_message);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s: %s", PreviewStateLabel(status.state),
                              preview_message);
        if (ImGui::Button(controls_visible ? "Hide inspector" : "Inspector")) {
            if (desktop_inspector) {
                workbench.controls_open = !workbench.controls_open;
            } else if (!controls_visible) {
                workbench.compact_inspector_pending = true;
            }
        }
        ImGui::SameLine();
        ImGui::Checkbox("GLSL / output", &workbench.dock.open);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150.0f * g_viewport_mgr.m_scale);
        if (ImGui::Combo("Mode", &mode,
                         "Normal\0Uber Shader\0Replacement Shader\0"
                         "Visualize\0")) {
            workbench.running_test.reset();
            workbench.editing_test = false;
            workbench.comparison_locked = false;
            workbench.generated_cache_valid = false;
            workbench.start_requested = false;
            workbench.start_error.clear();
            m_preview_packet_attempted = false;
            service.SetRequestedMode(static_cast<PreviewMode>(mode));
        }
    }
    ImGui::EndChild();
    if (ImGui::BeginChild("##preview-output",
                          ImVec2(0.0f, output_height), true)) {
        if (!workbench.comparison_locked) {
            if (DrawWorkbenchSceneToolbar(&m_preview_scene)) {
                service.EditScene(m_preview_scene);
                m_preview_packet_attempted = false;
            }
        } else {
            ImGui::TextDisabled("Camera locked for Original / Edited comparison");
        }
        ImGui::Separator();
        if (entry) {
            PreviewViewSettings view{};
            GetPreviewGlExecutor().DrawImage(image_max_width,
                                             image_max_height,
                                             entry && entry->key.stage == Stage::Pixel ?
                                                 &selection : nullptr,
                                             now_ns, &view,
                                             workbench.comparison_locked ?
                                                 nullptr : &m_preview_scene);
        } else {
            ImGui::TextWrapped(
                "Select a pixel shader to prepare a private preview.");
        }
        if (workbench.comparison_locked) ImGui::BeginDisabled();
        if (ImGui::SmallButton(status.clock.playing ? "Pause##clock" :
                                                   "Play##clock")) {
            if (!status.clock.playing &&
                m_preview_update_policy != PreviewUpdatePolicy::Continuous) {
                m_preview_update_policy = PreviewUpdatePolicy::Continuous;
                m_preview_packet_attempted = false;
            }
            service.EditClock(status.clock.playing ?
                PreviewClockAction::Pause : PreviewClockAction::Play,
                0, now_ns);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(std::max(80.0f,
            ImGui::GetContentRegionAvail().x - 150.0f * g_viewport_mgr.m_scale));
        float clock_time = static_cast<float>(status.clock.time_seconds);
        const float clock_end = std::max(1.0f,
            static_cast<float>(status.clock.loop ?
                status.clock.loop_seconds : 10.0));
        if (ImGui::SliderFloat("##clock-time", &clock_time, 0.0f,
                               clock_end, "%.2f s"))
            service.EditClock(PreviewClockAction::Scrub, clock_time, now_ns);
        ImGui::SameLine();
        bool clock_loop = status.clock.loop;
        if (ImGui::Checkbox("Loop##clock", &clock_loop))
            service.EditClock(PreviewClockAction::Loop, clock_loop, now_ns);
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##clock"))
            service.EditClock(PreviewClockAction::Restart, 0, now_ns);
        if (workbench.comparison_locked) ImGui::EndDisabled();
        ImGui::TextDisabled("Synthetic inputs; this is not an in-game draw");
        if (visualize) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", PreviewChannelLabel(
                static_cast<PreviewChannel>(m_preview_view_channel)));
        }
    }
    ImGui::EndChild();
    ImGui::EndChild();
    WorkbenchEditorDockAction editor_action{};
    if (editor_visible) {
        ImGui::SetCursorPos(ImVec2(pane_pos.x, pane_pos.y + top_height));
        ImGui::InvisibleButton("##workbench-dock-resize",
                               ImVec2(pane_size.x, gap));
        const ImVec2 grip_min = ImGui::GetItemRectMin();
        const ImVec2 grip_max = ImGui::GetItemRectMax();
        const float grip_center = (grip_min.x + grip_max.x) * 0.5f;
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(grip_center - 16.0f, (grip_min.y + grip_max.y) * 0.5f),
            ImVec2(grip_center + 16.0f, (grip_min.y + grip_max.y) * 0.5f),
            ImGui::GetColorU32(ImGui::IsItemHovered() || ImGui::IsItemActive() ?
                ImGuiCol_ButtonHovered : ImGuiCol_Border), 2.0f);
        if (ImGui::IsItemActive())
            preferred_editor_height = std::clamp(
                preferred_editor_height - ImGui::GetIO().MouseDelta.y /
                    g_viewport_mgr.m_scale,
                100.0f, std::max(100.0f,
                    pane_size.y * 0.48f / g_viewport_mgr.m_scale));
        ImGui::SetCursorPos(ImVec2(
            pane_pos.x, pane_pos.y + top_height + gap));
        if (ImGui::BeginChild("##workbench-editor",
                              ImVec2(pane_size.x, editor_height), true)) {
            if (ImGui::BeginTabBar("##workbench-dock-tabs")) {
            if (ImGui::BeginTabItem("Generated GLSL / draft")) {
            if (entry) {
                if (workbench.running_test && !workbench.editing_test) {
                    const WorkbenchExperiment &saved = *workbench.running_test;
                    workbench.dock.draft_view_valid = false;
                    ImGui::SeparatorText("Saved generated GLSL / draft");
                    ImGui::TextDisabled("Original: %s, saved revision %llu",
                        saved.original.resident ? "resident" : "reconstructed",
                        static_cast<unsigned long long>(saved.edit_revision));
                    if (ImGui::RadioButton("Original",
                            workbench.dock.variant ==
                                PreviewSourceVariant::Original))
                        workbench.dock.variant = PreviewSourceVariant::Original;
                    if (saved.successful_compile) {
                        ImGui::SameLine();
                        if (ImGui::RadioButton("Edited",
                                workbench.dock.variant ==
                                    PreviewSourceVariant::Edited))
                            workbench.dock.variant = PreviewSourceVariant::Edited;
                    }
                    const std::string &shown =
                        workbench.dock.variant == PreviewSourceVariant::Edited &&
                        saved.successful_compile ?
                            saved.successful_compile->source :
                            saved.original.text;
                    if (workbench.dock.variant != workbench.last_variant)
                        workbench.editor.SetSource(shown);
                    DrawWorkbenchSourceLines(&workbench.editor,
                                             &workbench.dock);
                    if (ImGui::Button("Create editable copy of saved GLSL")) {
                        std::string error;
                        const bool supported_route =
                            selection.mode != PreviewMode::Replacement &&
                            saved.original.route ==
                                (selection.mode == PreviewMode::Uber ?
                                    Route::Uber : Route::Specialized);
                        if (supported_route &&
                            workbench.draft.Create(saved.original,
                                                   workbench.next_draft_id++,
                                                   &error)) {
                            const std::string &saved_text =
                                !saved.edited_text.empty() ? saved.edited_text :
                                shown;
                            workbench.draft.Edit(saved_text, now_ns);
                            workbench.editing_test = true;
                            workbench.submitted_draft.reset();
                            workbench.dock.variant =
                                PreviewSourceVariant::Edited;
                            workbench.dock.buffer_revision = 0;
                            workbench.dock.edit_buffer.clear();
                            workbench.editor.SetSource({});
                            m_preview_packet_attempted = false;
                            m_action_message =
                                "Created private draft from saved GLSL";
                        } else {
                            m_action_message = error.empty() ?
                                "Saved source route cannot become an editable preview draft" :
                                error;
                        }
                    }
                } else {
                    const bool draft_matches = WorkbenchDraftMatches(
                        workbench.draft, selection);
                    editor_action = DrawWorkbenchEditorDock(
                        workbench.has_generated ? &workbench.generated : nullptr,
                        draft_matches ? &workbench.draft : nullptr,
                        &workbench.editor, &workbench.dock, now_ns);
                }
            } else {
                ImGui::TextDisabled("Select a pixel shader to inspect GLSL");
            }
            ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Output")) {
                ImGui::Text("%s", PreviewStateLabel(status.state));
                ImGui::TextWrapped("%s", status.message.c_str());
                if (!m_preview_packet_message.empty()) {
                    ImGui::SeparatorText("Preview inputs");
                    ImGui::TextWrapped("%s", m_preview_packet_message.c_str());
                }
                if (status.has_attempt) {
                    ImGui::TextWrapped("Attempted: %s",
                        PreviewSourceIdentity(status.attempted_compile).c_str());
                }
                if (status.has_displayed_source) {
                    ImGui::TextWrapped("Displayed: %s",
                        PreviewSourceIdentity(status.displayed_compile).c_str());
                }
                if (workbench.draft.Id() &&
                    !workbench.draft.Diagnostics().empty()) {
                    ImGui::SeparatorText("Compiler messages");
                    ImGui::TextWrapped("%s",
                        workbench.draft.Diagnostics().c_str());
                }
                ImGui::TextDisabled(
                    "Private preview output; no game rule is changed by a compile.");
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
            }
        }
        ImGui::EndChild();
    }
    if (entry) {
        if (editor_action.create_copy && workbench.has_generated) {
            if (workbench.draft.Create(workbench.generated,
                                       workbench.next_draft_id++,
                                       &m_action_message)) {
                workbench.submitted_draft.reset();
                workbench.dock.variant = PreviewSourceVariant::Edited;
                workbench.dock.buffer_revision = 0;
                workbench.dock.edit_buffer.clear();
                workbench.editor.SetSource({});
                m_preview_packet_attempted = false;
                m_action_message = "Created private editable GLSL draft";
            }
        }
        if (workbench.dock.variant != workbench.last_variant) {
            const PreviewSourceVariant previous = workbench.last_variant;
            if ((WorkbenchDraftMatches(workbench.draft, selection) ||
                 (workbench.running_test &&
                  workbench.running_test->successful_compile)) &&
                !workbench.comparison_locked &&
                !editor_action.create_copy &&
                !lock_comparison(selection, previous)) {
                if (previous == PreviewSourceVariant::Edited &&
                    workbench.dock.variant == PreviewSourceVariant::Original) {
                    m_action_message = "Switched to Original without a "
                                       "comparison lock; Edited has no "
                                       "current matching sample";
                } else {
                    workbench.dock.variant = previous;
                    if (workbench.running_test && !workbench.editing_test) {
                        const WorkbenchExperiment &saved =
                            *workbench.running_test;
                        workbench.editor.SetSource(
                            previous == PreviewSourceVariant::Edited &&
                            saved.successful_compile ?
                                saved.successful_compile->source :
                                saved.original.text);
                    }
                }
            }
            if (workbench.dock.variant != previous) {
                workbench.last_variant = workbench.dock.variant;
                m_preview_packet_attempted = false;
            }
        }
        if ((!workbench.running_test || workbench.editing_test) &&
            WorkbenchDraftMatches(workbench.draft, selection) &&
            workbench.dock.variant == PreviewSourceVariant::Edited &&
            (workbench.dock.auto_compile || editor_action.manual_compile)) {
            auto frozen = workbench.draft.FreezeCompile(
                now_ns, editor_action.manual_compile);
            if (frozen) {
                workbench.submitted_draft = std::move(*frozen);
                m_preview_packet_attempted = false;
            }
        }
    }
    ImGui::SetCursorPos(ImVec2(pane_pos.x,
                               pane_pos.y + pane_size.y));
}

// SPDX-License-Identifier: GPL-2.0-or-later
namespace {
struct CaptureComparisonUiState {
    CaptureComparisonController controller;
    CaptureComparisonSnapshot snapshot;
    std::optional<CaptureComparisonJob> job;
    uint64_t revision = 0, heartbeat = 0, pump_time = 0, job_started = 0;
    uint64_t selected_event = 0;
    uint64_t image_revision = 0, image_event = 0;
    int cached_image = -1;
    OwnedDrawImage shown;
    int scope = 2, image = 1;
    bool suffix = false, tested_suffix = false, profile_draw = false;
    std::string message;
};
CaptureComparisonUiState &CaptureComparisons()
{
    static CaptureComparisonUiState state;
    return state;
}
static bool CaptureComparisonVisible()
{
    const uint64_t now = PreviewNowNs();
    const uint64_t last = CaptureComparisons().heartbeat;
    return last && now >= last && now - last < kPreviewVisibilityStaleNs;
}
const char *CaptureComparisonOutcomeLabel(CaptureComparisonOutcome outcome)
{
    switch (outcome) {
    case CaptureComparisonOutcome::Pending:
        return "Pending";
    case CaptureComparisonOutcome::Completed:
        return "Compared";
    case CaptureComparisonOutcome::CompileFailed:
        return "Compile failed";
    case CaptureComparisonOutcome::Unsupported:
        return "Unsupported";
    case CaptureComparisonOutcome::Incomplete:
        return "Incomplete";
    case CaptureComparisonOutcome::Cancelled:
        return "Cancelled";
    }
    return "Unknown";
}
void PumpCaptureComparison(CaptureComparisonUiState &state, uint64_t now)
{
    PreviewService &service = GetPreviewService();
    const bool paused = runstate_check(RUN_STATE_PAUSED);
    const bool offline =
        CurrentWorkbenchBuildScope().title_id == 0 && !runstate_is_running();
    service.SetVisible(true, now);
    service.SetGuestPaused(paused);
    service.SetOfflineNoGuest(offline);
    service.UpdateHealth({ now, PreviewPressure::Normal, false });
    if ((!paused && !offline) ||
        state.snapshot.state != CaptureComparisonState::Running)
        return;
    bool needs_submit =
        state.pump_time && now - state.pump_time >= kPreviewVisibilityStaleNs;
    state.pump_time = now;
    auto finish =
        [&](CaptureComparisonOutcome outcome, const std::string &message,
            CaptureComparisonImage image = {}, PreviewDrawTiming timing = {}) {
            if (!state.job)
                return;
            CaptureComparisonCompletion result;
            result.identity = state.job->identity;
            result.phase = state.job->phase;
            result.result = state.job->expected_result;
            result.outcome = outcome;
            result.message = message;
            result.image = std::move(image);
            result.draw_timing = std::move(timing);
            state.controller.Complete(std::move(result));
            state.job.reset();
        };
    if (state.job &&
        !state.controller.StillCurrent(state.job->identity, state.job->phase)) {
        state.job.reset();
        service.SetEnabled(false);
    }
    if (!state.job) {
        CaptureComparisonJob job;
        if (!state.controller.TryClaimJob(&job))
            return;
        state.job = std::move(job);
        needs_submit = true;
    }
    if (needs_submit && state.job) {
        state.job_started = now;
        const PreviewBackend active =
            g_config.display.renderer == CONFIG_DISPLAY_RENDERER_VULKAN ?
                PreviewBackend::Vulkan :
                PreviewBackend::OpenGL;
        if (state.job->packet->selection.backend != active) {
            finish(CaptureComparisonOutcome::Unsupported,
                   "Switch to the recording's renderer to compare this "
                   "occurrence.");
            return;
        }
        service.SetEnabled(true);
        service.SetRequestedMode(state.job->packet->selection.mode);
        service.SetSelection(state.job->packet->selection, now);
        service.EditChannel(PreviewChannel::FinalRGBA);
        service.UsePacketView();
        std::string error;
        if (!service.SubmitPacket(*state.job->packet, now, &error)) {
            finish(CaptureComparisonOutcome::Unsupported, error);
            return;
        }
        service.RequestPreparation(&error);
        if (!GetPreviewGlExecutor().StartWhilePaused(&error)) {
            finish(CaptureComparisonOutcome::Incomplete, error);
            return;
        }
    }
    if (service.RequestAutomaticPreparation(now)) {
        std::string error;
        if (!GetPreviewGlExecutor().StartWhilePaused(&error)) {
            finish(CaptureComparisonOutcome::Incomplete, error);
            return;
        }
    }
    PreviewStatus status;
    service.CopyStatus(&status);
    if (status.has_attempt &&
        status.attempted_compile == state.job->expected_result.compile &&
        (status.state == PreviewState::Failed ||
         status.state == PreviewState::Unsupported)) {
        finish(status.state == PreviewState::Unsupported ?
                   CaptureComparisonOutcome::Unsupported :
                   CaptureComparisonOutcome::CompileFailed,
               status.message);
        return;
    }
    CaptureComparisonImage image;
    PreviewDrawTiming timing;
    std::string error;
    if (GetPreviewGlExecutor().CopyReadyImage(state.job->expected_result,
                                              &image.width, &image.height,
                                              &image.rgba, &error, &timing)) {
        finish(CaptureComparisonOutcome::Completed, status.material_fidelity,
               std::move(image), std::move(timing));
    } else if (!error.empty()) {
        finish(CaptureComparisonOutcome::Incomplete, error);
    } else if (now - state.job_started > UINT64_C(30000000000)) {
        finish(CaptureComparisonOutcome::Incomplete,
               "No exact preview result was ready within 30 seconds.");
    }
}
void DrawCaptureComparisons(const CaptureSessionSnapshot &capture,
                            uint64_t selected_event)
{
    auto &state = CaptureComparisons();
    state.heartbeat = PreviewNowNs();
    if (state.revision != state.controller.Revision()) {
        state.revision = state.controller.Revision();
        state.snapshot = state.controller.Snapshot();
    }
    const auto &draft = WorkbenchSession().draft;
    const auto &compiled = draft.LastSuccessfulCompile();
    ShaderCombo("Test scope", &state.scope,
                "Selected occurrence\0Matching uses in selected "
                "frame\0Matching uses in captured range\0");
    ImGui::Checkbox("Measure replay draw GPU intervals", &state.profile_draw);
    if (state.profile_draw)
        ImGui::TextWrapped(
            "Instrumented replay timings include the complete draw and its "
            "active pipeline stages. They are not stage-exclusive timings or "
            "normal-gameplay measurements.");
    ImGui::Checkbox("Replay downstream consumers", &state.suffix);
    if (state.suffix)
        ImGui::TextWrapped(
            "Replays the ordered dependency chain with newly produced "
            "images. Unsupported operations or missing versions stop the "
            "comparison with an explicit result.");
    const bool can_start = compiled && compiled->revision == draft.Revision() &&
                           draft.Base().key.stage == Stage::Pixel &&
                           !capture.events.empty();
    ImGui::BeginDisabled(!can_start);
    if (ImGui::Button("Run replacement comparison")) {
        CaptureComparisonRequest request;
        request.selection.shader = draft.Base().key;
        request.selection.scope = draft.Base().scope;
        request.selection.backend = draft.Base().backend;
        request.selection.session_epoch = capture.context.session_epoch;
        request.selection.renderer_epoch = capture.context.renderer_epoch;
        request.selection.mode = draft.Base().route == Route::Uber ?
                                     PreviewMode::Uber :
                                     PreviewMode::Normal;
        request.scope = CaptureComparisonScope(state.scope);
        request.dependencies =
            state.suffix ? CaptureComparisonDependencies::ProducerSuffix :
                           CaptureComparisonDependencies::FrozenInputs;
        request.selected_event = selected_event;
        request.profile_draw = state.profile_draw;
        request.edit = { compiled->draft_id, compiled->revision,
                         compiled->submission_id, compiled->source };
        state.controller.Start(request, capture);
        state.tested_suffix = state.suffix;
        state.job.reset();
        state.selected_event = selected_event;
        if (runstate_is_running())
            vm_stop(RUN_STATE_PAUSED);
    }
    ImGui::EndDisabled();
    if (!can_start)
        ImGui::TextWrapped(
            "Open a captured occurrence in Source / replacement, create an "
            "editable GLSL copy, and compile its current revision before "
            "testing its uses.");
    if (state.snapshot.state == CaptureComparisonState::Running) {
        ImGui::SameLine();
        if (ImGui::Button("Cancel comparison")) {
            state.controller.Cancel();
            state.job.reset();
            GetPreviewService().SetEnabled(false);
        }
    }
    PumpCaptureComparison(state, state.heartbeat);
    ImGui::Text("%llu matching seeds | %zu inspected draws | "
                "%llu omitted by budget | %llu compared",
                (unsigned long long)state.snapshot.matched_events,
                state.snapshot.results.size(),
                (unsigned long long)state.snapshot.omitted_events,
                (unsigned long long)state.snapshot
                    .outcomes[size_t(CaptureComparisonOutcome::Completed)]);
    if (state.tested_suffix)
        ImGui::Text("%llu ordered steps | %.2f MiB plan | %.2f MiB values",
                    (unsigned long long)state.snapshot.replay_steps,
                    state.snapshot.replay_plan_bytes / (1024.0 * 1024.0),
                    state.snapshot.replay_value_bytes / (1024.0 * 1024.0));
    ImGui::TextWrapped(
        state.tested_suffix ?
            "Original-camera dependency replay. Edited outputs propagate to "
            "later consumers. Differences need inspection; saving or testing "
            "never enables a replacement in game." :
            "Partial original-camera replay with frozen inputs. Differences "
            "need inspection; saving or testing never enables a replacement "
            "in game.");
    if (!state.snapshot.message.empty())
        ImGui::TextWrapped("%s", state.snapshot.message.c_str());
    const auto timing_summary = [](const char *label,
                                   const PreviewDrawTimingDistribution &d) {
        if (!d.requested)
            return;
        ImGui::Text(
            "%s: %llu measured / %llu requested; %llu unsupported, "
            "%llu failed, %llu pending",
            label, (unsigned long long)d.measured,
            (unsigned long long)d.requested, (unsigned long long)d.unsupported,
            (unsigned long long)d.failed, (unsigned long long)d.pending);
        if (d.sample_count)
            ImGui::Text("Latest %u measured draws: median %.3f ms; p95 %.3f ms",
                        d.sample_count, d.median_ns / 1000000.0,
                        d.p95_ns / 1000000.0);
    };
    timing_summary("Unedited replay", state.snapshot.original_timing);
    timing_summary("Replacement replay", state.snapshot.replacement_timing);
    const float height = ImGui::GetContentRegionAvail().y;
    if (ImGui::BeginTable("##comparison-results", 7,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                          ImVec2(0, std::max(60.0f, height * 0.48f)))) {
        for (const char *label :
             { "Occurrence", "Source", "Coverage", "Changed pixels",
               "Largest channel change", "Unedited GPU", "Replacement GPU" })
            ImGui::TableSetupColumn(label);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(int(state.snapshot.results.size()));
        while (clipper.Step())
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const auto &row = state.snapshot.results[i];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                const std::string label = "F" + std::to_string(row.frame) +
                                          " / E" +
                                          std::to_string(row.identity.event_id);
                if (ShaderOptionSelected(label.c_str(),
                                         state.selected_event ==
                                             row.identity.event_id,
                                         ImGuiSelectableFlags_SpanAllColumns))
                    state.selected_event = row.identity.event_id;
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(row.edited_seed ? "Edited" :
                                                         "Downstream");
                if (!row.edited_seed && ImGui::IsItemHovered())
                    ImGui::SetTooltip("Original source; consumes edited "
                                      "dependency outputs.");
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(
                    CaptureComparisonOutcomeLabel(row.outcome));
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%llu / %llu",
                            (unsigned long long)row.difference.changed_pixels,
                            (unsigned long long)row.difference.compared_pixels);
                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%u", row.difference.maximum_difference);
                for (int column = 5; column < 7; ++column) {
                    ImGui::TableSetColumnIndex(column);
                    const auto &timing = column == 5 ? row.original_timing :
                                                       row.replacement_timing;
                    if (timing.status == PreviewDrawTimingStatus::Measured)
                        ImGui::Text("%.3f ms", timing.nanoseconds / 1000000.0);
                    else
                        ImGui::TextUnformatted(
                            PreviewDrawTimingStatusLabel(timing.status));
                    if (ImGui::IsItemHovered() && !timing.message.empty())
                        ImGui::SetTooltip("%s", timing.message.c_str());
                }
            }
        ImGui::EndTable();
    }
    for (const auto &row : state.snapshot.results)
        if (row.identity.event_id == state.selected_event) {
            if (!row.message.empty())
                ImGui::TextWrapped("%s", row.message.c_str());
            ShaderCombo(
                "Result image", &state.image,
                "Unedited replay\0Replacement replay\0Color difference\0");
            auto copy = [](const SharedComparisonImage &image) {
                return image ? OwnedDrawImage{ image->width, image->height,
                                               image->rgba } :
                               OwnedDrawImage{};
            };
            if (state.image_revision != state.revision ||
                state.image_event != row.identity.event_id ||
                state.cached_image != state.image) {
                state.shown = {};
                if (state.image == 2)
                    MakeCapturedDrawDifference(copy(row.original),
                                               copy(row.replacement),
                                               &state.shown);
                else
                    state.shown =
                        copy(state.image ? row.replacement : row.original);
                state.image_revision = state.revision;
                state.image_event = row.identity.event_id;
                state.cached_image = state.image;
            }
            DrawCapturedImage(
                state.shown, state.revision ^ (row.identity.event_id << 32),
                state.image + 1000, ImGui::GetContentRegionAvail().x,
                std::max(1.0f, ImGui::GetContentRegionAvail().y), false);
            break;
        }
}
} // namespace

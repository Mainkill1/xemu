// SPDX-License-Identifier: GPL-2.0-or-later
// Capture ownership is independent of the live catalog and preview worker.
namespace {
struct CaptureFileResult {
    std::optional<CaptureSessionSnapshot> opened;
    std::string message;
};
struct CaptureWorkspaceState {
    CaptureSessionSettings settings;
    CaptureSessionSnapshot snapshot;
    uint64_t revision = UINT64_MAX, next_poll_ns = 0;
    uint64_t selected_event = 0, selected_revision = UINT64_MAX;
    uint64_t last_frame_progress_ns = 0, last_progress_frame = 0;
    bool archived = false, pause_on_completion = true, auto_pause = false;
    bool inspector = true, events_explorer = false, close_pending = false;
    bool selection_scroll_pending = true, all_frames = true;
    uint64_t viewed_frame = 0;
    size_t recorded_draws = 0, visible_draws = 0;
    int image_view = 0, texture_slot = 0, texture_image = 0, stage = 2;
    int cpu_mib = 256, disk_mib = 512, history = 60, post = 30;
    int event_budget = 100000;
    int post_reserve_percent = 25;
    std::optional<ShaderKey> shader_filter;
    std::string search, package_path, message;
    std::vector<std::shared_ptr<const CaptureOccurrence>> visible;
    std::map<ShaderKey, size_t> shader_uses;
    uint64_t image_revision = 0;
    std::shared_ptr<const CaptureOccurrence> selected;
    OwnedDrawInputs inputs;
    OwnedDrawImage difference;
    std::future<CaptureFileResult> file_job;
    std::shared_ptr<CaptureFileControl> file_control;
};
CaptureWorkspaceState &CaptureWorkspace()
{
    static CaptureWorkspaceState state;
    return state;
}
const char *CaptureStateLabel(CaptureSessionState state)
{
    switch (state) {
    case CaptureSessionState::Recording:
        return "Recording";
    case CaptureSessionState::Triggered:
        return "Recording after mark";
    case CaptureSessionState::Finalizing:
        return "Finishing GPU work / readbacks";
    case CaptureSessionState::Ready:
        return "Ready";
    case CaptureSessionState::Cancelled:
        return "Stopped / idle";
    case CaptureSessionState::BudgetExceeded:
        return "Incomplete: budget reached";
    case CaptureSessionState::Failed:
        return "Incomplete: capture failed";
    }
    return "Unknown";
}
const char *CaptureSubmissionLabel(CaptureBatchOutcome outcome)
{
    switch (outcome) {
    case CaptureBatchOutcome::None:
        return "Submission not tracked";
    case CaptureBatchOutcome::Pending:
        return "Awaiting queue submission";
    case CaptureBatchOutcome::Submitted:
        return "Queue submission accepted";
    case CaptureBatchOutcome::SubmissionFailed:
        return "Queue submission failed";
    case CaptureBatchOutcome::Aborted:
        return "Commands discarded";
    case CaptureBatchOutcome::Detached:
        return "Submission outcome unknown";
    }
    return "Submission outcome unknown";
}
const char *CaptureEventLabel(CaptureEventType type)
{
    switch (type) {
    case CaptureEventType::Draw:
        return "Draw";
    case CaptureEventType::Clear:
        return "Clear";
    case CaptureEventType::Copy:
        return "Copy";
    case CaptureEventType::Upload:
        return "Upload";
    case CaptureEventType::Resolve:
        return "Resolve";
    case CaptureEventType::FrameBoundary:
        return "Frame";
    case CaptureEventType::StateWrite:
        return "Command / state";
    case CaptureEventType::AllocationBoundary:
        return "Allocation";
    case CaptureEventType::ResetBoundary:
        return "Renderer reset";
    case CaptureEventType::TitleBoundary:
        return "Title change";
    case CaptureEventType::SaveStateBoundary:
        return "Save-state boundary";
    }
    return "Unknown";
}
bool EventUses(const CaptureOccurrence &event, const ShaderKey &shader)
{
    for (size_t i = 0; i < event.summary.shader_count; ++i)
        if (event.summary.shaders[i] == shader)
            return true;
    return false;
}
std::string CaptureShaderLabel(const ShaderKey &key)
{
    const auto hash = ShaderHashBase32(key.hash);
    return std::string(StageLabel(key.stage)) + " " + hash.substr(0, 4) + "-" +
           hash.substr(4, 4);
}
void RefreshCaptureSelection(CaptureWorkspaceState &state)
{
    auto found =
        std::find_if(state.snapshot.events.begin(), state.snapshot.events.end(),
                     [&](const auto &event) {
                         return event->event_id == state.selected_event;
                     });
    auto event = found == state.snapshot.events.end() ? nullptr : *found;
    if (event == state.selected)
        return;
    state.selected = event;
    state.selected_revision = state.revision;
    state.image_revision = PreviewNowNs();
    // Pending records contain identity only; they never expose mutable bytes.
    state.inputs =
        event && !event->pending ? event->CopyInputs() : OwnedDrawInputs{};
    state.difference = {};
    MakeCapturedDrawDifference(state.inputs.before, state.inputs.after,
                               &state.difference);
    state.texture_image = 0;
}
void FilterCaptureEvents(CaptureWorkspaceState &state)
{
    state.visible.clear();
    state.shader_uses.clear();
    state.recorded_draws = state.visible_draws = 0;
    if (!state.all_frames && !state.snapshot.retained_frames.empty()) {
        const auto &frames = state.snapshot.retained_frames;
        auto frame =
            std::lower_bound(frames.begin(), frames.end(), state.viewed_frame);
        if (frame == frames.end())
            --frame;
        state.viewed_frame = *frame;
    }
    for (const auto &event : state.snapshot.events) {
        if (event->type == CaptureEventType::Draw)
            ++state.recorded_draws;
        if (!state.all_frames && event->summary.key.frame != state.viewed_frame)
            continue;
        if (event->type == CaptureEventType::Draw)
            for (size_t i = 0; i < event->summary.shader_count; ++i)
                ++state.shader_uses[event->summary.shaders[i]];
        if (!state.events_explorer && event->type != CaptureEventType::Draw)
            continue;
        if (state.shader_filter && !EventUses(*event, *state.shader_filter))
            continue;
        if (!state.search.empty()) {
            std::string label = std::to_string(event->event_id) + " " +
                                CaptureEventLabel(event->type) + " " +
                                std::to_string(event->summary.key.frame);
            for (size_t i = 0; i < event->summary.shader_count; ++i)
                label += " " + CaptureShaderLabel(event->summary.shaders[i]);
            if (label.find(state.search) == std::string::npos)
                continue;
        }
        state.visible.push_back(event);
        if (event->type == CaptureEventType::Draw)
            ++state.visible_draws;
    }
    if (state.visible.empty())
        state.selected_event = 0;
    if (!state.visible.empty() &&
        std::none_of(state.visible.begin(), state.visible.end(),
                     [&](const auto &event) {
                         return event->event_id == state.selected_event;
                     })) {
        state.selected_event = state.visible.front()->event_id;
        state.selection_scroll_pending = true;
    }
    RefreshCaptureSelection(state);
}
void DrawCaptureFrameNavigation(CaptureWorkspaceState &state)
{
    const auto &frames = state.snapshot.retained_frames;
    if (frames.empty())
        return;
    if (state.all_frames && state.selected)
        state.viewed_frame = state.selected->summary.key.frame;
    bool changed = ImGui::Checkbox("All retained frames", &state.all_frames);
    if (changed && !state.all_frames)
        state.viewed_frame =
            state.selected ? state.selected->summary.key.frame : frames.front();
    auto found =
        std::lower_bound(frames.begin(), frames.end(), state.viewed_frame);
    int index = found == frames.end() ? int(frames.size() - 1) :
                                        int(found - frames.begin());
    ImGui::SameLine();
    ImGui::BeginDisabled(index == 0);
    if (ImGui::ArrowButton("##capture-previous-frame", ImGuiDir_Left)) {
        --index;
        state.all_frames = false;
        changed = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(state.all_frames);
    ImGui::SetNextItemWidth(
        std::max(120.0f, ImGui::GetContentRegionAvail().x - 105.0f));
    const std::string label = "F" + std::to_string(frames[index]);
    changed |= ImGui::SliderInt("Guest frame", &index, 0,
                                int(frames.size() - 1), label.c_str());
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(index + 1 >= int(frames.size()));
    if (ImGui::ArrowButton("##capture-next-frame", ImGuiDir_Right)) {
        ++index;
        state.all_frames = false;
        changed = true;
    }
    ImGui::EndDisabled();
    if (changed) {
        state.viewed_frame = frames[index];
        FilterCaptureEvents(state);
        state.selection_scroll_pending = true;
    }
    if (!state.all_frames)
        ImGui::TextDisabled(
            "Showing retained F%llu; recording scope remains all stages.",
            (unsigned long long)state.viewed_frame);
}
void SelectCaptureEvent(CaptureWorkspaceState &state, uint64_t id)
{
    if (state.selected_event == id)
        return;
    state.selected_event = id;
    state.selected_revision = UINT64_MAX;
    state.image_view = 0;
    state.selection_scroll_pending = true;
    RefreshCaptureSelection(state);
}
void DrawCaptureEventInspector(CaptureWorkspaceState &state)
{
    if (!state.selected) {
        ImGui::TextDisabled("Select an occurrence to inspect its inputs.");
        return;
    }
    const auto &event = *state.selected;
    ImGui::Text("E%llu | F%llu | %s | %s", (unsigned long long)event.event_id,
                (unsigned long long)event.summary.key.frame,
                event.emitted ? "Command emitted" : "No GPU draw",
                event.pending ? "Pending" : "Inputs retired");
    if (event.batch_id) {
        ImGui::TextDisabled("Batch %llu | %s",
                            (unsigned long long)event.batch_id,
                            CaptureSubmissionLabel(event.submission));
        if (event.completion == CaptureBatchCompletion::Pending)
            ImGui::TextDisabled("GPU completion pending");
        else if (event.completion == CaptureBatchCompletion::Completed)
            ImGui::TextDisabled("GPU completion confirmed");
        else if (event.completion == CaptureBatchCompletion::Failed)
            ImGui::TextDisabled("GPU completion failed; output unverified");
    }
    if (!event.failure.empty())
        ImGui::TextWrapped("%s", event.failure.c_str());
    ImGui::TextDisabled("Limitations: 0x%X", event.limitations);
    for (size_t i = 0; i < event.summary.shader_count; ++i)
        ImGui::TextWrapped(
            "%s", CaptureShaderLabel(event.summary.shaders[i]).c_str());
    CaptureExportIdentity identity;
    identity.scope = event.summary.scope;
    identity.backend = state.snapshot.context.backend == 2 ?
                           PreviewBackend::Vulkan :
                           PreviewBackend::OpenGL;
    for (size_t i = 0; i < event.summary.shader_count; ++i)
        if (event.summary.shaders[i].stage == Stage::Pixel)
            identity.shader = event.summary.shaders[i];
    DrawCapturedInputs(state.inputs, state.image_revision, &state.texture_slot,
                       &state.texture_image, &identity, &event.summary.key);
    if (ImGui::CollapsingHeader("Raw input evidence"))
        for (const auto &blob : state.inputs.blobs)
            ImGui::TextWrapped("%s | %zu bytes | stride %u | format 0x%X",
                               blob.name.c_str(), blob.bytes.size(),
                               blob.stride, blob.format);
}
static void PollAllShaderCapture()
{
    CaptureWorkspaceState &state = CaptureWorkspace();
    const uint64_t now = PreviewNowNs();
    if (state.file_job.valid() && state.file_job.wait_for(std::chrono::seconds(
                                      0)) == std::future_status::ready) {
        CaptureFileResult result;
        try {
            result = state.file_job.get();
        } catch (const std::exception &error) {
            if (state.file_control)
                state.file_control->Finish(false);
            result.message =
                std::string("Capture file operation failed: ") + error.what();
        }
        state.message = std::move(result.message);
        if (result.opened) {
            ResetCaptureInspectionUi();
            state.snapshot = std::move(*result.opened);
            state.archived = true;
            ++state.revision;
            state.selected_event = 0;
            state.all_frames = true;
            state.shader_filter.reset();
            FilterCaptureEvents(state);
        }
    }
    if (state.archived || now < state.next_poll_ns)
        return;
    state.next_poll_ns = now + UINT64_C(250000000);
    CaptureSession &session = GetCaptureSession();
    const uint64_t revision = session.Revision();
    if (revision != state.revision) {
        state.revision = revision;
        state.snapshot = session.Snapshot();
        FilterCaptureEvents(state);
    }
    if (!runstate_is_running() ||
        state.snapshot.last_frame != state.last_progress_frame) {
        state.last_progress_frame = state.snapshot.last_frame;
        state.last_frame_progress_ns = now;
    }
    if (session.Active() && runstate_is_running() &&
        state.last_frame_progress_ns &&
        now - state.last_frame_progress_ns > UINT64_C(15000000000)) {
        session.Stop();
        state.message = "Recording stopped: no guest-frame progress for 15 "
                        "seconds. Retained evidence is kept.";
    }
    const auto status = state.snapshot.state;
    if (state.auto_pause && status != CaptureSessionState::Recording &&
        status != CaptureSessionState::Triggered &&
        status != CaptureSessionState::Finalizing) {
        if (runstate_is_running())
            vm_stop(RUN_STATE_PAUSED);
        state.auto_pause = false;
    }
}
static bool ConfirmCaptureClose()
{
    CaptureWorkspaceState &state = CaptureWorkspace();
    if (!GetCaptureSession().Active() && !state.snapshot.pending_events)
        return true;
    state.close_pending = true;
    ImGui::OpenPopup("Close while recording?");
    return false;
}
static void DrawCaptureCloseChoice(bool *open)
{
    CaptureWorkspaceState &state = CaptureWorkspace();
    if (state.close_pending)
        ImGui::OpenPopup("Close while recording?");
    if (!ImGui::BeginPopupModal("Close while recording?", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;
    ImGui::TextWrapped("The all-shader session is still recording or finishing "
                       "GPU work / readbacks.");
    if (ImGui::Button("Keep recording and close")) {
        state.close_pending = false;
        *open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop recording and close")) {
        GetCaptureSession().Stop();
        state.close_pending = false;
        *open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Keep open")) {
        state.close_pending = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}
const char *CaptureFilePhaseLabel(CaptureFilePhase phase)
{
    switch (phase) {
    case CaptureFilePhase::Preparing:
        return "Preparing capture";
    case CaptureFilePhase::IndexingEvents:
        return "Indexing events";
    case CaptureFilePhase::WritingResources:
        return "Writing captured resources";
    case CaptureFilePhase::WritingMetadata:
        return "Writing capture manifest";
    case CaptureFilePhase::ReadingManifest:
        return "Reading capture manifest";
    case CaptureFilePhase::ReadingResources:
        return "Reading captured resources";
    case CaptureFilePhase::ReadingEvents:
        return "Reading events";
    case CaptureFilePhase::Validating:
        return "Validating capture";
    case CaptureFilePhase::Publishing:
        return "Finishing capture file operation";
    case CaptureFilePhase::Complete:
        return "Capture file operation finished";
    }
    return "Capture file operation";
}
void DrawCaptureFileProgress(CaptureWorkspaceState &state)
{
    if (!state.file_job.valid() || !state.file_control)
        return;
    const auto progress = state.file_control->Progress();
    ImGui::Text("%s%s", CaptureFilePhaseLabel(progress.phase),
                progress.cancel_requested ? " — cancelling…" : "…");
    if (progress.total) {
        ImGui::ProgressBar(float(double(progress.completed) / progress.total),
                           ImVec2(-1, 0));
        ImGui::TextDisabled("%llu / %llu in this step",
                            (unsigned long long)progress.completed,
                            (unsigned long long)progress.total);
    }
    ImGui::BeginDisabled(progress.cancel_requested || progress.finished ||
                         progress.publication_started);
    if (ImGui::Button("Cancel file operation"))
        state.file_control->RequestCancel();
    ImGui::EndDisabled();
}
void CaptureFileJob(CaptureWorkspaceState &state, bool open,
                    CaptureSessionSnapshot snapshot = {},
                    bool annotations = true)
{
    try {
        const std::filesystem::path path =
            std::filesystem::u8path(state.package_path);
        const auto tracks = annotations ? CaptureInspectionUi().tracks :
                                          std::vector<CaptureTemporalTrack>{};
        const auto owner = CaptureInspectionUi().recording;
        const auto control = std::make_shared<CaptureFileControl>();
        state.file_control = control;
        state.file_job = std::async(
            std::launch::async,
            [path, open, snapshot = std::move(snapshot), tracks, owner,
             annotations, control]() mutable {
                CaptureFileResult result;
                std::string error;
                bool success = false;
                if (open) {
                    CaptureSessionSnapshot loaded;
                    if (CaptureSession::Reopen(path, &loaded, &error,
                                               control.get())) {
                        success = true;
                        result.opened = std::move(loaded);
                        result.message =
                            "Opened captured game data from " + path.u8string();
                    }
                } else if ((!annotations ||
                            PrepareCaptureInspectionAnnotations(
                                snapshot, tracks, owner, &error)) &&
                           CaptureSession::SaveSnapshot(snapshot, path, &error,
                                                        control.get())) {
                    success = true;
                    result.message = "Saved capture to " + path.u8string();
                }
                if (!success)
                    result.message = error.empty() ?
                                         "Capture file operation failed." :
                                         error;
                control->Finish(success);
                return result;
            });
    } catch (const std::exception &error) {
        state.file_control.reset();
        state.message =
            std::string("Cannot start capture file operation: ") + error.what();
    }
}
} // namespace

void ShaderBrowserWindow::RequestClose()
{
    const auto snapshot = GetCaptureSession().Snapshot();
    if (GetCaptureSession().Active() || snapshot.pending_events) {
        CaptureWorkspace().close_pending = true;
        m_is_open = true;
    } else
        m_is_open = false;
}

void ShaderBrowserWindow::DrawCaptureExplorer()
{
    CaptureWorkspaceState &state = CaptureWorkspace();
    int explorer = state.events_explorer ? 1 : 0;
    if (ShaderCombo("Browse", &explorer, "Shaders\0Events\0")) {
        state.events_explorer = explorer != 0;
        state.shader_filter.reset();
        FilterCaptureEvents(state);
    }
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##capture-filter", "Search capture...",
                                 &state.search))
        FilterCaptureEvents(state);
    if (ImGui::Selectable("All captured events", !state.shader_filter)) {
        state.shader_filter.reset();
        FilterCaptureEvents(state);
    }
    if (!state.events_explorer) {
        std::optional<ShaderKey> chosen;
        if (ImGui::BeginChild("##capture-shaders")) {
            for (const auto &item : state.shader_uses) {
                const std::string label = CaptureShaderLabel(item.first) +
                                          " | " + std::to_string(item.second) +
                                          " draws";
                if (ShaderOptionSelected(label.c_str(),
                                         state.shader_filter &&
                                             *state.shader_filter ==
                                                 item.first)) {
                    chosen = item.first;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "%s", PortableShaderHash(item.first.hash).c_str());
            }
        }
        ImGui::EndChild();
        if (chosen) {
            state.shader_filter = chosen;
            FilterCaptureEvents(state);
        }
    } else {
        if (ImGui::BeginChild("##capture-events")) {
            ImGuiListClipper clipper;
            clipper.Begin(int(state.visible.size()));
            while (clipper.Step())
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd;
                     ++i) {
                    const auto &event = state.visible[i];
                    std::string label = "E" + std::to_string(event->event_id) +
                                        " " + CaptureEventLabel(event->type);
                    if (ShaderOptionSelected(label.c_str(),
                                             state.selected_event ==
                                                 event->event_id))
                        SelectCaptureEvent(state, event->event_id);
                }
        }
        ImGui::EndChild();
    }
    ImGui::TextWrapped("Filters change the view. Recording continues for all "
                       "shader stages and rendering events.");
}

bool ShaderBrowserWindow::InspectCapturedOccurrence(
    std::shared_ptr<const CaptureOccurrence> occurrence,
    const CaptureSessionContext &context, std::string *error,
    const CaptureSessionSnapshot *recording)
{
    if(error)error->clear();
    if (!occurrence || occurrence->pending) {
        if(error)*error="Owned draw is missing or GPU capture is still pending";
        return false;
    }
    auto &state = CaptureWorkspace();
    if (state.file_job.valid()) {
        if(error)*error="Finish the Shader Browser file operation before opening another draw";
        return false;
    }
    state.snapshot = {};
    state.snapshot.context = context;
    state.snapshot.events = { occurrence };
    state.snapshot.state = CaptureSessionState::Ready;
    if(recording && std::find(recording->events.begin(),recording->events.end(),occurrence)!=recording->events.end())
        state.snapshot=*recording;
    state.archived = true;
    state.selected = std::move(occurrence);
    state.selected_event = state.selected->event_id;
    state.inputs = state.selected->CopyInputs();
    state.difference = {};
    state.message.clear();
    const bool opened=OpenCapturedOccurrence();
    if(!opened && error)*error=state.message;
    return opened;
}

bool ShaderBrowserWindow::OpenCapturedOccurrence()
{
    CaptureWorkspaceState &state = CaptureWorkspace();
    if (!state.selected || state.selected->pending)
        return false;
    const CaptureOccurrence &event = *state.selected;
    const ShaderKey *pixel = nullptr;
    for (size_t i = 0; i < event.summary.shader_count; ++i)
        if (event.summary.shaders[i].stage == Stage::Pixel)
            pixel = &event.summary.shaders[i];
    if (!pixel || state.inputs.sources[2].empty()) {
        state.message = "This event has no captured pixel stage. Its raw "
                        "evidence and other stage sources remain available.";
        return false;
    }
    CanonicalRecipe recipe;
    recipe.key = *pixel;
    recipe.recipe_format_version = 1;
    recipe.scopes = { event.summary.scope };
    for (const auto &blob : state.inputs.blobs)
        if (blob.name == "recipe.stage2")
            recipe.bytes = blob.bytes;
    RecipeInspection inspection;
    std::string error;
    if (!InspectCanonicalRecipe(recipe, &inspection, &error)) {
        state.message = "Captured recipe cannot be previewed: " + error;
        return false;
    }
    m_selected_key = *pixel;
    m_has_selection = true;
    m_selected_recipe = recipe;
    m_recipe_inspection = inspection;
    m_recipe_error.clear();
    m_has_recipe = true;
    m_selected_source = 0;
    m_source_line_offsets.clear();
    m_source_offsets_generation = 0;
    ShaderWorkbenchSession &workbench = WorkbenchSession();
    GetDrawCaptureRequest().Cancel();
    workbench.capture_auto_pause = false;
    workbench.capture_request_id = 0;
    workbench.running_test.reset();
    workbench.inspected_occurrence = true;
    workbench.advanced_lab = false;
    workbench.occurrence_entry = {};
    workbench.occurrence_entry.key = *pixel;
    workbench.occurrence_entry.scopes = recipe.scopes;
    workbench.occurrence_entry.recipe_format_version =
        recipe.recipe_format_version;
    workbench.occurrence_entry.recipe_size = recipe.bytes.size();
    workbench.occurrence_recipe = std::move(recipe);
    workbench.capture_summary = event.summary;
    workbench.capture_inputs = state.inputs;
    auto geometry = event.CopyGeometry();
    workbench.capture_mesh.positions = std::move(geometry.positions);
    workbench.capture_mesh.indices = std::move(geometry.indices);
    workbench.capture_component = -1;
    workbench.use_capture_mesh = !workbench.capture_mesh.indices.empty();
    workbench.capture_difference = state.difference;
    workbench.capture_image_revision = PreviewNowNs();
    workbench.occurrence_view = 0;
    workbench.dock.open = true;
    workbench.generated_cache_valid = false;
    workbench.comparison_locked = false;
    workbench.occurrence_source = {};
    auto &source = workbench.occurrence_source;
    source.key = *pixel;
    source.scope = event.summary.scope;
    source.backend = state.snapshot.context.backend == 2 ?
                         PreviewBackend::Vulkan :
                         PreviewBackend::OpenGL;
    source.stage = HostSourceStage::Fragment;
    source.route = Route::Unknown;
    source.generator_abi = 1;
    source.interface_abi = 1;
    source.text = state.inputs.sources[2];
    source.digest = ComputePreviewDigest(
        reinterpret_cast<const uint8_t *>(source.text.data()),
        source.text.size());
    source.resident = true;
    source.build_scope_verified = true;
    for (const auto &reg : state.inputs.registers) {
        if (reg.name == "capture.generator_abi")
            source.generator_abi = reg.value;
        if (reg.name == "capture.interface_abi")
            source.interface_abi = reg.value;
        if (reg.name == "capture.route" &&
            reg.value <= uint32_t(Route::Disabled))
            source.route = Route(reg.value);
    }
    m_detail_snapshot = {};
    m_detail_snapshot.generation = PreviewNowNs();
    m_detail_snapshot.request.key = *pixel;
    m_detail_snapshot.backend = source.backend == PreviewBackend::Vulkan ?
                                    DetailBackend::Vulkan :
                                    DetailBackend::OpenGL;
    m_detail_snapshot.request.backend = m_detail_snapshot.backend;
    m_detail_snapshot.state = DetailState::Partial;
    m_detail_snapshot.status =
        "Captured occurrence E" + std::to_string(event.event_id) +
        ": exact owned stage sources; runtime lifecycle and complete replay "
        "dependencies are unavailable.";
    for (const size_t stage : { size_t(2), size_t(1), size_t(3) }) {
        if (state.inputs.sources[stage].empty())
            continue;
        HostSource captured;
        captured.backend = m_detail_snapshot.backend;
        captured.stage = stage == 2 ? HostSourceStage::Fragment :
                         stage == 1 ? HostSourceStage::Vertex :
                                      HostSourceStage::Geometry;
        captured.kind = HostSourceKind::Glsl;
        captured.route = source.route;
        captured.exact_runtime_source = true;
        captured.label = "Captured E" + std::to_string(event.event_id) + " " +
                         HostSourceStageLabel(captured.stage);
        captured.text = state.inputs.sources[stage];
        m_detail_snapshot.sources.push_back(std::move(captured));
    }
    workbench.capture_selection = {};
    workbench.capture_selection.shader = *pixel;
    workbench.capture_selection.scope = event.summary.scope;
    workbench.capture_selection.backend = source.backend;
    workbench.capture_selection.mode =
        source.route == Route::Uber ? PreviewMode::Uber : PreviewMode::Normal;
    workbench.capture_selection.session_epoch = event.summary.key.session_epoch;
    workbench.capture_selection.renderer_epoch = event.summary.key.renderer_epoch;
    m_preview_packet_attempted = false;
    m_select_preview_tab = true;
    m_action_message = "Inspecting captured E" +
                       std::to_string(event.event_id) +
                       ". Original camera uses captured vertex processing "
                       "where supported; saving "
                       "does not enable a replacement.";
    return true;
}

void ShaderBrowserWindow::DrawCaptureWorkspace()
{
    CaptureWorkspaceState &state = CaptureWorkspace();
    CaptureSession &session = GetCaptureSession();
    PollCaptureInspectionUi(state.snapshot, state.revision,
                            state.selected_event);
    const bool busy = session.Active() || state.snapshot.pending_events;
    auto start = [&](CaptureSessionMode mode) {
        CaptureSessionContext context;
        context.scope = CurrentWorkbenchBuildScope();
        context.scope_generation = xemu_shader_browser_scope_generation();
        context.session_epoch = m_snapshot.session_epoch;
        context.renderer_epoch = nv2a_profile_preview_renderer_epoch();
        context.generation = PreviewNowNs();
        context.current_frame = m_snapshot.current_frame;
        context.backend =
            g_config.display.renderer == CONFIG_DISPLAY_RENDERER_VULKAN ? 2 : 1;
        if (!context.scope.title_id) {
            state.message = "Load a game before starting a capture.";
            return;
        }
        state.settings.mode = mode;
        state.settings.cpu_byte_budget =
            uint64_t(std::clamp(state.cpu_mib, 16, 1024)) << 20;
        state.settings.disk_byte_budget =
            uint64_t(std::clamp(state.disk_mib, 16, 4096)) << 20;
        state.settings.history_frames =
            uint32_t(std::clamp(state.history, 1, 600));
        state.settings.post_frames = uint32_t(std::clamp(state.post, 0, 600));
        state.settings.event_budget =
            uint32_t(std::clamp(state.event_budget, 32, 1000000));
        state.settings.post_trigger_reserve_percent =
            uint32_t(std::clamp(state.post_reserve_percent, 0, 99));
        GetDrawCaptureRequest().Cancel();
        if (!session.Start(context, state.settings)) {
            state.message =
                "Could not start capture; pending readbacks must finish first.";
            return;
        }
        state.archived = false;
        state.revision = UINT64_MAX;
        state.selected_event = 0;
        state.selected.reset();
        state.inputs = {};
        state.difference = {};
        state.all_frames = true;
        state.shader_filter.reset();
        state.next_poll_ns = 0;
        state.last_frame_progress_ns = PreviewNowNs();
        state.auto_pause = state.pause_on_completion;
        state.message.clear();
        if (runstate_check(RUN_STATE_PAUSED))
            vm_start();
        PollAllShaderCapture();
    };
    ImGui::BeginDisabled(busy || state.file_job.valid());
    if (ImGui::Button("Capture next frame"))
        start(CaptureSessionMode::NextFrame);
    ImGui::SameLine();
    if (ImGui::Button("Arm animation"))
        start(CaptureSessionMode::RollingAnimation);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!session.Active() ||
                         state.snapshot.settings.mode !=
                             CaptureSessionMode::RollingAnimation);
    if (ImGui::Button("Mark problem"))
        session.Mark();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!session.Active());
    if (ImGui::Button("Stop"))
        session.Stop();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Setup"))
        ImGui::OpenPopup("##capture-setup");
    if (ImGui::BeginPopup("##capture-setup")) {
        ImGui::BeginDisabled(busy);
        ImGui::InputInt("History guest frames", &state.history);
        ImGui::InputInt("After mark guest frames", &state.post);
        ImGui::InputInt("CPU budget (MiB)", &state.cpu_mib);
        ImGui::InputInt("Disk budget (MiB)", &state.disk_mib);
        ImGui::InputInt("Event budget", &state.event_budget);
        ImGui::InputInt("Capacity reserved until mark (%)",
                        &state.post_reserve_percent);
        ImGui::TextWrapped(
            "Animation capture holds this share of CPU memory and event "
            "capacity for recording after the mark. It does not guarantee "
            "a number of frames. Zero disables the reserve.");
        ImGui::Checkbox("Pause game on completion", &state.pause_on_completion);
        ImGui::Checkbox("Observe before / after every draw",
                        &state.settings.maximum_evidence);
        ImGui::TextWrapped(
            "Default output checkpoints are bounded. Additional images "
            "increase capture cost. "
            "Vulkan staging is limited to 256 MiB; each draw input is limited "
            "to 64 MiB. "
            "History begins when armed; no earlier payloads can be recovered.");
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    const auto &snapshot = state.snapshot;
    ImGui::Text("All stages | %s%s", CaptureStateLabel(snapshot.state),
                state.archived ? " | Reopened capture" : "");
    if (snapshot.has_frame_range)
        ImGui::TextDisabled("Guest frames %llu–%llu | Mark %llu",
                            (unsigned long long)snapshot.first_frame,
                            (unsigned long long)snapshot.last_frame,
                            (unsigned long long)snapshot.trigger_frame);
    else
        ImGui::TextDisabled(session.Active() ?
                                "Waiting for the next guest-frame boundary" :
                                "No recording loaded");
    if (!snapshot.retained_frames.empty()) {
        const uint64_t span = snapshot.last_frame - snapshot.first_frame;
        const bool gaps =
            span == UINT64_MAX || snapshot.retained_frames.size() != span + 1;
        ImGui::TextDisabled("Evidence retained in %zu guest frames%s; full "
                            "replay dependencies are not established",
                            snapshot.retained_frames.size(),
                            gaps ? " (gaps present)" : "");
    }
    ImGui::TextDisabled(
        "%zu events | %zu events pending | %s + %s reserved / %s",
        snapshot.events.size(), snapshot.pending_events,
        BytesText(snapshot.cpu_bytes).c_str(),
        BytesText(snapshot.reserved_bytes).c_str(),
        BytesText(snapshot.settings.cpu_byte_budget).c_str());
    if (snapshot.state == CaptureSessionState::Recording &&
        snapshot.settings.mode == CaptureSessionMode::RollingAnimation &&
        snapshot.settings.post_frames &&
        snapshot.settings.post_trigger_reserve_percent) {
        const auto limits = CaptureSessionPreTriggerLimits(snapshot.settings);
        ImGui::TextDisabled(
            "Held until mark: %s and %u event slots (%u%% of capacity)",
            BytesText(snapshot.settings.cpu_byte_budget - limits.cpu_bytes)
                .c_str(),
            snapshot.settings.event_budget - limits.events,
            snapshot.settings.post_trigger_reserve_percent);
    }
    if (!snapshot.reason.empty())
        ImGui::TextWrapped("%s", snapshot.reason.c_str());
    ImGui::TextDisabled("Dependency replay is incomplete; recorded input "
                        "snapshots remain inspectable.");
    DrawCaptureFrameNavigation(state);
    if (!state.message.empty())
        ImGui::TextWrapped("%s", state.message.c_str());
    if (ImGui::CollapsingHeader("Save / reopen / extract")) {
        if (state.package_path.empty()) {
            const char *base = xemu_settings_get_base_path();
            if (base)
                state.package_path =
                    (std::filesystem::u8path(base) / "shader-captures" /
                     ("session-" + std::to_string(g_get_real_time())))
                        .u8string();
        }
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##capture-package-path",
                                 "Capture package directory...",
                                 &state.package_path);
        ImGui::BeginDisabled(busy || state.file_job.valid() ||
                             CaptureInspectionBusy() ||
                             state.package_path.empty());
        if (ImGui::Button("Save capture"))
            CaptureFileJob(state, false, snapshot);
        ImGui::SameLine();
        if (ImGui::Button("Reopen capture"))
            CaptureFileJob(state, true);
        ImGui::SameLine();
        ImGui::BeginDisabled(!state.selected);
        if (ImGui::Button("Extract selected event")) {
            CaptureSessionSnapshot subset = snapshot;
            subset.events = { state.selected };
            subset.annotations.clear();
            subset.reason =
                "Selected event extraction; dependency closure is incomplete.";
            CaptureFileJob(state, false, std::move(subset), false);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!state.shader_filter);
        if (ImGui::Button("Extract all shader uses")) {
            CaptureSessionSnapshot subset = snapshot;
            subset.annotations.clear();
            subset.events.clear();
            for (const auto &event : snapshot.events)
                if (EventUses(*event, *state.shader_filter))
                    subset.events.push_back(event);
            subset.reason = "Matching shader inputs; other rendering events "
                            "and dependency closure omitted.";
            CaptureFileJob(state, false, std::move(subset), false);
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        DrawCaptureFileProgress(state);
    }
    const float scale = g_viewport_mgr.m_scale;
    const float available_height = ImGui::GetContentRegionAvail().y;
    const float upper_height =
        std::max(140.0f * scale, available_height * 0.56f);
    const bool inspector_allowed =
        ImGui::GetContentRegionAvail().x > 650.0f * scale;
    if (ImGui::BeginChild("##capture-output-area", ImVec2(0, upper_height))) {
        if (!state.selected) {
            ImGui::TextWrapped(
                "Capture a frame, mark an animation, or reopen a recording. "
                "Every draw occurrence retains its own inputs; selecting a "
                "shader filters its uses.");
        } else {
            const auto &event = *state.selected;
            ImGui::Text("E%llu | F%llu / D%u / emission %llu | %s",
                        (unsigned long long)event.event_id,
                        (unsigned long long)event.summary.key.frame,
                        event.summary.key.draw,
                        (unsigned long long)event.summary.key.submission,
                        CaptureEventLabel(event.type));
            const char *views[] = { "After draw", "Before draw",
                                    "Color difference" };
            ShaderCombo("Output", &state.image_view, views, 3);
            if (inspector_allowed) {
                ImGui::SameLine();
                ImGui::Checkbox("Inspector", &state.inspector);
            } else {
                ImGui::SameLine();
                if (ImGui::Button("Inspector"))
                    ImGui::OpenPopup("Occurrence inspector");
                ImGui::SetNextWindowSize(
                    ImVec2(
                        std::min(600.0f * scale,
                                 ImGui::GetIO().DisplaySize.x - 24.0f * scale),
                        std::min(560.0f * scale,
                                 ImGui::GetIO().DisplaySize.y - 24.0f * scale)),
                    ImGuiCond_Appearing);
                if (ImGui::BeginPopup("Occurrence inspector")) {
                    if (ImGui::Button("Close inspector"))
                        ImGui::CloseCurrentPopup();
                    DrawCaptureEventInspector(state);
                    ImGui::EndPopup();
                }
            }
            const float width = ImGui::GetContentRegionAvail().x;
            const float output_width =
                inspector_allowed && state.inspector ? width * 0.58f : width;
            if (ImGui::BeginChild("##capture-recorded",
                                  ImVec2(output_width, 0))) {
                if (event.pending)
                    ImGui::TextDisabled("Input readback is still pending.");
                else {
                    const OwnedDrawImage &image =
                        state.image_view == 1 ? state.inputs.before :
                        state.image_view == 2 ? state.difference :
                                                state.inputs.after;
                    DrawCapturedImage(
                        image, state.image_revision, state.image_view,
                        ImGui::GetContentRegionAvail().x,
                        std::max(1.0f,
                                 ImGui::GetContentRegionAvail().y -
                                     ImGui::GetTextLineHeightWithSpacing() * 2),
                        false);
                    ImGui::TextWrapped(
                        image.rgba.empty() ?
                            "No observed checkpoint for this event. Its inputs "
                            "are retained; replay cannot yet reconstruct this "
                            "output." :
                            "Observed at the draw render target. Color "
                            "difference excludes depth/stencil and does not "
                            "establish object ownership.");
                }
            }
            ImGui::EndChild();
            if (inspector_allowed && state.inspector) {
                ImGui::SameLine();
                if (ImGui::BeginChild("##capture-event-inspector")) {
                    DrawCaptureEventInspector(state);
                }
                ImGui::EndChild();
            }
        }
    }
    ImGui::EndChild();
    if (ImGui::BeginTabBar("##capture-lower-dock")) {
        if (ImGui::BeginTabItem("Uses / events")) {
            ImGui::TextDisabled(
                "%zu draws shown / %zu recorded draws | %zu events shown / %zu "
                "retained | Arrow keys inspect immediately",
                state.visible_draws, state.recorded_draws, state.visible.size(),
                snapshot.events.size());
            if (ImGui::BeginTable("##capture-occurrences", 7,
                                  ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_ScrollY |
                                      ImGuiTableFlags_BordersInnerV,
                                  ImVec2(0, 0))) {
                ImGui::TableSetupScrollFreeze(0, 1);
                for (const char *name :
                     { "Event", "Frame / draw", "Geometry", "Constants",
                       "Textures", "Stages", "Status" })
                    ImGui::TableSetupColumn(name);
                ImGui::TableHeadersRow();
                bool explicit_move = false;
                auto &context = *ImGui::GetCurrentContext();
                // The virtualized table owns these keys only while focused.
                // Text editing, popups and the guest window retain their keys.
                if (ImGui::IsWindowFocused() && !context.ActiveId &&
                    !ImGui::GetIO().WantTextInput && !ImGui::GetIO().KeyMods &&
                    !ImGui::IsPopupOpen(nullptr,
                                        ImGuiPopupFlags_AnyPopupId |
                                            ImGuiPopupFlags_AnyPopupLevel)) {
                    std::optional<ShaderListMove> move;
                    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
                        move = ShaderListMove::Previous;
                    else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
                        move = ShaderListMove::Next;
                    else if (ImGui::IsKeyPressed(ImGuiKey_Home))
                        move = ShaderListMove::First;
                    else if (ImGui::IsKeyPressed(ImGuiKey_End))
                        move = ShaderListMove::Last;
                    else if (ImGui::IsKeyPressed(ImGuiKey_PageUp))
                        move = ShaderListMove::PagePrevious;
                    else if (ImGui::IsKeyPressed(ImGuiKey_PageDown))
                        move = ShaderListMove::PageNext;
                    if (move && !state.visible.empty()) {
                        const auto current = std::find_if(
                            state.visible.begin(), state.visible.end(),
                            [&](const auto &item) {
                                return item->event_id == state.selected_event;
                            });
                        const auto index =
                            size_t(current - state.visible.begin());
                        const auto rows = size_t(std::max(
                            1.0f, ImGui::GetContentRegionAvail().y /
                                      ImGui::GetTextLineHeightWithSpacing()));
                        const auto next = ShaderListNavigationTarget(
                            state.visible.size(), index, *move, rows);
                        SelectCaptureEvent(state,
                                           state.visible[next]->event_id);
                        state.selection_scroll_pending = true;
                        explicit_move = true;
                        ImGui::NavMoveRequestCancel();
                    }
                }
                ImGuiListClipper clipper;
                clipper.Begin(int(state.visible.size()));
                if (state.selection_scroll_pending) {
                    const auto selected = std::find_if(
                        state.visible.begin(), state.visible.end(),
                        [&](const auto &item) {
                            return item->event_id == state.selected_event;
                        });
                    if (selected != state.visible.end())
                        clipper.IncludeItemByIndex(
                            int(selected - state.visible.begin()));
                }
                while (clipper.Step())
                    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd;
                         ++i) {
                        const auto &event = state.visible[i];
                        ImGui::PushID(std::to_string(event->event_id).c_str());
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        std::string label =
                            "E" + std::to_string(event->event_id) + " " +
                            CaptureEventLabel(event->type);
                        const bool selected =
                            state.selected_event == event->event_id;
                        const bool activated =
                            explicit_move ?
                                ImGui::Selectable(
                                    label.c_str(), selected,
                                    ImGuiSelectableFlags_SpanAllColumns) :
                                ShaderOptionSelected(
                                    label.c_str(), selected,
                                    ImGuiSelectableFlags_SpanAllColumns);
                        if (activated)
                            SelectCaptureEvent(state, event->event_id);
                        if (state.selected_event == event->event_id &&
                            state.selection_scroll_pending) {
                            ImGui::SetScrollHereY(0.5f);
                            if (explicit_move) {
                                auto *window = ImGui::GetCurrentWindow();
                                const auto &rect = context.LastItemData.NavRect;
                                ImGui::SetNavID(
                                    context.LastItemData.ID,
                                    window->DC.NavLayerCurrent,
                                    context.CurrentFocusScopeId,
                                    ImRect(ImVec2(rect.Min.x - window->Pos.x,
                                                  rect.Min.y - window->Pos.y),
                                           ImVec2(rect.Max.x - window->Pos.x,
                                                  rect.Max.y - window->Pos.y)));
                            }
                            state.selection_scroll_pending = false;
                        }
                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text(
                            "%llu / %u",
                            (unsigned long long)event->summary.key.frame,
                            event->summary.key.draw);
                        ImGui::TableSetColumnIndex(2);
                        ImGui::Text("%zu / %zu", event->geometry.position_count,
                                    event->geometry.index_count);
                        ImGui::TableSetColumnIndex(3);
                        ImGui::Text("%zu", event->inputs.uniforms.size());
                        ImGui::TableSetColumnIndex(4);
                        size_t bound = 0;
                        for (const auto &texture : event->inputs.textures)
                            if (texture.metadata.bound)
                                ++bound;
                        ImGui::Text("%zu", bound);
                        ImGui::TableSetColumnIndex(5);
                        ImGui::Text("%u", event->summary.shader_count);
                        ImGui::TableSetColumnIndex(6);
                        ImGui::TextUnformatted(
                            event->pending ? "Pending" :
                            event->batch_id &&
                                    event->submission !=
                                        CaptureBatchOutcome::Submitted ?
                                             "Not submitted" :
                            event->batch_id &&
                                    event->completion ==
                                        CaptureBatchCompletion::Failed ?
                                             "Completion failed" :
                            !event->emitted &&
                                    event->type == CaptureEventType::Draw ?
                                             "Not emitted" :
                            event->limitations ? "Partial" :
                                                 "Ready");
                        ImGui::PopID();
                    }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Source / replacement")) {
            if (state.selected && !state.selected->pending) {
                ShaderCombo(
                    "Bound stage", &state.stage,
                    "Shared\0Vertex\0Pixel\0Host geometry\0Fixed function\0");
                const auto &source = state.inputs.sources[state.stage];
                if (ImGui::Button("Open occurrence preview / editor"))
                    OpenCapturedOccurrence();
                ImGui::TextWrapped(
                    "Captured stage source is read-only. Create replacement in "
                    "the editor; "
                    "saving does not enable it in game. Original camera uses "
                    "captured vertex processing where supported.");
                if (source.empty())
                    ImGui::TextDisabled("No generated source was captured for "
                                        "this bound stage.");
                else
                    ImGui::InputTextMultiline(
                        "##captured-stage-glsl",
                        const_cast<char *>(source.c_str()), source.size() + 1,
                        ImVec2(-1, -1), ImGuiInputTextFlags_ReadOnly);
            } else
                ImGui::TextDisabled(
                    "Select a retired occurrence to inspect its bound source.");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Replacement comparisons")) {
            DrawCaptureComparisons(state.snapshot, state.selected_event);
            ImGui::EndTabItem();
        }
        const auto compare_flags = CaptureInspectionUi().select_compare_tab ?
                                       ImGuiTabItemFlags_SetSelected :
                                       ImGuiTabItemFlags_None;
        if (ImGui::BeginTabItem("Compare inputs", nullptr, compare_flags)) {
            CaptureInspectionUi().select_compare_tab = false;
            DrawCaptureInputComparisons(snapshot, state.revision,
                                        state.selected);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Trends")) {
            const auto event = DrawCaptureInputTrends(snapshot, state.revision,
                                                      state.selected);
            if (event)
                SelectCaptureEvent(state, event);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

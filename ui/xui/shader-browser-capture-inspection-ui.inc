// SPDX-License-Identifier: GPL-2.0-or-later
namespace {
enum class CaptureInspectionUiWork { Source, Action, Page };
struct CaptureInspectionUiResult {
    CaptureInspectionUiWork kind = CaptureInspectionUiWork::Page;
    uint64_t request = 0, recording = 0, revision = 0, selection = 0;
    uint64_t job = 0, track = 0;
    bool loaded = false;
    CaptureInspectionPage page;
    std::vector<CaptureTemporalTrack> tracks;
    std::string message;
};
struct CaptureInspectionUiState {
    CaptureInspectionController controller;
    std::future<CaptureInspectionUiResult> worker;
    uint64_t request = 1, recording = 0, revision = UINT64_MAX;
    uint64_t wanted_recording = 0, wanted_revision = UINT64_MAX, selection = 0;
    uint64_t failed_recording = 0, failed_revision = UINT64_MAX;
    uint64_t job = 0, track = 0, anchor = 0, offset = 0, frame = 0;
    bool changed_only = true, force_open = false, page_requested = false;
    bool select_compare_tab = false;
    std::vector<uint64_t> drop_jobs;
    int shader = 0;
    std::string label = "Tracked subject", pinned_field, message;
    CaptureInspectionPage page;
    std::vector<CaptureTemporalTrack> tracks;
};
CaptureInspectionUiState &CaptureInspectionUi()
{
    static CaptureInspectionUiState state;
    return state;
}
bool CaptureInspectionBusy()
{
    return CaptureInspectionUi().worker.valid();
}
void ResetCaptureInspectionUi()
{
    auto &s = CaptureInspectionUi();
    ++s.request;
    s.force_open = true;
    s.recording = 0;
    s.revision = UINT64_MAX;
    s.job = s.track = s.anchor = s.offset = 0;
    s.failed_recording = 0;
    s.failed_revision = UINT64_MAX;
    s.page = {};
    s.tracks.clear();
    s.message.clear();
}
void PollCaptureInspectionUi(const CaptureSessionSnapshot &capture,
                             uint64_t revision, uint64_t selected_event,
                             bool requested = false)
{
    auto &s = CaptureInspectionUi();
    const auto recording = capture.context.generation;
    if (s.wanted_recording != recording || s.wanted_revision != revision ||
        s.selection != selected_event) {
        if (s.job)
            s.drop_jobs.push_back(s.job);
        ++s.request;
        s.job = 0;
        s.offset = 0;
        s.page = {};
        s.page_requested = false;
        if (s.selection != selected_event)
            s.frame = 0;
        s.wanted_recording = recording;
        s.wanted_revision = revision;
        s.selection = selected_event;
    }
    if (s.worker.valid() && s.worker.wait_for(std::chrono::seconds(0)) ==
                                std::future_status::ready) {
        try {
            auto result = s.worker.get();
            const bool current_source =
                result.recording == recording && result.revision == revision;
            if (current_source) {
                if (result.kind == CaptureInspectionUiWork::Source &&
                    result.loaded) {
                    s.recording = recording;
                    s.revision = revision;
                    s.force_open = false;
                    s.failed_recording = 0;
                    s.failed_revision = UINT64_MAX;
                } else if (result.kind == CaptureInspectionUiWork::Source) {
                    s.failed_recording = recording;
                    s.failed_revision = revision;
                }
                // User-confirmed annotations survive a selection change. Only
                // the comparison page belongs to the old selected occurrence.
                s.tracks = std::move(result.tracks);
                if (result.kind == CaptureInspectionUiWork::Source ||
                    result.request == s.request || result.track)
                    s.message = std::move(result.message);
                if (result.track)
                    s.track = result.track;
                if (!s.track && !s.tracks.empty())
                    s.track = s.tracks.front().track_id;
                if (result.kind != CaptureInspectionUiWork::Source &&
                    result.request == s.request &&
                    result.selection == selected_event) {
                    s.job = result.job;
                    s.page = std::move(result.page);
                } else if (result.job)
                    s.drop_jobs.push_back(result.job);
            } else if (result.job)
                s.drop_jobs.push_back(result.job);
        } catch (const std::exception &e) {
            s.message = e.what();
            s.job = 0;
        }
    }
    if (!s.worker.valid() && !s.drop_jobs.empty()) {
        auto drops = std::move(s.drop_jobs);
        s.drop_jobs.clear();
        const auto request = s.request;
        s.worker =
            std::async(std::launch::async,
                       [&controller = s.controller, drops = std::move(drops),
                        request, recording, revision, selected_event] {
                           for (const auto job : drops) {
                               controller.Cancel(job);
                               controller.Drop(job);
                           }
                           CaptureInspectionUiResult result;
                           result.kind = CaptureInspectionUiWork::Action;
                           result.request = request;
                           result.recording = recording;
                           result.revision = revision;
                           result.selection = selected_event;
                           result.tracks = controller.Tracks();
                           return result;
                       });
    }
    if (s.worker.valid() || !recording || capture.events.empty())
        return;
    if (s.failed_recording == recording && s.failed_revision == revision)
        return;
    const bool needs_source =
        s.recording != recording || s.revision != revision || s.force_open;
    if (!needs_source ||
        (!requested && !s.recording && capture.annotations.empty()))
        return;
    const bool open = s.force_open || s.recording != recording;
    const auto request = s.request;
    s.worker = std::async(std::launch::async, [&controller = s.controller,
                                               capture, revision,
                                               selected_event, request, open] {
        CaptureInspectionUiResult result;
        result.kind = CaptureInspectionUiWork::Source;
        result.request = request;
        result.recording = capture.context.generation;
        result.revision = revision;
        result.selection = selected_event;
        result.loaded = open ? controller.Open(result.recording, capture) :
                               controller.Refresh(result.recording, capture);
        if (!result.loaded)
            result.message = controller.Error();
        else if (open && !capture.annotations.empty() &&
                 !controller.RestoreAnnotations(capture.annotations,
                                                &result.message))
            result.message =
                "Capture opened; temporal annotations could not be restored: " +
                result.message;
        result.tracks = controller.Tracks();
        return result;
    });
}
using CaptureInspectionUiAction = std::function<void(
    CaptureInspectionController &, CaptureInspectionUiResult &)>;
void StartCaptureInspectionAction(const CaptureInspectionUiAction &action)
{
    auto &s = CaptureInspectionUi();
    if (s.worker.valid())
        return;
    const auto old_job = s.job;
    const auto request = ++s.request;
    const auto recording = s.recording, revision = s.revision,
               selection = s.selection;
    s.job = 0;
    s.offset = 0;
    s.page = {};
    s.page_requested = false;
    s.worker = std::async(std::launch::async, [&controller = s.controller,
                                               action, old_job, request,
                                               recording, revision, selection] {
        CaptureInspectionUiResult result;
        result.kind = CaptureInspectionUiWork::Action;
        result.request = request;
        result.recording = recording;
        result.revision = revision;
        result.selection = selection;
        if (old_job) {
            controller.Cancel(old_job);
            controller.Drop(old_job);
        }
        action(controller, result);
        if (result.job) {
            controller.Run(result.job, 1);
            result.page = controller.Page(result.job);
        } else if (result.message.empty() && !result.track)
            result.message = controller.Error();
        result.tracks = controller.Tracks();
        return result;
    });
}
void PumpCaptureInspectionPage()
{
    auto &s = CaptureInspectionUi();
    if (s.worker.valid() || !s.job ||
        (s.page.state != CaptureInspectionJobState::Running &&
         !s.page_requested))
        return;
    const auto request = s.request, recording = s.recording,
               revision = s.revision, selection = s.selection;
    const auto job = s.job, offset = s.offset;
    s.page_requested = false;
    s.worker = std::async(std::launch::async, [&controller = s.controller,
                                               request, recording, revision,
                                               selection, job, offset] {
        CaptureInspectionUiResult result;
        result.request = request;
        result.recording = recording;
        result.revision = revision;
        result.selection = selection;
        result.job = job;
        controller.Run(job, controller.Page(job, 0, 0).kind ==
                                    CaptureInspectionJobKind::Difference ?
                                32 :
                                1);
        result.page = controller.Page(job, offset, 128);
        result.tracks = controller.Tracks();
        return result;
    });
}
const char *CaptureTrackStateLabel(CaptureTrackFrameState state)
{
    switch (state) {
    case CaptureTrackFrameState::Confirmed:
        return "Confirmed";
    case CaptureTrackFrameState::Candidate:
        return "Candidate";
    case CaptureTrackFrameState::Ambiguous:
        return "Ambiguous";
    case CaptureTrackFrameState::Missing:
        return "No candidate";
    case CaptureTrackFrameState::Incomplete:
        return "Unresolved";
    }
    return "Unknown";
}
std::string CaptureRawValueText(const CaptureInspectionValue &value)
{
    if (!value.present)
        return "Not captured / unbound";
    const auto &bytes = value.block ? value.block->bytes : value.bits;
    const char *hex = "0123456789ABCDEF";
    std::string text;
    if (value.block)
        text = "B" + std::to_string(value.block->id) + " | " +
               std::to_string(bytes.size()) + " bytes | ";
    else
        text = "0x";
    const auto size = std::min<size_t>(bytes.size(), 16);
    for (size_t i = 0; i < size; ++i) {
        const auto byte = value.block ? bytes[i] : bytes[bytes.size() - 1 - i];
        text += hex[byte >> 4];
        text += hex[byte & 15];
        if (value.block && i + 1 < size)
            text += ' ';
    }
    if (size != bytes.size())
        text += " ...";
    return text;
}
void DrawCaptureInspectionPaging()
{
    auto &s = CaptureInspectionUi();
    const auto &page = s.page;
    if (!s.job)
        return;
    ImGui::TextDisabled("%llu / %llu fields or events examined | %llu results "
                        "| %llu bytes compared",
                        (unsigned long long)page.processed,
                        (unsigned long long)page.total,
                        (unsigned long long)page.result_count,
                        (unsigned long long)page.compared_bytes);
    if (!page.message.empty())
        ImGui::TextWrapped("%s", page.message.c_str());
    if (page.omitted_results)
        ImGui::TextDisabled("%llu results omitted by the declared budget",
                            (unsigned long long)page.omitted_results);
    ImGui::BeginDisabled(s.worker.valid() || !s.offset);
    if (ImGui::Button("Previous fields")) {
        s.offset = s.offset >= 128 ? s.offset - 128 : 0;
        s.page_requested = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(s.worker.valid() || !page.has_more);
    if (ImGui::Button("Next fields")) {
        s.offset += 128;
        s.page_requested = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel analysis")) {
        if (s.job)
            s.drop_jobs.push_back(s.job);
        ++s.request;
        s.job = 0;
        s.page = {};
        s.page_requested = false;
    }
}
bool CaptureInspectionReady()
{
    auto &s = CaptureInspectionUi();
    return !s.worker.valid() && s.recording &&
           s.recording == s.wanted_recording && s.revision == s.wanted_revision;
}
uint64_t DrawCaptureInputComparisons(
    const CaptureSessionSnapshot &capture, uint64_t revision,
    const std::shared_ptr<const CaptureOccurrence> &selected)
{
    auto &s = CaptureInspectionUi();
    const uint64_t event = selected ? selected->event_id : 0;
    PollCaptureInspectionUi(capture, revision, event, true);
    ImGui::TextWrapped("Compare retained input bits between draw occurrences. "
                       "Previous shader use is a use-history comparison and "
                       "does not identify a tracked subject.");
    const bool usable = CaptureInspectionReady() && selected &&
                        selected->type == CaptureEventType::Draw;
    ImGui::BeginDisabled(!usable);
    if (ImGui::Button("Set selected draw as compare anchor"))
        s.anchor = event;
    ImGui::SameLine();
    ImGui::TextDisabled("Anchor E%llu | Selected E%llu",
                        (unsigned long long)s.anchor,
                        (unsigned long long)event);
    ImGui::Checkbox("Changed fields only", &s.changed_only);
    ImGui::BeginDisabled(!s.anchor);
    if (ImGui::Button("Compare anchor in this frame")) {
        const auto anchor = s.anchor;
        const bool changed = s.changed_only;
        StartCaptureInspectionAction(
            [anchor, event, changed](auto &c, auto &r) {
                r.job = c.BeginWithinFrameComparison(anchor, event, changed);
            });
    }
    ImGui::EndDisabled();
    if (selected && selected->summary.shader_count) {
        std::vector<std::string> labels;
        std::vector<const char *> pointers;
        const size_t count = std::min<size_t>(selected->summary.shader_count,
                                              selected->summary.shaders.size());
        for (size_t i = 0; i < count; ++i) {
            const auto &key = selected->summary.shaders[i];
            labels.push_back(std::string(StageLabel(key.stage)) + " " +
                             ShaderHashBase32(key.hash).substr(0, 8));
        }
        for (const auto &label : labels)
            pointers.push_back(label.c_str());
        s.shader = std::clamp(s.shader, 0, int(count) - 1);
        ShaderCombo("Shader use history", &s.shader, pointers.data(),
                    int(count));
        if (ImGui::Button("Compare previous use of this shader")) {
            const auto shader = selected->summary.shaders[s.shader];
            const bool changed = s.changed_only;
            StartCaptureInspectionAction([event, shader, changed](auto &c,
                                                                  auto &r) {
                r.job = c.BeginPriorShaderUseComparison(event, shader, changed);
            });
        }
    }
    ImGui::EndDisabled();
    if (s.worker.valid())
        ImGui::TextDisabled("Input analysis worker is running...");
    if (!s.message.empty())
        ImGui::TextWrapped("%s", s.message.c_str());
    if (s.page.kind == CaptureInspectionJobKind::Difference && s.job) {
        ImGui::Text(
            "%s | E%llu / F%llu -> E%llu / F%llu",
            s.page.relation == CaptureInspectionRelation::PriorShaderUse ?
                "Previous shader use" :
            s.page.relation == CaptureInspectionRelation::ConfirmedTrack ?
                "Confirmed subject selections" :
                "Within-frame inputs",
            (unsigned long long)s.page.before.event_id,
            (unsigned long long)s.page.before.frame,
            (unsigned long long)s.page.after.event_id,
            (unsigned long long)s.page.after.frame);
        DrawCaptureInspectionPaging();
        if (ImGui::BeginTable("##capture-input-diff", 5,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_BordersInnerV,
                              ImVec2(0, 0))) {
            for (const char *name : { "Input field", "Before raw bits",
                                      "After raw bits", "Change", "Trend" })
                ImGui::TableSetupColumn(name);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();
            for (const auto &field : s.page.fields) {
                ImGui::PushID(field.path.c_str());
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(field.path.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(
                    CaptureRawValueText(field.before).c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(
                    CaptureRawValueText(field.after).c_str());
                ImGui::TableSetColumnIndex(3);
                const char *change =
                    field.change == CaptureInspectionChange::Added ?
                        "Added" :
                    field.change == CaptureInspectionChange::Removed ?
                        "Removed" :
                    field.change == CaptureInspectionChange::Changed ?
                        "Changed" :
                    field.change == CaptureInspectionChange::Uncompared ?
                        "Uncompared" :
                        "Same bits";
                ImGui::TextUnformatted(change);
                ImGui::TableSetColumnIndex(4);
                if (ImGui::SmallButton("Pin raw field"))
                    s.pinned_field = field.path;
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    PumpCaptureInspectionPage();
    return 0;
}
uint64_t
DrawCaptureInputTrends(const CaptureSessionSnapshot &capture, uint64_t revision,
                       const std::shared_ptr<const CaptureOccurrence> &selected)
{
    auto &s = CaptureInspectionUi();
    const uint64_t event = selected ? selected->event_id : 0;
    PollCaptureInspectionUi(capture, revision, event, true);
    uint64_t select = 0;
    ImGui::TextWrapped("A track records your confirmed occurrence choices. "
                       "Input matches are candidates; identical inputs remain "
                       "ambiguous. Evicted evidence stays unresolved.");
    const bool usable = CaptureInspectionReady() && selected &&
                        selected->type == CaptureEventType::Draw;
    if (!s.tracks.empty()) {
        std::vector<std::string> labels;
        std::vector<const char *> pointers;
        int index = 0;
        for (size_t i = 0; i < s.tracks.size(); ++i) {
            const auto &track = s.tracks[i];
            if (track.track_id == s.track)
                index = int(i);
            labels.push_back("T" + std::to_string(track.track_id) + " | " +
                             track.label);
        }
        for (const auto &label : labels)
            pointers.push_back(label.c_str());
        if (ShaderCombo("Confirmed track", &index, pointers.data(),
                        int(pointers.size())))
            s.track = s.tracks[index].track_id;
    }
    const bool choices_open = ImGui::CollapsingHeader(
        "Track choices and candidates", s.tracks.empty() ?
                                            ImGuiTreeNodeFlags_DefaultOpen :
                                            ImGuiTreeNodeFlags_None);
    const auto track = s.track;
    if (choices_open) {
        ImGui::InputText("Subject label", &s.label);
        ImGui::BeginDisabled(!usable);
        if (ImGui::Button("Create track: confirm selected occurrence")) {
            const auto label = s.label;
            StartCaptureInspectionAction([event, label](auto &c, auto &r) {
                r.track = c.CreateTrack(event, label, true);
            });
        }
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!usable || !track);
        if (ImGui::Button("Confirm selected occurrence for this frame"))
            StartCaptureInspectionAction([track, event](auto &c, auto &r) {
                if (!c.Confirm(track, event, true))
                    r.message = c.Error();
                else
                    r.message = "Confirmed frame selection.";
            });
        ImGui::SameLine();
        if (ImGui::Button("Create explicit branch at selected occurrence")) {
            const auto label = s.label;
            StartCaptureInspectionAction(
                [track, event, label](auto &c, auto &r) {
                    r.track = c.Branch(track, event, label, true);
                });
        }
        if (selected && !s.frame)
            s.frame = selected->summary.key.frame;
        ImGui::InputScalar("Candidate guest frame", ImGuiDataType_U64,
                           &s.frame);
        if (ImGui::Button("Find input correspondence candidates")) {
            const auto frame = s.frame;
            StartCaptureInspectionAction([track, frame](auto &c, auto &r) {
                r.job = c.BeginCandidates(track, frame);
            });
        }
        ImGui::SameLine();
        if (ImGui::Button("Compare prior confirmed subject selection")) {
            const auto frame = selected->summary.key.frame;
            const bool changed = s.changed_only;
            StartCaptureInspectionAction([track, frame, changed](auto &c,
                                                                 auto &r) {
                r.job = c.BeginConfirmedTrackComparison(track, frame, changed);
            });
            s.select_compare_tab = true;
        }
        ImGui::EndDisabled();
    }
    if (ImGui::CollapsingHeader("Raw field trend")) {
        ImGui::InputText("Pinned raw field", &s.pinned_field);
        ImGui::BeginDisabled(!CaptureInspectionReady() || !track ||
                             s.pinned_field.empty());
        if (ImGui::Button("Run confirmed-frame raw trend")) {
            const auto field = s.pinned_field;
            StartCaptureInspectionAction([track, field](auto &c, auto &r) {
                r.job = c.BeginTrend(track, field);
            });
        }
        ImGui::EndDisabled();
    }
    if (!s.message.empty())
        ImGui::TextWrapped("%s", s.message.c_str());
    if (s.worker.valid())
        ImGui::TextDisabled("Temporal analysis worker is running...");
    DrawCaptureInspectionPaging();
    if (s.job && s.page.kind == CaptureInspectionJobKind::Trend) {
        if (ImGui::BeginTable("##capture-trend", 5,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                              ImVec2(0, 0))) {
            for (const char *name :
                 { "Frame", "Selection", "State", "Raw field bits", "Change" })
                ImGui::TableSetupColumn(name);
            ImGui::TableHeadersRow();
            for (const auto &point : s.page.trend) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%llu", (unsigned long long)point.frame);
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("E%llu", (unsigned long long)point.event.event_id);
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(CaptureTrackStateLabel(point.state));
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(
                    CaptureRawValueText(point.value).c_str());
                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(
                    !point.comparable_to_previous ? "Gap / first sample" :
                    point.changed_from_previous   ? "Changed bits" :
                                                    "Same bits");
            }
            ImGui::EndTable();
        }
    } else {
        auto found = std::find_if(
            s.tracks.begin(), s.tracks.end(),
            [track](const auto &t) { return t.track_id == track; });
        if (found != s.tracks.end() &&
            ImGui::BeginTable("##capture-track-map", 4,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                              ImVec2(0, 0))) {
            for (const char *name : { "Frame", "Correspondence",
                                      "Confirmed / candidates", "Evidence" })
                ImGui::TableSetupColumn(name);
            ImGui::TableHeadersRow();
            ImGuiListClipper clipper;
            clipper.Begin(int(found->frames.size()));
            while (clipper.Step())
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd;
                     ++i) {
                    const auto &frame = found->frames[i];
                    ImGui::PushID(std::to_string(frame.frame).c_str());
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%llu", (unsigned long long)frame.frame);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(CaptureTrackStateLabel(frame.state));
                    ImGui::TableSetColumnIndex(2);
                    if (frame.state == CaptureTrackFrameState::Confirmed) {
                        ImGui::BeginDisabled(!frame.evidence_retained);
                        const auto label =
                            "E" + std::to_string(frame.selected.event_id);
                        if (ImGui::Selectable(label.c_str(),
                                              event == frame.selected.event_id))
                            select = frame.selected.event_id;
                        ImGui::EndDisabled();
                    } else
                        for (const auto &candidate : frame.candidates) {
                            const auto label =
                                "E" + std::to_string(candidate.event.event_id) +
                                " (candidate)";
                            ImGui::BeginDisabled(!candidate.evidence_retained);
                            if (ImGui::Selectable(label.c_str(),
                                                  event ==
                                                      candidate.event.event_id))
                                select = candidate.event.event_id;
                            ImGui::EndDisabled();
                        }
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextUnformatted(
                        frame.state == CaptureTrackFrameState::Missing ?
                            "No selection" :
                        frame.evidence_retained ? "Retained" :
                                                  "Evicted / unavailable");
                    if (!frame.note.empty() && ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", frame.note.c_str());
                    ImGui::PopID();
                }
            ImGui::EndTable();
        }
    }
    PumpCaptureInspectionPage();
    return select;
}
bool PrepareCaptureInspectionAnnotations(
    CaptureSessionSnapshot &snapshot,
    const std::vector<CaptureTemporalTrack> &tracks, uint64_t owner,
    std::string *error)
{
    CaptureInspectionController controller;
    if (!snapshot.context.generation) {
        if (error)
            *error = "Capture has no persistent recording identity.";
        return false;
    }
    if (!controller.Open(snapshot.context.generation, snapshot)) {
        if (error)
            *error = controller.Error();
        return false;
    }
    if (owner == snapshot.context.generation) {
        auto annotations = tracks;
        for (auto &track : annotations)
            for (auto &frame : track.frames) {
                if (frame.state == CaptureTrackFrameState::Confirmed)
                    frame.evidence_retained =
                        controller.Reference(frame.selected.event_id) ==
                        frame.selected;
                for (auto &candidate : frame.candidates)
                    candidate.evidence_retained =
                        controller.Reference(candidate.event.event_id) ==
                        candidate.event;
                if (frame.state != CaptureTrackFrameState::Confirmed &&
                    !frame.candidates.empty())
                    frame.evidence_retained = std::all_of(
                        frame.candidates.begin(), frame.candidates.end(),
                        [](const auto &candidate) {
                            return candidate.evidence_retained;
                        });
            }
        if (!controller.RestoreTracks(annotations)) {
            if (error)
                *error = controller.Error();
            return false;
        }
    } else if (!snapshot.annotations.empty() &&
               !controller.RestoreAnnotations(snapshot.annotations, error))
        return false;
    return controller.ExportAnnotations(&snapshot.annotations, error);
}
} // namespace

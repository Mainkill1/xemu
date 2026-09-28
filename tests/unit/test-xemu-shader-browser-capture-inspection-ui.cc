// SPDX-License-Identifier: GPL-2.0-or-later
#include "../../ui/xui/shader-browser-capture-inspection.hh"
#include "../../ui/xui/shader-browser-selection-ui.hh"
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <functional>
#include <future>
#include <iostream>
#include <thread>

using namespace xemu::shader_browser;
#include "../../ui/xui/shader-browser-capture-inspection-ui.inc"
static CaptureSessionSnapshot Recording()
{
    CaptureSessionSnapshot s;
    s.context.scope.title_id = 17;
    s.context.generation = 44;
    s.context.session_epoch = 2;
    s.context.renderer_epoch = 3;
    s.context.backend = 1;
    for (uint64_t id = 1; id <= 2; ++id) {
        auto e = std::make_shared<CaptureOccurrence>();
        e->event_id = id;
        e->summary.scope = s.context.scope;
        e->summary.key = { 2, 3, 1, uint32_t(id), id };
        e->summary.shader_count = 1;
        e->summary.shaders[0].stage = Stage::Pixel;
        e->finished = e->emitted = e->inputs.complete = true;
        e->pending = false;
        e->inputs.registers.push_back({ "pgraph.raw", uint32_t(id) });
        s.events.push_back(e);
    }
    return s;
}
static void Drain(const CaptureSessionSnapshot &snapshot, uint64_t revision,
                  uint64_t event)
{
    for (size_t i = 0; i < 10000; ++i) {
        PollCaptureInspectionUi(snapshot, revision, event, true);
        auto &s = CaptureInspectionUi();
        if (!s.worker.valid() && s.recording == snapshot.context.generation &&
            s.revision == revision && s.drop_jobs.empty())
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(false);
}
static void Frame(const CaptureSessionSnapshot &snapshot, uint64_t revision,
                  const std::shared_ptr<const CaptureOccurrence> &event,
                  bool trends)
{
    ImGui::NewFrame();
    ImGui::SetNextWindowSize(ImVec2(1100, 700), ImGuiCond_Always);
    ImGui::Begin("Inspection");
    if (trends)
        DrawCaptureInputTrends(snapshot, revision, event);
    else
        DrawCaptureInputComparisons(snapshot, revision, event);
    ImGui::End();
    ImGui::Render();
}
int main()
{
    auto *context = ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1200, 800);
    io.DeltaTime = 1.0f / 60;
    unsigned char *pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    auto snapshot = Recording();
    ResetCaptureInspectionUi();
    Drain(snapshot, 1, 1);
    assert(!CaptureInspectionBusy());
    StartCaptureInspectionAction(
        [](auto &c, auto &r) { r.job = c.BeginWithinFrameComparison(1, 2); });
    // Selection changes while the worker owns the old request. No old page may
    // become the new event's inputs, and cancelled jobs must release admission.
    PollCaptureInspectionUi(snapshot, 1, 2, true);
    Drain(snapshot, 1, 2);
    assert(CaptureInspectionUi().job == 0 &&
           CaptureInspectionUi().page.fields.empty());
    for (size_t i = 0; i < 20; ++i) {
        StartCaptureInspectionAction([](auto &c, auto &r) {
            r.job = c.BeginWithinFrameComparison(1, 2);
        });
        PollCaptureInspectionUi(snapshot, 1, i % 2 ? 2 : 1, true);
        Drain(snapshot, 1, i % 2 ? 2 : 1);
    }
    StartCaptureInspectionAction([](auto &c, auto &r) {
        r.track = c.CreateTrack(1, "Manual subject", true);
    });
    Drain(snapshot, 1, 1);
    assert(CaptureInspectionUi().tracks.size() == 1);
    auto reduced = snapshot;
    reduced.events.erase(reduced.events.begin());
    Drain(reduced, 2, 2);
    assert(CaptureInspectionUi().tracks.size() == 1);
    assert(!CaptureInspectionUi().tracks[0].frames[0].evidence_retained);
    std::string error;
    assert(PrepareCaptureInspectionAnnotations(
        reduced, CaptureInspectionUi().tracks, 44, &error));
    assert(!reduced.annotations.empty());
    CaptureInspectionController reopened;
    assert(reopened.Open(44, reduced));
    assert(reopened.RestoreAnnotations(reduced.annotations, &error));
    assert(!reopened.Tracks()[0].frames[0].evidence_retained);
    Frame(reduced, 2, reduced.events[0], false);
    Frame(reduced, 2, reduced.events[0], true);
    Drain(reduced, 2, 2);
    ImGui::DestroyContext(context);
    std::cout << "capture inspection UI tests passed\n";
}

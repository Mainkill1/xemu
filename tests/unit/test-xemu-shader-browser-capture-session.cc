// SPDX-License-Identifier: GPL-2.0-or-later
#include "../../ui/xui/shader-browser-capture-session.hh"

#include <cassert>
#include <cstring>
#include <iostream>
#include <fstream>
#include <nlohmann/json.hpp>

using namespace xemu::shader_browser;

static CaptureSessionContext Context()
{
    CaptureSessionContext context;
    context.scope.title_id = 17;
    context.scope_generation = 1;
    context.session_epoch = 2;
    context.renderer_epoch = 3;
    context.generation = 4;
    return context;
}
static DrawCaptureSummary Draw(uint64_t frame, uint32_t draw)
{
    DrawCaptureSummary summary;
    summary.key = { 2, 3, frame, draw, draw + 1 };
    summary.scope = Context().scope;
    summary.shader_count = 2;
    summary.shaders[0].hash.version = summary.shaders[1].hash.version = 1;
    summary.shaders[0].stage = Stage::Vertex;
    summary.shaders[1].stage = Stage::Pixel;
    summary.shaders[1].hash.bytes[0] = 0x71;
    return summary;
}
static void test_archive_descriptor_memory_budget()
{
    CaptureSessionSnapshot snapshot;
    snapshot.context = Context();
    snapshot.state = CaptureSessionState::Ready;
    snapshot.settings.cpu_byte_budget = 64U * 1024U;
    auto event = std::make_shared<CaptureOccurrence>();
    event->event_id = 1;
    event->summary = Draw(1, 1);
    event->finished = true;
    event->pending = false;
    event->inputs.complete = true;
    for (size_t i = 0; i < 4096; ++i)
        event->inputs.registers.push_back({ "x", 0 });
    assert(event->inputs.registers.capacity() * sizeof(OwnedDrawRegister) >
           snapshot.settings.cpu_byte_budget);
    snapshot.events.push_back(event);
    snapshot.total_events = 1;
    const auto path = std::filesystem::temp_directory_path() /
                      "xemu-capture-descriptor-budget-test";
    std::filesystem::remove_all(path);
    std::string error;
    assert(CaptureSession::SaveSnapshot(snapshot, path, &error));
    CaptureSessionSnapshot reopened;
    assert(!CaptureSession::Reopen(path, &reopened, &error));
    assert(reopened.events.empty() && !error.empty());
    std::filesystem::remove_all(path);
    snapshot.settings.cpu_byte_budget = 4U * 1024U * 1024U;
    assert(CaptureSession::SaveSnapshot(snapshot, path, &error));
    assert(CaptureSession::Reopen(path, &reopened, &error));
    assert(reopened.events[0]->inputs.registers.size() == 4096);
    assert(reopened.cpu_bytes >=
           reopened.events[0]->inputs.registers.capacity() *
               sizeof(OwnedDrawRegister));
    nlohmann::json metadata;
    std::ifstream(path / "metadata.json") >> metadata;
    nlohmann::json nested = 0;
    for (size_t i = 0; i < 70; ++i)
        nested = nlohmann::json::array({ std::move(nested) });
    metadata["ignored"] = std::move(nested);
    std::ofstream(path / "metadata.json", std::ios::trunc) << metadata.dump();
    assert(!CaptureSession::Reopen(path, &reopened, &error));
    assert(reopened.events.empty() && !error.empty());
    std::filesystem::remove_all(path);
}
static void test_atomic_claim_rejects_rearmed_context()
{
    CaptureSession session;
    CaptureSessionSettings settings;
    settings.mode = CaptureSessionMode::RollingAnimation;
    assert(session.Start(Context(), settings));
    uint64_t old_generation = 0;
    const auto old = session.Context(&old_generation);
    assert(session.Start(Context(), settings));
    uint64_t fresh_generation = 0;
    session.Context(&fresh_generation);
    assert(old_generation && fresh_generation != old_generation);
    assert(!session.BeginOccurrence(Draw(1, 1), CaptureEventType::Draw,
                                    CaptureMissingDependencies, old_generation,
                                    old.scope_generation));
    session.GuestFrameBoundary(1000, old_generation);
    session.Invalidate(0, 0, 0, old_generation);
    session.AbortAllocationFailure(old_generation);
    assert(session.Active() &&
           session.Snapshot().state == CaptureSessionState::Recording);
    assert(session.Snapshot().events.empty());
    const auto current = session.BeginOccurrence(
        Draw(1, 2), CaptureEventType::Draw, CaptureMissingDependencies,
        fresh_generation, old.scope_generation);
    assert(current);
    session.AbortAllocationFailure(0, current - 1);
    assert(session.Active());
    assert(!session.BeginOccurrence(
        Draw(1, 3), CaptureEventType::Draw, CaptureMissingDependencies,
        fresh_generation, old.scope_generation + 1));
    assert(!session.Active() &&
           session.Snapshot().state == CaptureSessionState::Cancelled);
    assert(session.Start(Context(), settings));
    session.AbortAllocationFailure(0, current);
    assert(session.Active());
    uint64_t latest_generation = 0;
    session.Context(&latest_generation);
    session.GuestFrameBoundary(5, latest_generation);
    assert(!session.Snapshot().events.empty());
    session.Invalidate(0, 0, 0, latest_generation);
    assert(!session.Active());
}
static void test_many_uses_preserve_owned_versions()
{
    CaptureSession session;
    assert(session.Start(Context()));
    assert(session.BeginOccurrence(Draw(0, 0)) == 0);
    session.GuestFrameBoundary(1);
    std::vector<uint64_t> tokens;
    for (uint32_t i = 0; i < 32; ++i) {
        uint64_t token = session.BeginOccurrence(Draw(1, i));
        assert(token & kCaptureSessionTokenBit);
        tokens.push_back(token);
        OwnedDrawGeometry geometry;
        geometry.positions = { { { float(i), 0, 0, 1 } },
                               { { 0, 1, 0, 1 } },
                               { { 0, 0, 1, 1 } } };
        geometry.indices = { 0, 1, 2 };
        assert(session.StageGeometry(token, geometry));
        uint32_t constant = i;
        XemuShaderDrawUniform uniform{
            1,         "constant",      XEMU_SHADER_DRAW_UNIFORM_UINT, 1, 1,
            &constant, sizeof(constant)
        };
        assert(session.StageUniform(token, uniform));
        uint32_t unchanged = 123;
        uniform.name = "unchanged";
        uniform.data = &unchanged;
        assert(session.StageUniform(token, uniform));
        uint8_t rgba[4] = { uint8_t(i), 2, 3, 4 };
        XemuShaderDrawTexture texture{};
        texture.slot = 0;
        texture.bound = 1;
        texture.width = texture.height = texture.depth = 1;
        texture.mip_levels = texture.face_count = 1;
        uint32_t scale_bits = i % 2 ? 0x80000000 : 0x7fc12345;
        std::memcpy(&texture.coordinate_scale, &scale_bits, sizeof(scale_bits));
        texture.image = { 1, 1, rgba, sizeof(rgba) };
        assert(session.StageTexture(token, texture));
        assert(session.StageSource(token, 1, "vertex", 6));
        assert(session.StageSource(token, 2, "pixel", 5));
        assert(session.Finish(token, true, 5, 3, 3));
    }
    session.GuestFrameBoundary(2);
    assert(session.Snapshot().state == CaptureSessionState::Finalizing);
    for (uint64_t token : tokens)
        assert(session.InputsComplete(token));
    auto snapshot = session.Snapshot();
    assert(snapshot.state == CaptureSessionState::Ready);
    assert(snapshot.pending_events == 0);
    assert(snapshot.events[0]->type == CaptureEventType::FrameBoundary);
    const auto uses = session.Uses(Draw(1, 0).shaders[1]);
    assert(uses.size() == 32);
    auto first = session.Find(uses[0]);
    for (uint32_t i = 0; i < 32; ++i) {
        auto event = session.Find(uses[i]);
        assert(event && event->event_id == uses[i]);
        assert(event->CopyGeometry().positions[0][0] == float(i));
        auto inputs = event->CopyInputs();
        uint32_t constant = 0;
        std::memcpy(&constant, inputs.uniforms[0].data.data(), 4);
        assert(constant == i);
        assert(inputs.textures[0].images[0].image.rgba[0] == i);
        assert(event->inputs.sources[1] == first->inputs.sources[1]);
        assert(event->inputs.sources[2] == first->inputs.sources[2]);
        assert(event->inputs.uniforms[1].data ==
               first->inputs.uniforms[1].data);
        assert(event->geometry.indices == first->geometry.indices);
    }
    auto directory =
        std::filesystem::temp_directory_path() / "xemu-capture-session-test";
    std::filesystem::remove_all(directory);
    std::string error;
    assert(session.Save(directory, &error));
    CaptureSessionSnapshot reopened;
    assert(CaptureSession::Reopen(directory, &reopened, &error));
    assert(reopened.events.size() == snapshot.events.size());
    for (size_t i = 0; i < reopened.events.size(); ++i) {
        const auto &a = snapshot.events[i];
        const auto &b = reopened.events[i];
        assert(a->event_id == b->event_id);
        assert(a->summary.key == b->summary.key);
        if (a->type != CaptureEventType::Draw)
            continue;
        assert(a->CopyGeometry().positions == b->CopyGeometry().positions);
        assert(a->CopyInputs().uniforms[0].data ==
               b->CopyInputs().uniforms[0].data);
        assert(a->CopyInputs().textures[0].images[0].image.rgba ==
               b->CopyInputs().textures[0].images[0].image.rgba);
        assert(!std::memcmp(&a->inputs.textures[0].metadata.coordinate_scale,
                            &b->inputs.textures[0].metadata.coordinate_scale,
                            sizeof(float)));
    }
    std::filesystem::remove_all(directory);
}
static uint64_t CompleteDraw(CaptureSession &session, uint64_t frame,
                             uint32_t draw)
{
    auto token = session.BeginOccurrence(Draw(frame, draw));
    assert(token);
    assert(session.Finish(token, true, 5, 3, 0));
    assert(session.InputsComplete(token));
    return token;
}
static void test_rolling_range_waits_for_pending_payload()
{
    CaptureSession session;
    CaptureSessionSettings settings;
    settings.mode = CaptureSessionMode::RollingAnimation;
    settings.history_frames = 2;
    settings.post_frames = 1;
    assert(session.Start(Context(), settings));
    session.GuestFrameBoundary(1);
    CompleteDraw(session, 1, 1);
    session.GuestFrameBoundary(2);
    auto pending = session.BeginOccurrence(Draw(2, 2));
    assert(session.Finish(pending, true, 5, 3, 0));
    auto pending_snapshot = session.Find(pending & ~kCaptureSessionTokenBit);
    assert(pending_snapshot->pending && !pending_snapshot->inputs.complete);
    session.GuestFrameBoundary(3);
    CompleteDraw(session, 3, 3);
    assert(session.Mark());
    session.GuestFrameBoundary(4);
    CompleteDraw(session, 4, 4);
    session.GuestFrameBoundary(5);
    assert(session.Snapshot().state == CaptureSessionState::Finalizing);
    assert(session.InputsComplete(pending));
    auto snapshot = session.Snapshot();
    assert(snapshot.state == CaptureSessionState::Ready);
    assert(snapshot.first_frame == 2 && snapshot.last_frame == 4);
    assert(snapshot.trigger_frame == 3);
    assert(session.Uses(Draw(2, 0).shaders[1]).size() == 3);
    assert(pending_snapshot->pending); // earlier snapshots are immutable
    assert(snapshot.settings.history_frames == 2 &&
           snapshot.settings.post_frames == 1);
}
static void test_raw_malformed_and_suppressed_evidence_survives()
{
    CaptureSession session;
    assert(session.Start(Context()));
    session.GuestFrameBoundary(1);
    auto token = session.BeginOccurrence(Draw(1, 1), CaptureEventType::Draw,
                                         CaptureUnsupported);
    OwnedDrawGeometry geometry;
    geometry.positions = { { { 0, 0, 0, 1 } }, { { 0, 0, 0, 1 } } };
    uint32_t nan = 0x7fc12345;
    std::memcpy(&geometry.positions[1][0], &nan, 4);
    geometry.indices = { 99, 0, 0 };
    assert(session.StageGeometry(token, geometry));
    uint32_t bits[2] = { nan, 0x80000000 };
    XemuShaderDrawBlob blob{};
    blob.name = "raw-stream";
    blob.data = bits;
    blob.byte_count = sizeof(bits);
    blob.normalized = 0xf1234567;
    assert(session.StageBlob(token, blob));
    assert(session.Finish(token, false, 5, 2, 3));
    assert(session.InputsComplete(token));
    session.Stop();
    auto event = session.Find(token & ~kCaptureSessionTokenBit);
    assert(event && !event->emitted);
    assert(event->limitations & CaptureMalformed);
    assert(event->limitations & CaptureUnsupported);
    assert(event->limitations & CaptureSuppressed);
    auto raw = event->CopyGeometry();
    assert(!std::memcmp(raw.positions.data(), geometry.positions.data(),
                        geometry.positions.size() *
                            sizeof(geometry.positions[0])));
    assert(event->CopyInputs().blobs[0].normalized == blob.normalized);
    auto directory =
        std::filesystem::temp_directory_path() / "xemu-capture-raw-test";
    std::filesystem::remove_all(directory);
    std::string error;
    assert(session.Save(directory, &error));
    CaptureSessionSnapshot reopened;
    assert(CaptureSession::Reopen(directory, &reopened, &error));
    const auto &saved = reopened.events.back();
    assert(saved->CopyInputs().blobs[0].bytes ==
           event->CopyInputs().blobs[0].bytes);
    assert(saved->CopyInputs().blobs[0].normalized == blob.normalized);
    assert(saved->geometry.positions->bytes ==
           event->geometry.positions->bytes);
    std::ifstream file(directory / "metadata.json");
    auto metadata = nlohmann::json::parse(file);
    file.close();
    // Legacy inline-event archives remain readable after paging is introduced.
    metadata["version"] = 1;
    metadata["events"] = nlohmann::json::array();
    for (const auto &page : metadata["event_pages"]) {
        std::ifstream contents(directory / "events" /
                               page["file"].get<std::string>());
        for (const auto &item : nlohmann::json::parse(contents))
            metadata["events"].push_back(item);
    }
    metadata.erase("event_pages");
    metadata.erase("event_count");
    std::ofstream(directory / "metadata.json") << metadata.dump();
    assert(CaptureSession::Reopen(directory, &reopened, &error));
    auto limited = metadata;
    limited["settings"]["disk_bytes"] = 1;
    std::ofstream(directory / "metadata.json") << limited.dump();
    assert(!CaptureSession::Reopen(directory, &reopened, &error));
    limited = metadata;
    limited["settings"]["event_bytes"] = 8;
    std::ofstream(directory / "metadata.json") << limited.dump();
    assert(!CaptureSession::Reopen(directory, &reopened, &error));
    limited = metadata;
    limited["events"].back()["payload_bytes"] = 0;
    std::ofstream(directory / "metadata.json") << limited.dump();
    assert(!CaptureSession::Reopen(directory, &reopened, &error));
    limited = metadata;
    limited["range"][2] = 999;
    std::ofstream(directory / "metadata.json") << limited.dump();
    assert(!CaptureSession::Reopen(directory, &reopened, &error));
    metadata["blocks"][0]["file"] = "../outside.raw";
    std::ofstream(directory / "metadata.json") << metadata.dump();
    assert(!CaptureSession::Reopen(directory, &reopened, &error));
    assert(reopened.events.empty() && !error.empty());
    std::filesystem::remove_all(directory);
}
static void test_budget_stops_new_events_and_preserves_pending()
{
    CaptureSession session;
    CaptureSessionSettings settings;
    settings.event_budget = 2;
    assert(session.Start(Context(), settings));
    session.GuestFrameBoundary(1);
    auto token = session.BeginOccurrence(Draw(1, 1));
    assert(token);
    assert(session.BeginOccurrence(Draw(1, 2)) == 0);
    assert(session.Snapshot().state == CaptureSessionState::BudgetExceeded);
    assert(session.WantsInputs(token));
    assert(session.StageSource(token, 2, "owned", 5));
    assert(session.Finish(token, true, 5, 3, 0));
    assert(session.InputsComplete(token));
    auto snapshot = session.Snapshot();
    assert(snapshot.state == CaptureSessionState::BudgetExceeded);
    assert(snapshot.pending_events == 0 && snapshot.events.size() == 2);
    assert(!snapshot.reason.empty());
}
static void test_admitted_readback_reservation_is_not_charged_twice()
{
    CaptureSession calibration;
    assert(calibration.Start(Context()));
    calibration.GuestFrameBoundary(1);
    assert(calibration.BeginOccurrence(Draw(1, 1)));
    uint64_t metadata_bytes = calibration.Snapshot().cpu_bytes;
    CaptureSession session;
    CaptureSessionSettings settings;
    settings.cpu_byte_budget =
        metadata_bytes + 1024 + sizeof(CaptureImmutableBlock);
    assert(session.Start(Context(), settings));
    session.GuestFrameBoundary(1);
    auto token = session.BeginOccurrence(Draw(1, 1));
    assert(session.ReservePayload(token, 1024));
    std::vector<uint8_t> rgba(1024, 7);
    XemuShaderDrawImage image{ 16, 16, rgba.data(), rgba.size() };
    assert(session.StageImage(token, true, image));
    assert(session.Snapshot().reserved_bytes == 0);
    assert(session.Snapshot().cpu_bytes == settings.cpu_byte_budget);
    assert(session.Finish(token, true, 5, 3, 0));
    assert(session.InputsComplete(token));
    session.Stop();
    assert(session.Snapshot().state == CaptureSessionState::Ready);
}
static void test_cancel_and_restart_reject_stale_tokens()
{
    CaptureSession session;
    assert(session.Start(Context()));
    session.GuestFrameBoundary(1);
    auto old = session.BeginOccurrence(Draw(1, 1));
    session.Cancel();
    assert(!session.StageSource(old, 2, "stale", 5));
    assert(session.Start(Context()));
    session.GuestFrameBoundary(1);
    auto next = session.BeginOccurrence(Draw(1, 1));
    assert(next != old);
    assert(!session.Finish(old, true, 5, 1, 0));
    session.BudgetExceeded(old, "Stale failure");
    assert(session.Active());
    session.Invalidate(1, 2, 4);
    assert(session.Snapshot().state == CaptureSessionState::Cancelled);
    assert(session.Snapshot().events.back()->type ==
           CaptureEventType::ResetBoundary);
    assert(!session.StageSource(next, 2, "stale", 5));
}
static void test_failure_before_submission_retains_actual_outcome()
{
    CaptureSession session;
    assert(session.Start(Context()));
    session.GuestFrameBoundary(1);
    auto token = session.BeginOccurrence(Draw(1, 1));
    session.Fail(token, "Optional readback unavailable");
    assert(session.Finish(token, true, 5, 17, 9));
    session.Stop();
    auto event = session.Find(token & ~kCaptureSessionTokenBit);
    assert(event && event->emitted && event->finished && !event->pending);
    assert(event->summary.vertex_count == 17 &&
           event->summary.index_count == 9);
    assert(event->limitations & CaptureReadbackFailed);
}
static void test_frame_rewind_invalidates_pending_tokens()
{
    CaptureSession session;
    CaptureSessionSettings settings;
    settings.mode = CaptureSessionMode::RollingAnimation;
    assert(session.Start(Context(), settings));
    session.GuestFrameBoundary(5);
    auto token = session.BeginOccurrence(Draw(5, 1));
    session.GuestFrameBoundary(1);
    assert(session.Snapshot().state == CaptureSessionState::Cancelled);
    assert(!session.WantsInputs(token));
    assert(!session.InputsComplete(token));
}
static void test_checkpoint_policy_and_non_draw_outcomes()
{
    CaptureSession session;
    assert(session.Start(Context()));
    session.GuestFrameBoundary(1);
    auto first = session.BeginOccurrence(Draw(1, 1));
    auto second = session.BeginOccurrence(Draw(1, 2));
    assert(session.WantsImages(first) && !session.WantsImages(second));
    auto state = Draw(1, 3);
    state.shader_count = 0;
    auto command = session.BeginOccurrence(state, CaptureEventType::StateWrite);
    assert(session.Finish(command, false, 0, 0, 0));
    assert(session.InputsComplete(command));
    auto event = session.Find(command & ~kCaptureSessionTokenBit);
    assert(!(event->limitations & CaptureSuppressed));
    session.Cancel();
    CaptureSessionSettings settings;
    settings.maximum_evidence = true;
    assert(session.Start(Context(), settings));
    session.GuestFrameBoundary(1);
    assert(session.WantsImages(session.BeginOccurrence(Draw(1, 1))));
    assert(session.WantsImages(session.BeginOccurrence(Draw(1, 2))));
}
static void test_existing_temporary_directory_is_not_removed()
{
    CaptureSession session;
    assert(session.Start(Context()));
    session.GuestFrameBoundary(1);
    CompleteDraw(session, 1, 1);
    session.Stop();
    auto directory =
        std::filesystem::temp_directory_path() / "xemu-capture-temp-test";
    auto temporary = directory;
    temporary += ".tmp-" +
                 std::to_string(session.Snapshot().events.back()->event_id + 1);
    std::filesystem::remove_all(directory);
    std::filesystem::remove_all(temporary);
    std::filesystem::create_directory(temporary);
    std::ofstream(temporary / "keep.txt") << "owned by someone else";
    std::string error;
    assert(!session.Save(directory, &error));
    assert(std::filesystem::exists(temporary / "keep.txt"));
    std::filesystem::remove_all(temporary);
}
static void test_rejected_and_empty_draws_are_not_suppression()
{
    CaptureSession session;
    assert(session.Start(Context()));
    session.GuestFrameBoundary(1);
    for (uint32_t outcome : { 1U, 2U, 3U }) {
        auto token = session.BeginOccurrence(Draw(1, outcome));
        assert(session.StageRegister(token, "capture.outcome", outcome));
        assert(session.Finish(token, false, 5, 0, 0));
        assert(session.InputsComplete(token));
        auto event = session.Find(token & ~kCaptureSessionTokenBit);
        assert(event->summary.key.submission == 0);
        assert(bool(event->limitations & CaptureSuppressed) == (outcome == 1));
        if (outcome == 2)
            assert(event->limitations & CaptureUnsupported);
        if (outcome == 3)
            assert(event->limitations & CaptureMalformed);
    }
    session.Stop();
}
static void test_retained_frame_holes_are_explicit_and_persisted()
{
    CaptureSession session;
    CaptureSessionSettings settings;
    settings.mode = CaptureSessionMode::RollingAnimation;
    settings.history_frames = 1;
    assert(session.Start(Context(), settings));
    session.GuestFrameBoundary(1);
    auto pending = session.BeginOccurrence(Draw(1, 1));
    assert(session.Finish(pending, true, 5, 3, 0));
    session.GuestFrameBoundary(2);
    CompleteDraw(session, 2, 2);
    session.GuestFrameBoundary(3);
    CompleteDraw(session, 3, 3);
    session.Stop();
    assert(session.InputsComplete(pending));
    auto snapshot = session.Snapshot();
    assert((snapshot.retained_frames == std::vector<uint64_t>{ 1, 3 }));
    assert(snapshot.first_frame == 1 && snapshot.last_frame == 3);
    auto directory =
        std::filesystem::temp_directory_path() / "xemu-capture-holes-test";
    std::filesystem::remove_all(directory);
    std::string error;
    assert(session.Save(directory, &error));
    CaptureSessionSnapshot reopened;
    assert(CaptureSession::Reopen(directory, &reopened, &error));
    assert(reopened.retained_frames == snapshot.retained_frames);
    std::filesystem::remove_all(directory);
}
static void test_global_budget_failure_keeps_existing_inputs_writable()
{
    CaptureSession session;
    assert(session.Start(Context()));
    session.GuestFrameBoundary(1);
    auto token = session.BeginOccurrence(Draw(1, 1));
    session.BudgetExceeded(0, "Metadata allocation failed");
    assert(!session.Active());
    assert(session.Snapshot().state == CaptureSessionState::BudgetExceeded);
    assert(!session.BeginOccurrence(Draw(1, 2)));
    assert(session.WantsInputs(token));
    assert(session.StageSource(token, 2, "pending", 7));
    assert(session.Finish(token, true, 5, 3, 0));
    assert(session.InputsComplete(token));
    assert(session.Snapshot().pending_events == 0);
}
int main()
{
    test_atomic_claim_rejects_rearmed_context();
    test_archive_descriptor_memory_budget();
    {
        CaptureSessionSnapshot snapshot;
        snapshot.context = Context();
        snapshot.settings.event_budget = 8192;
        snapshot.settings.cpu_byte_budget = 256U * 1024U * 1024U;
        snapshot.state = CaptureSessionState::Ready;
        for (uint32_t i = 0; i < 4096; ++i) {
            auto event = std::make_shared<CaptureOccurrence>();
            event->event_id = i + 1;
            event->summary = Draw(1, i);
            event->finished = true;
            event->pending = false;
            event->inputs.complete = true;
            for (uint32_t field = 0; field < 512; ++field)
                event->inputs.registers.push_back(
                    { "raw.command.field." + std::to_string(field), field });
            snapshot.events.push_back(std::move(event));
        }
        snapshot.total_events = snapshot.events.size();
        const auto path = std::filesystem::temp_directory_path() /
                          "xemu-capture-paged-metadata-test";
        std::filesystem::remove_all(path);
        std::string error;
        assert(CaptureSession::SaveSnapshot(snapshot, path, &error));
        CaptureSessionSnapshot reopened;
        assert(CaptureSession::Reopen(path, &reopened, &error));
        assert(reopened.events.size() == 4096);
        assert(reopened.events.back()->inputs.registers[511].value == 511);
        std::ofstream(path / "events" / "0.json", std::ios::trunc) << "[]";
        assert(!CaptureSession::Reopen(path, &reopened, &error));
        assert(reopened.events.empty() && !error.empty());
        std::filesystem::remove_all(path);
    }
    {
        CaptureSession session;
        CaptureSessionSettings settings;
        settings.mode = CaptureSessionMode::RollingAnimation;
        assert(session.Start(Context(), settings));
        const auto token = session.BeginOccurrence(Draw(1, 1));
        assert(token && session.ReservePayload(token, 4096));
        assert(session.StageSource(token, 2, "evidence", 8));
        session.AbortAllocationFailure();
        const auto snapshot = session.Snapshot();
        assert(snapshot.state == CaptureSessionState::BudgetExceeded);
        assert(!session.Active() && !snapshot.pending_events &&
               !snapshot.reserved_bytes);
        assert(snapshot.events.size() == 1 &&
               snapshot.events[0]->inputs.sources[2]);
        assert(snapshot.events[0]->limitations & CaptureReadbackFailed);
        assert(!session.WantsInputs(token) && !session.InputsComplete(token));
        assert(!session.Finish(token, true, 5, 3, 0));
    }
    {
        CaptureSession session;
        CaptureSessionSettings settings;
        settings.mode = CaptureSessionMode::RollingAnimation;
        settings.post_frames = 2;
        assert(session.Start(Context(), settings));
        const auto token = session.BeginOccurrence(Draw(1000, 1));
        assert(token && session.Mark());
        session.Finish(token, true, 5, 3, 0);
        session.InputsComplete(token);
        session.GuestFrameBoundary(1001);
        assert(session.Active());
        assert(session.Snapshot().trigger_frame == 1000);
        session.GuestFrameBoundary(1003);
        assert(!session.Active());
    }
    test_many_uses_preserve_owned_versions();
    test_rolling_range_waits_for_pending_payload();
    test_raw_malformed_and_suppressed_evidence_survives();
    test_budget_stops_new_events_and_preserves_pending();
    test_admitted_readback_reservation_is_not_charged_twice();
    test_cancel_and_restart_reject_stale_tokens();
    test_failure_before_submission_retains_actual_outcome();
    test_frame_rewind_invalidates_pending_tokens();
    test_checkpoint_policy_and_non_draw_outcomes();
    test_existing_temporary_directory_is_not_removed();
    test_rejected_and_empty_draws_are_not_suppression();
    test_retained_frame_holes_are_explicit_and_persisted();
    test_global_budget_failure_keeps_existing_inputs_writable();
    std::cout << "capture session tests passed\n";
}

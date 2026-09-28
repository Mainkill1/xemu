// SPDX-License-Identifier: GPL-2.0-or-later
#include "../../ui/xui/shader-browser-capture-comparison.hh"
#include "../../ui/xui/shader-browser-capture-replay.hh"
#include "../../ui/xui/shader-browser-preview-adapter.hh"

#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <iostream>

using namespace xemu::shader_browser;

#define CHECK(...)                                                   \
    do {                                                             \
        if (!(__VA_ARGS__)) {                                        \
            std::cerr << "Check failed at line " << __LINE__ << ": " \
                      << #__VA_ARGS__ << '\n';                       \
            std::exit(EXIT_FAILURE);                                 \
        }                                                            \
    } while (false)

static ShaderKey Pixel()
{
    const uint8_t recipe[] = { 1, 2, 3, 4 };
    return { ComputeShaderHash(1, Stage::Pixel, 1, recipe, sizeof(recipe)),
             Stage::Pixel };
}
static CaptureSessionContext Context()
{
    CaptureSessionContext c;
    c.scope.title_id = 17;
    c.scope_generation = 1;
    c.session_epoch = 2;
    c.renderer_epoch = 3;
    c.generation = 4;
    c.backend = 1;
    return c;
}
static void Record(CaptureSession &session, uint32_t index, bool emitted = true,
                   int32_t width = 160, int32_t height = 120)
{
    DrawCaptureSummary summary;
    summary.scope = Context().scope;
    summary.key = { 2, 3, 1, index, index + 1 };
    summary.shaders[0] = Pixel();
    summary.shader_count = 1;
    auto token = session.BeginOccurrence(summary);
    OwnedDrawGeometry geometry;
    geometry.positions = { { { float(index), 0, 0, 1 } },
                           { { 0, 1, 0, 1 } },
                           { { 0, 0, 1, 1 } } };
    geometry.indices = { 0, 1, 2 };
    CHECK(session.StageGeometry(token, geometry));
    const float value = float(index);
    XemuShaderDrawUniform uniform{
        2,      "constantColor", XEMU_SHADER_DRAW_UNIFORM_FLOAT, 1, 1,
        &value, sizeof(value)
    };
    CHECK(session.StageUniform(token, uniform));
    uint8_t pixel[] = { uint8_t(index), 0, 0, 255 };
    XemuShaderDrawTexture texture{};
    texture.slot = 0;
    texture.bound = 1;
    texture.width = texture.height = texture.depth = texture.face_count =
        texture.mip_levels = 1;
    texture.image = { 1, 1, pixel, sizeof(pixel) };
    CHECK(session.StageTexture(token, texture));
    CHECK(session.StageSource(token, 1, "vertex", 6));
    CHECK(session.StageSource(token, 2, "pixel", 5));
    const uint8_t recipe[] = { 1, 2, 3, 4 };
    XemuShaderDrawBlob blob{};
    blob.name = "recipe.stage2";
    blob.data = recipe;
    blob.byte_count = sizeof(recipe);
    CHECK(session.StageBlob(token, blob));
    blob.name = "geometry.host_indices";
    blob.data = geometry.indices.data();
    blob.byte_count = geometry.indices.size() * sizeof(uint32_t);
    blob.count = 3;
    CHECK(session.StageBlob(token, blob));
    blob.name = "vertex.attribute0";
    blob.data = geometry.positions.data();
    blob.byte_count = geometry.positions.size() * sizeof(geometry.positions[0]);
    blob.slot = 0;
    blob.count = 3;
    blob.format = 0x1406;
    blob.components = 4;
    blob.stride = 16;
    CHECK(session.StageBlob(token, blob));
    const int32_t viewport[] = { 0, 0, width, height };
    blob.name = "host.viewport";
    blob.data = viewport;
    blob.byte_count = sizeof(viewport);
    CHECK(session.StageBlob(token, blob));
    CHECK(session.StageRegister(token, "capture.first_vertex", 0));
    CHECK(session.StageRegister(token, "capture.last_vertex", 2));
    CHECK(session.StageRegister(token, "vertex.enabled0", 1));
    CHECK(session.StageRegister(token, "capture.generator_abi", 1));
    CHECK(session.StageRegister(token, "capture.interface_abi", 1));
    CHECK(session.Finish(token, emitted, 5, 3, 3));
    CHECK(session.InputsComplete(token));
}
static CaptureSessionSnapshot
Recording(uint32_t count = 32, int32_t width = 160, int32_t height = 120)
{
    CaptureSession session;
    CHECK(session.Start(Context()));
    session.GuestFrameBoundary(1);
    for (uint32_t i = 0; i < count; ++i)
        Record(session, i, true, width, height);
    session.Stop();
    return session.Snapshot();
}
static CaptureComparisonRequest Request()
{
    CaptureComparisonRequest request;
    request.selection.scope = Context().scope;
    request.selection.shader = Pixel();
    request.selection.backend = PreviewBackend::OpenGL;
    request.selection.session_epoch = 10;
    request.selection.renderer_epoch = 11;
    request.edit = { 7, 8, 9, "edited pixel" };
    request.width = request.height = 2;
    return request;
}
static CaptureComparisonBuildOutcome
MockPackets(const CaptureComparisonRequest &request,
            const CaptureOccurrence &event, PreviewPacket *original,
            PreviewPacket *replacement, std::string *error)
{
    PreviewPacketInputs inputs;
    inputs.selection = request.selection;
    inputs.source_resident = true;
    inputs.source = event.CopyInputs().sources[2];
    inputs.partner_source = "vertex";
    inputs.generator_abi = inputs.interface_abi = 1;
    inputs.recipe.key = request.selection.shader;
    inputs.recipe.recipe_format_version = 1;
    inputs.recipe.bytes = { 1, 2, 3, 4 };
    for (const auto &blob : event.CopyInputs().blobs)
        if (blob.name == "recipe.stage2")
            inputs.recipe.bytes = blob.bytes;
    inputs.recipe.scopes = { request.selection.scope };
    inputs.fixture_bytes = EncodePreviewSyntheticFixture(
        MakePreviewFixture(PreviewFixtureProfile::Flat));
    inputs.input_revision = event.event_id;
    inputs.width = request.width;
    inputs.height = request.height;
    CHECK(BuildPreviewPacket(inputs, original, error));
    auto geometry = event.CopyGeometry();
    original->captured_mesh = { geometry.positions, geometry.indices };
    original->mesh_digest = ComputeCapturedMeshDigest(original->captured_mesh);
    original->captured_material =
        BuildPreviewCapturedMaterial(event.CopyInputs());
    original->material_digest =
        ComputePreviewCapturedMaterialDigest(*original->captured_material);
    original->packet_kind = PreviewPacketKind::Replay;
    original->replay_class = PreviewReplayClass::Approximate;
    *replacement = *original;
    replacement->source = request.edit.source;
    replacement->source_variant = PreviewSourceVariant::Edited;
    replacement->replacement_id = request.edit.id;
    replacement->replacement_revision = request.edit.revision;
    replacement->draft_id = request.edit.id;
    replacement->draft_revision = request.edit.revision;
    replacement->draft_submission_id = request.edit.submission_id;
    replacement->source_digest = ComputePreviewDigest(
        reinterpret_cast<const uint8_t *>(request.edit.source.data()),
        request.edit.source.size());
    CHECK(ValidatePreviewPacket(*original, error));
    CHECK(ValidatePreviewPacket(*replacement, error));
    return CaptureComparisonBuildOutcome::Ready;
}
static CaptureComparisonCompletion Completion(const CaptureComparisonJob &job,
                                              uint8_t value)
{
    CaptureComparisonCompletion done;
    done.identity = job.identity;
    done.phase = job.phase;
    done.result = job.expected_result;
    done.image = { 2, 2, std::vector<uint8_t>(16, value) };
    return done;
}
static void test_every_use_has_distinct_result_and_exact_identity()
{
    auto recording = Recording();
    CaptureComparisonController controller;
    auto request = Request();
    request.profile_draw = true;
    auto run = controller.Start(request, recording);
    CHECK(run);
    auto initial = controller.Snapshot();
    CHECK(initial.matched_events == 32 && initial.results.size() == 32);
    std::vector<uint64_t> events;
    CaptureComparisonJob job;
    for (uint32_t i = 0; i < 32; ++i) {
        CHECK(controller.TryClaimJob(&job, MockPackets));
        CHECK(job.phase == CaptureComparisonPhase::Original);
        CHECK(job.packet->profile_draw && job.expected_result.profile_draw);
        CHECK(job.packet->captured_mesh.positions[0][0] == float(i));
        CHECK(job.expected_result.input_revision == job.identity.event_id);
        CHECK(job.identity.original_revision ==
              job.occurrence->inputs.sources[2]->id);
        events.push_back(job.identity.event_id);
        auto wrong = Completion(job, uint8_t(i));
        wrong.result.input_revision++;
        CHECK(!controller.Complete(std::move(wrong)));
        auto original = Completion(job, uint8_t(i));
        original.draw_timing.result = job.expected_result;
        original.draw_timing.backend = job.packet->selection.backend;
        original.draw_timing.provenance =
            PreviewDrawTimingProvenance::ReplayInstrumented;
        original.draw_timing.status = PreviewDrawTimingStatus::Measured;
        original.draw_timing.timestamp_valid_bits = 64;
        original.draw_timing.timestamp_period_ns = 1;
        original.draw_timing.actual_draw_commands = 1;
        original.draw_timing.nanoseconds = i + 100;
        auto pending = original;
        pending.draw_timing.status = PreviewDrawTimingStatus::Pending;
        pending.draw_timing.nanoseconds = 0;
        CHECK(!controller.Complete(std::move(pending)));
        CHECK(controller.StillCurrent(job.identity, job.phase));
        auto stale_timing = original;
        ++stale_timing.draw_timing.result.input_revision;
        CHECK(!controller.Complete(std::move(stale_timing)));
        CHECK(controller.Complete(original));
        CHECK(!controller.Complete(original));
        CHECK(controller.TryClaimJob(&job, MockPackets));
        CHECK(job.phase == CaptureComparisonPhase::Replacement);
        CHECK(job.packet->source == Request().edit.source);
        auto replacement = Completion(job, uint8_t(i + 1));
        replacement.draw_timing = original.draw_timing;
        replacement.draw_timing.result = job.expected_result;
        replacement.draw_timing.nanoseconds = i + 200;
        CHECK(controller.Complete(std::move(replacement)));
    }
    CHECK(!controller.TryClaimJob(&job, MockPackets));
    auto result = controller.Snapshot();
    CHECK(result.state == CaptureComparisonState::Ready);
    CHECK(result.original_timing.measured == 32 &&
          result.original_timing.sample_count == 32);
    CHECK(result.replacement_timing.measured == 32 &&
          result.replacement_timing.sample_count == 32);
    CHECK(result.original_timing.median_ns == 115.5 &&
          result.original_timing.p95_ns == 130);
    CHECK(result.replacement_timing.median_ns == 215.5 &&
          result.replacement_timing.p95_ns == 230);
    CHECK(result.outcomes[size_t(CaptureComparisonOutcome::Completed)] == 32);
    for (uint32_t i = 0; i < 32; ++i) {
        const auto &row = result.results[i];
        CHECK(row.identity.event_id == events[i]);
        CHECK(row.original->rgba[0] == i && row.replacement->rgba[0] == i + 1);
        CHECK(row.difference.compared_bytes == 16 &&
              row.difference.compared_pixels == 4);
        CHECK(row.difference.changed_bytes == 16 &&
              row.difference.changed_pixels == 4);
        CHECK(row.difference.maximum_difference == 1 &&
              row.difference.mean_difference == 1);
        CHECK(row.replay_class == PreviewReplayClass::Approximate);
    }
}
static void test_latest_request_rejects_old_phase_and_source_revision()
{
    auto recording = Recording(2);
    CaptureComparisonController controller;
    auto request = Request();
    CHECK(controller.Start(request, recording));
    CaptureComparisonJob old;
    CHECK(controller.TryClaimJob(&old, MockPackets));
    request.edit.revision++;
    request.edit.source += " new";
    auto current = controller.Start(request, recording);
    CHECK(current != old.identity.request_id);
    CHECK(!controller.StillCurrent(old.identity, old.phase));
    CHECK(!controller.Complete(Completion(old, 1)));
    CaptureComparisonJob job;
    CHECK(controller.TryClaimJob(&job, MockPackets));
    CHECK(job.identity.replacement_revision == request.edit.revision);
    auto wrong = Completion(job, 1);
    wrong.identity.replacement_revision--;
    CHECK(!controller.Complete(std::move(wrong)));
    controller.Cancel();
    CHECK(!controller.Complete(Completion(job, 1)));
    CHECK(controller.Snapshot()
              .outcomes[size_t(CaptureComparisonOutcome::Cancelled)] == 2);
}
static void test_repeated_frozen_runs_require_fresh_result_and_timing_keys()
{
    const auto recording = Recording(1);
    const auto event =
        std::find_if(recording.events.begin(), recording.events.end(),
                     [](const auto &occurrence) {
                         return occurrence->type == CaptureEventType::Draw;
                     });
    CHECK(event != recording.events.end());
    auto request = Request();
    request.profile_draw = true;
    CaptureComparisonController controller;
    std::array<PreviewResultKey, 2> previous_keys;
    std::array<PreviewDrawTiming, 2> previous_timings;
    uint64_t previous_token = 0;
    for (unsigned run = 0; run < 2; ++run) {
        CHECK(controller.Start(request, recording));
        CaptureComparisonJob job;
        for (unsigned phase = 0; phase < 2; ++phase) {
            CHECK(controller.TryClaimJob(&job, MockPackets));
            CHECK(job.identity.event_id == (*event)->event_id);
            CHECK(job.expected_result.input_revision == job.identity.event_id);
            CHECK(job.packet->source ==
                  (phase ? request.edit.source : "pixel"));
            auto done = Completion(job, uint8_t(run + phase));
            done.draw_timing.result = job.expected_result;
            done.draw_timing.backend = job.packet->selection.backend;
            done.draw_timing.provenance =
                PreviewDrawTimingProvenance::ReplayInstrumented;
            done.draw_timing.status = PreviewDrawTimingStatus::Unsupported;
            done.draw_timing.message = "Controller unit has no GPU timestamps";
            if (run) {
                CHECK(job.expected_result != previous_keys[phase]);
                CHECK(job.expected_result.compile ==
                      previous_keys[phase].compile);
                CHECK(job.expected_result.view_revision !=
                      previous_keys[phase].view_revision);
                CHECK(job.expected_result.view_revision == job.identity.token);
                CHECK(job.identity.token != previous_token);
                auto cached_image = done;
                cached_image.result = previous_keys[phase];
                CHECK(!controller.Complete(std::move(cached_image)));
                auto cached_timing = done;
                cached_timing.draw_timing = previous_timings[phase];
                CHECK(!controller.Complete(std::move(cached_timing)));
                CHECK(controller.StillCurrent(job.identity, job.phase));
            } else {
                previous_keys[phase] = job.expected_result;
                previous_timings[phase] = done.draw_timing;
                previous_token = job.identity.token;
            }
            CHECK(controller.Complete(std::move(done)));
        }
        CHECK(!controller.TryClaimJob(&job, MockPackets));
        CHECK(controller.Snapshot().state == CaptureComparisonState::Ready);
    }
}
static void test_coverage_includes_nonemitted_pending_and_producer_unsupported()
{
    auto recording = Recording(3);
    auto suppressed = std::make_shared<CaptureOccurrence>(*recording.events[2]);
    suppressed->emitted = false;
    suppressed->limitations |= CaptureSuppressed;
    recording.events[2] = suppressed;
    auto pending = std::make_shared<CaptureOccurrence>(*recording.events[3]);
    pending->pending = true;
    pending->inputs.complete = false;
    recording.events[3] = pending;
    CaptureComparisonController controller;
    CHECK(controller.Start(Request(), recording));
    auto snapshot = controller.Snapshot();
    CHECK(snapshot.matched_events == 3 && snapshot.admitted_jobs == 1);
    CHECK(snapshot.results[1].outcome == CaptureComparisonOutcome::Unsupported);
    CHECK(snapshot.results[2].outcome == CaptureComparisonOutcome::Incomplete);
    auto request = Request();
    request.dependencies = CaptureComparisonDependencies::ProducerSuffix;
    CHECK(controller.Start(request, recording));
    snapshot = controller.Snapshot();
    CHECK(snapshot.state == CaptureComparisonState::Ready &&
          snapshot.admitted_jobs == 0);
    CHECK(snapshot.outcomes[size_t(CaptureComparisonOutcome::Unsupported)] ==
          3);
    CaptureComparisonJob job;
    CHECK(!controller.TryClaimJob(&job, MockPackets));
}
static void test_recorded_command_without_accepted_submission_is_not_compared()
{
    auto recording = Recording(1);
    auto &entry =
        *std::find_if(recording.events.begin(), recording.events.end(),
                      [](const auto &event) {
                          return event->type == CaptureEventType::Draw;
                      });
    const auto baseline = entry;
    CaptureComparisonController controller;
    for (const auto outcome :
         { CaptureBatchOutcome::None, CaptureBatchOutcome::Pending,
           CaptureBatchOutcome::SubmissionFailed, CaptureBatchOutcome::Aborted,
           CaptureBatchOutcome::Detached }) {
        auto event = std::make_shared<CaptureOccurrence>(*baseline);
        event->batch_id = 17;
        event->command_recorded = true;
        event->submission = outcome;
        entry = event;
        CHECK(controller.Start(Request(), recording));
        const auto result = controller.Snapshot();
        CHECK(result.admitted_jobs == 0);
        CHECK(result.results[0].outcome ==
              CaptureComparisonOutcome::Unsupported);
    }
    auto event = std::make_shared<CaptureOccurrence>(*baseline);
    event->batch_id = 17;
    event->command_recorded = true;
    event->submission = CaptureBatchOutcome::Submitted;
    event->completion = CaptureBatchCompletion::Failed;
    entry = event;
    CHECK(controller.Start(Request(), recording));
    CHECK(controller.Snapshot().admitted_jobs == 0);
    CHECK(controller.Snapshot().results[0].outcome ==
          CaptureComparisonOutcome::Incomplete);
    event = std::make_shared<CaptureOccurrence>(*event);
    event->completion = CaptureBatchCompletion::Completed;
    entry = event;
    CHECK(controller.Start(Request(), recording));
    CHECK(controller.Snapshot().admitted_jobs == 1);
}
static void test_budgets_report_every_matched_use_or_an_explicit_omission()
{
    auto recording = Recording();
    CaptureComparisonController controller;
    CaptureComparisonSettings settings;
    settings.maximum_jobs = 1;
    CHECK(controller.Start(Request(), recording, settings));
    auto snapshot = controller.Snapshot();
    CHECK(snapshot.matched_events == 32 && snapshot.results.size() == 32 &&
          snapshot.admitted_jobs == 1);
    CHECK(snapshot.outcomes[size_t(CaptureComparisonOutcome::Incomplete)] ==
          31);
    settings.maximum_rows = 1;
    CHECK(controller.Start(Request(), recording, settings));
    snapshot = controller.Snapshot();
    CHECK(snapshot.matched_events == 32 && snapshot.omitted_events == 31 &&
          snapshot.results.size() == 1);
    CHECK(!snapshot.message.empty());
    CaptureComparisonJob job;
    CHECK(controller.TryClaimJob(&job, MockPackets));
    CHECK(controller.Complete(Completion(job, 0)));
    CHECK(controller.TryClaimJob(&job, MockPackets));
    CHECK(controller.Complete(Completion(job, 1)));
    CHECK(controller.Snapshot().state == CaptureComparisonState::Ready);
    CaptureComparisonController images;
    settings = {};
    settings.image_byte_budget = 2 * (sizeof(CaptureComparisonImage) + 16);
    CHECK(images.Start(Request(), Recording(2), settings));
    CHECK(images.TryClaimJob(&job, MockPackets));
    CHECK(images.Complete(Completion(job, 0)));
    CHECK(images.TryClaimJob(&job, MockPackets));
    CHECK(images.Complete(Completion(job, 1)));
    CHECK(images.TryClaimJob(&job, MockPackets));
    CHECK(images.Complete(Completion(job, 2)));
    snapshot = images.Snapshot();
    CHECK(snapshot.outcomes[size_t(CaptureComparisonOutcome::Completed)] == 1);
    CHECK(snapshot.outcomes[size_t(CaptureComparisonOutcome::Incomplete)] == 1);
    CHECK(snapshot.image_bytes == settings.image_byte_budget);
}
static void test_compile_failure_keeps_baseline_and_advances_to_other_uses()
{
    CaptureComparisonController controller;
    CHECK(controller.Start(Request(), Recording(2)));
    CaptureComparisonJob job;
    CHECK(controller.TryClaimJob(&job, MockPackets));
    CHECK(controller.Complete(Completion(job, 4)));
    CHECK(controller.TryClaimJob(&job, MockPackets));
    auto failed = Completion(job, 0);
    failed.image = {};
    failed.outcome = CaptureComparisonOutcome::CompileFailed;
    failed.message = "compiler diagnostic";
    CHECK(controller.Complete(std::move(failed)));
    auto snapshot = controller.Snapshot();
    CHECK(snapshot.results[0].outcome ==
          CaptureComparisonOutcome::CompileFailed);
    CHECK(snapshot.results[0].original->rgba[0] == 4 &&
          !snapshot.results[0].replacement);
    CHECK(controller.TryClaimJob(&job, MockPackets));
    CHECK(job.identity.event_id == snapshot.results[1].identity.event_id);
    CHECK(job.phase == CaptureComparisonPhase::Original);
}
static void test_scopes_do_not_retarget_capture_or_execution_identity()
{
    auto recording = Recording(3);
    auto request = Request();
    request.scope = CaptureComparisonScope::SelectedOccurrence;
    request.selected_event = recording.events[2]->event_id;
    CaptureComparisonController controller;
    CHECK(controller.Start(request, recording));
    auto snapshot = controller.Snapshot();
    CHECK(snapshot.matched_events == 1);
    CHECK(snapshot.results[0].identity.event_id == request.selected_event);
    request.scope = CaptureComparisonScope::MatchingFrame;
    request.first_frame = request.last_frame = 2;
    CHECK(controller.Start(request, recording));
    CHECK(controller.Snapshot().matched_events == 0);
    request.scope = CaptureComparisonScope::MatchingRange;
    request.first_frame = 0;
    request.last_frame = UINT64_MAX;
    CHECK(controller.Start(request, recording));
    CaptureComparisonJob job;
    CHECK(controller.TryClaimJob(&job, MockPackets));
    auto selection = request.selection;
    ++selection.renderer_epoch;
    controller.Invalidate(selection);
    CHECK(!controller.StillCurrent(job.identity, job.phase));
    CHECK(controller.Snapshot()
              .outcomes[size_t(CaptureComparisonOutcome::Cancelled)] == 3);
}
static void test_captured_extent_is_exact_and_never_clamped()
{
    OwnedDrawInputs inputs;
    OwnedDrawBlob viewport;
    viewport.name = "host.viewport";
    int32_t gl[] = { 0, 0, 640, 480 };
    viewport.bytes.resize(sizeof(gl));
    std::memcpy(viewport.bytes.data(), gl, sizeof(gl));
    inputs.blobs.push_back(viewport);
    uint32_t width = 0, height = 0;
    std::string error;
    CHECK(GetCapturedPreviewExtent(inputs, PreviewBackend::OpenGL, &width,
                                   &height, &error));
    CHECK(width == 640 && height == 480);
    gl[0] = 10;
    std::memcpy(inputs.blobs[0].bytes.data(), gl, sizeof(gl));
    CHECK(!GetCapturedPreviewExtent(inputs, PreviewBackend::OpenGL, &width,
                                    &height, &error));
    CHECK(width == 0 && height == 0);
    gl[0] = 0;
    gl[2] = 1280;
    std::memcpy(inputs.blobs[0].bytes.data(), gl, sizeof(gl));
    CHECK(GetCapturedPreviewExtent(inputs, PreviewBackend::OpenGL, &width,
                                   &height, &error));
    CHECK(width == 1280 && height == 480);
    gl[2] = 1921;
    std::memcpy(inputs.blobs[0].bytes.data(), gl, sizeof(gl));
    CHECK(!GetCapturedPreviewExtent(inputs, PreviewBackend::OpenGL, &width,
                                    &height, &error));
    inputs.blobs[0].name = "vk.viewport";
    float vk[] = { 0, 0, 320, 240, 0, 1 };
    inputs.blobs[0].bytes.resize(sizeof(vk));
    std::memcpy(inputs.blobs[0].bytes.data(), vk, sizeof(vk));
    CHECK(GetCapturedPreviewExtent(inputs, PreviewBackend::Vulkan, &width,
                                   &height, &error));
    CHECK(width == 320 && height == 240);
    vk[2] = 1280;
    vk[3] = 480;
    std::memcpy(inputs.blobs[0].bytes.data(), vk, sizeof(vk));
    CHECK(GetCapturedPreviewExtent(inputs, PreviewBackend::Vulkan, &width,
                                   &height, &error));
    CHECK(width == 1280 && height == 480);
    vk[2] = 1920;
    vk[3] = 1080;
    std::memcpy(inputs.blobs[0].bytes.data(), vk, sizeof(vk));
    CHECK(GetCapturedPreviewExtent(inputs, PreviewBackend::Vulkan, &width,
                                   &height, &error));
    CHECK(width == 1920 && height == 1080);
    vk[3] = 1081;
    std::memcpy(inputs.blobs[0].bytes.data(), vk, sizeof(vk));
    CHECK(!GetCapturedPreviewExtent(inputs, PreviewBackend::Vulkan, &width,
                                    &height, &error));
    vk[2] = 320.5f;
    vk[3] = 240;
    std::memcpy(inputs.blobs[0].bytes.data(), vk, sizeof(vk));
    CHECK(!GetCapturedPreviewExtent(inputs, PreviewBackend::Vulkan, &width,
                                    &height, &error));
    inputs.blobs.clear();
    CHECK(!GetCapturedPreviewExtent(inputs, PreviewBackend::Vulkan, &width,
                                    &height, &error));
}
static void
test_default_adapter_uses_original_sources_raw_pipeline_and_native_extent()
{
    auto recording = Recording(2);
    auto request = Request();
    request.width = request.height = 0;
    CaptureComparisonController controller;
    CHECK(controller.Start(request, recording));
    CaptureComparisonJob job;
    CHECK(controller.TryClaimJob(&job));
    CHECK(job.packet->captured_pipeline &&
          job.packet->partner_source == "vertex");
    CHECK(job.packet->source == "pixel" && job.packet->width == 160 &&
          job.packet->height == 120);
    CHECK(job.packet->captured_pipeline->attributes[0].enabled);
    CHECK(job.packet->captured_pipeline->attributes[0].stream.bytes.size() ==
          48);
    CHECK(job.expected_result.pipeline_digest == job.packet->pipeline_digest);
    auto done = Completion(job, 0);
    done.image = { 160, 120, std::vector<uint8_t>(160 * 120 * 4) };
    CHECK(controller.Complete(std::move(done)));
    CHECK(controller.TryClaimJob(&job));
    CHECK(job.packet->source_variant == PreviewSourceVariant::Edited);
    CHECK(job.packet->selection.mode == PreviewMode::Normal);
    CHECK(job.packet->source == request.edit.source &&
          job.packet->draft_revision == request.edit.revision);
    auto snapshot = controller.Snapshot();
    CHECK(snapshot.results[0].original_key.pipeline_digest ==
          snapshot.results[0].replacement_key.pipeline_digest);
    CHECK(snapshot.results[0].original_key.compile.partner_digest ==
          snapshot.results[0].replacement_key.compile.partner_digest);
    auto wide_recording = Recording(1, 1280, 480);
    CaptureComparisonController wide_controller;
    CHECK(wide_controller.Start(request, wide_recording));
    CHECK(wide_controller.TryClaimJob(&job));
    CHECK(job.packet->width == 1280 && job.packet->height == 480 &&
          job.packet->captured_pipeline &&
          job.packet->captured_pipeline->raster.width == 1280 &&
          job.packet->captured_pipeline->raster.height == 480);
    std::string error;
    CHECK(ValidatePreviewPacket(*job.packet, &error));
    auto missing = std::make_shared<CaptureOccurrence>(*recording.events[1]);
    missing->inputs.blobs.erase(
        std::remove_if(
            missing->inputs.blobs.begin(), missing->inputs.blobs.end(),
            [](const auto &blob) { return blob.name == "host.viewport"; }),
        missing->inputs.blobs.end());
    recording.events = { missing };
    CHECK(controller.Start(request, recording));
    CHECK(!controller.TryClaimJob(&job));
    snapshot = controller.Snapshot();
    CHECK(snapshot.results[0].outcome == CaptureComparisonOutcome::Unsupported);
    CHECK(snapshot.results[0].message.find("viewport") != std::string::npos);
}
static void test_packet_pair_preserves_extent_and_frozen_inputs()
{
    auto request = Request();
    request.width = request.height = 0;
    CaptureComparisonController controller;
    CHECK(controller.Start(request, Recording(1)));
    auto different_extent = [](const auto &candidate, const auto &event,
                               auto *original, auto *replacement, auto *error) {
        auto explicit_extent = candidate;
        explicit_extent.width = explicit_extent.height = 2;
        auto outcome =
            MockPackets(explicit_extent, event, original, replacement, error);
        replacement->width = 1;
        return outcome;
    };
    CaptureComparisonJob job;
    CHECK(!controller.TryClaimJob(&job, different_extent));
    CHECK(controller.Snapshot().results[0].outcome ==
          CaptureComparisonOutcome::Incomplete);
    CHECK(controller.Start(Request(), Recording(1)));
    auto different_state = [](const auto &candidate, const auto &event,
                              auto *original, auto *replacement, auto *error) {
        auto outcome =
            MockPackets(candidate, event, original, replacement, error);
        replacement->render_state.clear_color[0] = 0.5f;
        return outcome;
    };
    CHECK(!controller.TryClaimJob(&job, different_state));
    CHECK(controller.Snapshot().results[0].outcome ==
          CaptureComparisonOutcome::Incomplete);
}
static void test_supersession_during_packet_build_does_not_publish_old_packets()
{
    auto recording = Recording(1);
    auto request = Request();
    CaptureComparisonController controller;
    const auto old_request = controller.Start(request, recording);
    CHECK(old_request);
    auto newer = request;
    newer.edit.revision++;
    newer.edit.source += " newer";
    uint64_t new_request = 0;
    auto supersede = [&](const auto &candidate, const auto &event,
                         auto *original, auto *replacement, auto *error) {
        new_request = controller.Start(newer, recording);
        CaptureComparisonJob waiting;
        CHECK(!controller.TryClaimJob(&waiting, MockPackets));
        return MockPackets(candidate, event, original, replacement, error);
    };
    CaptureComparisonJob job;
    CHECK(!controller.TryClaimJob(&job, supersede));
    CHECK(new_request && new_request != old_request);
    CHECK(controller.Snapshot().request_id == new_request);
    CHECK(controller.TryClaimJob(&job, MockPackets));
    CHECK(job.identity.request_id == new_request);
    CHECK(job.packet->source == "pixel");
    CHECK(controller.Complete(Completion(job, 0)));
    CHECK(controller.TryClaimJob(&job, MockPackets));
    CHECK(job.packet->source == newer.edit.source);
}
// Logical replay/controller acceptance only: images below are injected owned
// results, not a native GPU execution or timestamp measurement.
static SharedCaptureBlock SuffixPixels(uint8_t value)
{
    auto block = std::make_shared<CaptureImmutableBlock>();
    block->id = 1000 + value;
    block->bytes.assign(16, value);
    return block;
}
static CaptureResourceOperation
SuffixOp(CaptureResourceOperationType type, uint64_t allocation, uint64_t view,
         uint32_t kind = XEMU_SHADER_CAPTURE_RESOURCE_COLOR)
{
    CaptureResourceOperation op;
    op.type = type;
    op.allocation_id = allocation;
    op.view_id = view;
    op.byte_size = 16;
    op.range = { 0, 16 };
    op.kind = kind;
    return op;
}
static XemuShaderCaptureReplayBinding SuffixBinding(uint32_t role, bool write)
{
    XemuShaderCaptureReplayBinding binding{};
    binding.role = role;
    binding.resource.kind =
        role == XEMU_SHADER_CAPTURE_REPLAY_TEXTURE ||
                role == XEMU_SHADER_CAPTURE_REPLAY_COPY_DESTINATION ?
            XEMU_SHADER_CAPTURE_RESOURCE_TEXTURE :
            XEMU_SHADER_CAPTURE_RESOURCE_COLOR;
    binding.resource.write = write;
    binding.image.width = binding.image.height = 2;
    binding.image.mip_levels = binding.image.layers = binding.image.samples = 1;
    binding.image.format = XEMU_SHADER_CAPTURE_REPLAY_RGBA8_UNORM;
    for (uint32_t i = 0; i < 4; ++i)
        binding.image.storage_to_rgba[i] = binding.image.sample_swizzle[i] = i;
    return binding;
}
static CaptureSessionSnapshot SuffixRecording()
{
    auto snapshot = Recording(2);
    std::vector<std::shared_ptr<const CaptureOccurrence>> draws;
    for (const auto &event : snapshot.events)
        if (event->type == CaptureEventType::Draw)
            draws.push_back(event);
    CHECK(draws.size() == 2);
    CaptureResourceTimeline timeline;
    const auto a = timeline.ReserveAllocationId(),
               b = timeline.ReserveAllocationId(),
               c = timeline.ReserveAllocationId();
    const auto av = timeline.ReserveViewId(), bv = timeline.ReserveViewId(),
               cv = timeline.ReserveViewId();
    std::vector<CaptureResourceOperation> initialize;
    for (const auto &ids : { std::make_pair(a, av), std::make_pair(b, bv),
                             std::make_pair(c, cv) }) {
        initialize.push_back(SuffixOp(CaptureResourceOperationType::Allocate,
                                      ids.first, ids.second));
        auto alias = SuffixOp(CaptureResourceOperationType::Alias, ids.first,
                              ids.second);
        alias.aliases = { { ids.first, { 0, 16 } } };
        initialize.push_back(alias);
    }
    initialize.push_back(
        SuffixOp(CaptureResourceOperationType::FullWrite, a, av));
    initialize.push_back(
        SuffixOp(CaptureResourceOperationType::FullWrite, c, cv));
    auto baseline = std::make_shared<CaptureOccurrence>();
    baseline->event_id = 1;
    baseline->type = CaptureEventType::AllocationBoundary;
    baseline->finished = baseline->inputs.complete = true;
    baseline->pending = false;
    baseline->resource_evidence = timeline.Apply(1, initialize);
    auto evidence = timeline.ApplyBatch(
        { { 10,
            { SuffixOp(CaptureResourceOperationType::Read, a, av),
              SuffixOp(CaptureResourceOperationType::UncertainWrite, a, av) } },
          { 2,
            { SuffixOp(CaptureResourceOperationType::Read, a, av),
              SuffixOp(CaptureResourceOperationType::Read, b, bv,
                       XEMU_SHADER_CAPTURE_RESOURCE_TEXTURE),
              SuffixOp(CaptureResourceOperationType::UncertainWrite, b, bv,
                       XEMU_SHADER_CAPTURE_RESOURCE_TEXTURE) } },
          { 12,
            { SuffixOp(CaptureResourceOperationType::Read, b, bv,
                       XEMU_SHADER_CAPTURE_RESOURCE_TEXTURE),
              SuffixOp(CaptureResourceOperationType::Read, c, cv),
              SuffixOp(CaptureResourceOperationType::UncertainWrite, c,
                       cv) } } });
    CHECK(evidence.size() == 3);
    auto producer = std::make_shared<CaptureOccurrence>(*draws[0]);
    auto consumer = std::make_shared<CaptureOccurrence>(*draws[1]);
    auto copy = std::make_shared<CaptureOccurrence>();
    producer->event_id = 10;
    producer->resource_evidence = evidence[0];
    producer->inputs.before = { 2, 2, SuffixPixels(0) };
    producer->inputs.textures = {};
    copy->event_id = 2;
    copy->type = CaptureEventType::Copy;
    copy->finished = copy->inputs.complete = true;
    copy->pending = false;
    copy->resource_evidence = evidence[1];
    consumer->event_id = 12;
    consumer->resource_evidence = evidence[2];
    const uint8_t consumer_recipe[] = { 1, 2, 3, 5 };
    consumer->summary.shaders[0].hash = ComputeShaderHash(
        1, Stage::Pixel, 1, consumer_recipe, sizeof(consumer_recipe));
    auto recipe = std::make_shared<CaptureImmutableBlock>();
    recipe->id = 2001;
    recipe->bytes.assign(std::begin(consumer_recipe),
                         std::end(consumer_recipe));
    for (auto &blob : consumer->inputs.blobs)
        if (blob.name == "recipe.stage2")
            blob.data = recipe;
    consumer->inputs.before = { 2, 2, SuffixPixels(0) };
    auto source = std::make_shared<CaptureImmutableBlock>();
    source->id = 2000;
    source->bytes = { 'c', 'o', 'n', 's', 'u', 'm', 'e', 'r' };
    consumer->inputs.sources[2] = source;
    auto &texture = consumer->inputs.textures[0];
    texture.metadata.width = texture.metadata.height = 2;
    texture.images = { { 0, 0, { 2, 2, SuffixPixels(99) } } };
    auto color = SuffixBinding(XEMU_SHADER_CAPTURE_REPLAY_COLOR, false);
    color.checkpoint = 1;
    auto producer_description = std::make_shared<CaptureReplayDescription>();
    producer_description->kind = uint32_t(CaptureCommandKind::Draw);
    producer_description->bindings = {
        color, SuffixBinding(XEMU_SHADER_CAPTURE_REPLAY_COLOR, true)
    };
    producer->replay_description = producer_description;
    auto copy_description = std::make_shared<CaptureReplayDescription>();
    copy_description->kind = uint32_t(CaptureCommandKind::ImageCopy);
    copy_description->width = copy_description->height = 2;
    copy_description->bindings = {
        SuffixBinding(XEMU_SHADER_CAPTURE_REPLAY_COPY_SOURCE, false),
        SuffixBinding(XEMU_SHADER_CAPTURE_REPLAY_COPY_DESTINATION, false),
        SuffixBinding(XEMU_SHADER_CAPTURE_REPLAY_COPY_DESTINATION, true)
    };
    copy->replay_description = copy_description;
    auto consumer_description = std::make_shared<CaptureReplayDescription>();
    consumer_description->kind = uint32_t(CaptureCommandKind::Draw);
    auto texture_binding =
        SuffixBinding(XEMU_SHADER_CAPTURE_REPLAY_TEXTURE, false);
    texture_binding.checkpoint =
        1; // Old 99 must never replace a produced value.
    consumer_description->bindings = {
        texture_binding, color,
        SuffixBinding(XEMU_SHADER_CAPTURE_REPLAY_COLOR, true)
    };
    consumer->replay_description = consumer_description;
    snapshot.resource_domain = timeline.DomainId();
    // Observation ID and container order deliberately differ from execution.
    snapshot.events = { baseline, copy, producer, consumer };
    auto proof = BuildCaptureReplayPlan(snapshot, { 10 });
    if (proof.status != CaptureReplayStatus::Ready)
        std::cerr << "Suffix proof: " << proof.reason << "\n";
    CHECK(proof.status == CaptureReplayStatus::Ready);
    CHECK(proof.logical_closure_complete && proof.steps.size() == 3);
    return snapshot;
}
static CaptureComparisonBuildOutcome
SuffixPackets(const CaptureComparisonRequest &request,
              const CaptureOccurrence &event, PreviewPacket *original,
              PreviewPacket *replacement, std::string *error)
{
    CHECK(request.dependencies == CaptureComparisonDependencies::FrozenInputs);
    auto outcome = MockPackets(request, event, original, replacement, error);
    auto material = std::make_shared<PreviewCapturedMaterial>();
    material->limitations = 0;
    material->textures = event.CopyInputs().textures;
    auto pipeline = std::make_shared<PreviewCapturedPipeline>();
    pipeline->backend = request.selection.backend;
    // This CPU dependency fixture uses a legacy triangle-list pipeline.
    pipeline->host_topology =
        request.selection.backend == PreviewBackend::Vulkan ? 3 : 4;
    pipeline->vertex_count = 3;
    pipeline->ranges = { { 0, 3 } };
    pipeline->raster.available = 1023;
    pipeline->raster.width = pipeline->raster.height = 2;
    pipeline->color_before = { 2, 2, std::vector<uint8_t>(16) };
    for (auto *packet : { original, replacement }) {
        packet->captured_pipeline = pipeline;
        packet->captured_material = material;
        packet->pipeline_digest =
            ComputePreviewCapturedPipelineDigest(*pipeline);
        packet->pipeline_layout_digest =
            ComputePreviewCapturedPipelineDigest(*pipeline, true);
        packet->material_digest =
            ComputePreviewCapturedMaterialDigest(*material);
        CHECK(ValidatePreviewPacket(*packet, error));
    }
    return outcome;
}
static CaptureComparisonCompletion
SuffixCompletion(const CaptureComparisonJob &job, uint8_t value)
{
    auto result = Completion(job, value);
    if (job.expected_result.profile_draw) {
        result.draw_timing.result = job.expected_result;
        result.draw_timing.backend = job.packet->selection.backend;
        result.draw_timing.provenance =
            PreviewDrawTimingProvenance::ReplayInstrumented;
        result.draw_timing.status = PreviewDrawTimingStatus::Unsupported;
        result.draw_timing.message =
            "Injected controller images have no native GPU timestamps";
    }
    return result;
}
static void
test_producer_suffix_uses_independent_outputs_and_event_local_sources()
{
    auto recording = SuffixRecording();
    auto request = Request();
    request.dependencies = CaptureComparisonDependencies::ProducerSuffix;
    request.profile_draw = true;
    CaptureComparisonController controller;
    CHECK(controller.Start(request, recording));
    CHECK(controller.Snapshot().matched_events == 1);
    CaptureComparisonJob job;
    for (unsigned branch = 0; branch < 2; ++branch) {
        CHECK(controller.TryClaimJob(&job, SuffixPackets));
        CHECK(job.identity.event_id == 10);
        CHECK(job.edited_seed == bool(branch));
        CHECK(job.phase == (branch ? CaptureComparisonPhase::Replacement :
                                     CaptureComparisonPhase::Original));
        CHECK(job.packet->selection.shader == request.selection.shader);
        CHECK(job.packet->source == (branch ? request.edit.source : "pixel"));
        CHECK(job.packet->captured_pipeline->color_before.rgba[0] == 0);
        CHECK(controller.Complete(SuffixCompletion(job, branch ? 7 : 3)));
        CHECK(controller.TryClaimJob(&job, SuffixPackets));
        CHECK(job.identity.event_id == 12);
        CHECK(!job.edited_seed);
        CHECK(job.packet->selection.shader != request.selection.shader);
        CHECK(job.packet->source == "consumer");
        CHECK(job.packet->source_variant == PreviewSourceVariant::Original);
        CHECK(job.packet->draft_id == 0 && job.packet->replacement_id == 0);
        CHECK(job.packet->captured_material->textures[0]
                  .images[0]
                  .image.rgba[0] == (branch ? 7 : 3));
        const auto retained = controller.Snapshot().replay_value_bytes;
        auto wrong = SuffixCompletion(job, 99);
        ++wrong.result.input_revision;
        CHECK(!controller.Complete(std::move(wrong)));
        CHECK(controller.StillCurrent(job.identity, job.phase));
        CHECK(controller.Snapshot().replay_value_bytes == retained);
        CHECK(controller.Complete(SuffixCompletion(job, branch ? 11 : 5)));
    }
    CHECK(controller.Snapshot().state == CaptureComparisonState::Running);
    const auto running_revision = controller.Revision();
    CHECK(!controller.TryClaimJob(&job, SuffixPackets));
    CHECK(controller.Revision() > running_revision);
    auto result = controller.Snapshot();
    CHECK(result.state == CaptureComparisonState::Ready &&
          result.results.size() == 2);
    CHECK(result.results[0].original->rgba[0] == 3 &&
          result.results[0].replacement->rgba[0] == 7);
    CHECK(result.results[1].original->rgba[0] == 5 &&
          result.results[1].replacement->rgba[0] == 11);
    CHECK(result.results[1].difference.maximum_difference == 6);
    CHECK(result.results[0].edited_seed && !result.results[1].edited_seed);
    CHECK(result.replay_steps == 3 && result.replay_plan_bytes > 0);
    CHECK(result.original_timing.requested == 2 &&
          result.original_timing.unsupported == 2);
    CHECK(result.replacement_timing.requested == 2 &&
          result.replacement_timing.unsupported == 2);
    CHECK(result.original_timing.sample_count == 0 &&
          result.replacement_timing.sample_count == 0);
    for (const auto &row : result.results) {
        CHECK(row.original_timing.result == row.original_key);
        CHECK(row.replacement_timing.result == row.replacement_key);
        CHECK(row.original_key.input_revision !=
              row.replacement_key.input_revision);
    }
}

static void test_suffix_missing_proof_and_full_chain_budgets_are_terminal()
{
    auto recording = SuffixRecording();
    auto request = Request();
    request.dependencies = CaptureComparisonDependencies::ProducerSuffix;
    CaptureComparisonController controller;
    auto missing = recording;
    auto copy = std::make_shared<CaptureOccurrence>(*missing.events[1]);
    copy->replay_description.reset();
    missing.events[1] = copy;
    CHECK(controller.Start(request, missing));
    CHECK(controller.Snapshot().results[0].outcome ==
          CaptureComparisonOutcome::Unsupported);
    CHECK(controller.Snapshot().admitted_jobs == 0);
    CaptureComparisonJob job;
    CHECK(!controller.TryClaimJob(&job, SuffixPackets));
    CaptureComparisonSettings limits;
    limits.maximum_jobs = 1;
    CHECK(controller.Start(request, recording, limits));
    CHECK(controller.Snapshot().admitted_jobs == 0);
    CHECK(controller.Snapshot().results[0].outcome ==
          CaptureComparisonOutcome::Incomplete);
    limits = {};
    limits.maximum_rows = limits.maximum_jobs = 1;
    CHECK(controller.Start(request, recording, limits));
    CHECK(controller.Snapshot().results[0].outcome ==
          CaptureComparisonOutcome::Incomplete);
    limits = {};
    limits.packet_byte_budget = 1;
    CHECK(controller.Start(request, recording, limits));
    CHECK(controller.Snapshot().results[0].outcome ==
          CaptureComparisonOutcome::Incomplete);
    limits = {};
    // Checkpoint map entries require 4*512 bytes. Admit them, then refuse
    // the first produced version rather than silently using captured bytes.
    limits.image_byte_budget = 2048;
    CHECK(controller.Start(request, recording, limits));
    CHECK(controller.TryClaimJob(&job, SuffixPackets));
    CHECK(controller.Complete(SuffixCompletion(job, 3)));
    auto result = controller.Snapshot();
    CHECK(result.state == CaptureComparisonState::Failed);
    CHECK(result.outcomes[size_t(CaptureComparisonOutcome::Incomplete)] == 2);
    CHECK(!controller.TryClaimJob(&job, SuffixPackets));
    CHECK(result.message.find("bound") != std::string::npos);
}
static void test_suffix_failure_cannot_skip_a_producer_and_preserves_originals()
{
    auto recording = SuffixRecording();
    auto request = Request();
    request.dependencies = CaptureComparisonDependencies::ProducerSuffix;
    CaptureComparisonController controller;
    CHECK(controller.Start(request, recording));
    CaptureComparisonJob job;
    CHECK(controller.TryClaimJob(&job, SuffixPackets));
    CHECK(controller.Complete(SuffixCompletion(job, 3)));
    CHECK(controller.TryClaimJob(&job, SuffixPackets));
    CHECK(controller.Complete(SuffixCompletion(job, 5)));
    CHECK(controller.TryClaimJob(&job, SuffixPackets));
    CHECK(job.identity.event_id == 10 &&
          job.phase == CaptureComparisonPhase::Replacement);
    auto failed = SuffixCompletion(job, 0);
    failed.outcome = CaptureComparisonOutcome::CompileFailed;
    failed.message = "edited producer compiler diagnostic";
    failed.image = {};
    CHECK(controller.Complete(std::move(failed)));
    auto result = controller.Snapshot();
    CHECK(result.state == CaptureComparisonState::Failed);
    CHECK(result.results[0].outcome == CaptureComparisonOutcome::CompileFailed);
    CHECK(result.results[1].outcome == CaptureComparisonOutcome::Incomplete);
    CHECK(result.results[0].original->rgba[0] == 3 &&
          !result.results[0].replacement);
    CHECK(result.results[1].original->rgba[0] == 5 &&
          !result.results[1].replacement);
    CHECK(!controller.TryClaimJob(&job, SuffixPackets));
    CHECK(controller.Start(request, recording));
    auto throws = [](const CaptureComparisonRequest &,
                     const CaptureOccurrence &, PreviewPacket *,
                     PreviewPacket *,
                     std::string *) -> CaptureComparisonBuildOutcome {
        throw std::bad_alloc();
    };
    CHECK(!controller.TryClaimJob(&job, throws));
    result = controller.Snapshot();
    CHECK(result.state == CaptureComparisonState::Failed &&
          !result.message.empty());
    CHECK(result.outcomes[size_t(CaptureComparisonOutcome::Incomplete)] == 2);
}
static void test_suffix_cancel_supersession_and_event_local_contract()
{
    auto recording = SuffixRecording();
    auto request = Request();
    request.dependencies = CaptureComparisonDependencies::ProducerSuffix;
    CaptureComparisonController controller;
    CHECK(controller.Start(request, recording));
    CaptureComparisonJob old;
    CHECK(controller.TryClaimJob(&old, SuffixPackets));
    controller.Cancel();
    CHECK(controller.Snapshot().state == CaptureComparisonState::Cancelled);
    CHECK(!controller.Complete(SuffixCompletion(old, 99)));
    CHECK(controller.Start(request, recording));
    auto newer = request;
    ++newer.edit.revision;
    newer.edit.source += " new";
    auto supersede = [&](const auto &candidate, const auto &event,
                         auto *original, auto *replacement, auto *error) {
        CHECK(controller.Start(newer, recording));
        CaptureComparisonJob waiting;
        CHECK(!controller.TryClaimJob(&waiting, SuffixPackets));
        return SuffixPackets(candidate, event, original, replacement, error);
    };
    CaptureComparisonJob job;
    CHECK(!controller.TryClaimJob(&job, supersede));
    CHECK(controller.TryClaimJob(&job, SuffixPackets));
    CHECK(job.identity.request_id != old.identity.request_id);
    CHECK(!controller.Complete(SuffixCompletion(old, 99)));
    CHECK(controller.Complete(SuffixCompletion(job, 3)));
    auto retarget = [&](const auto &candidate, const auto &event,
                        auto *original, auto *replacement, auto *error) {
        auto outcome =
            SuffixPackets(candidate, event, original, replacement, error);
        original->selection.shader = request.selection.shader;
        return outcome;
    };
    CHECK(!controller.TryClaimJob(&job, retarget));
    auto result = controller.Snapshot();
    CHECK(result.state == CaptureComparisonState::Failed);
    CHECK(result.results[1].message.find("event-local") != std::string::npos);
    CHECK(result.results[0].original->rgba[0] == 3);
}

int main()
{
    test_producer_suffix_uses_independent_outputs_and_event_local_sources();
    test_suffix_missing_proof_and_full_chain_budgets_are_terminal();
    test_suffix_failure_cannot_skip_a_producer_and_preserves_originals();
    test_suffix_cancel_supersession_and_event_local_contract();
    test_every_use_has_distinct_result_and_exact_identity();
    test_latest_request_rejects_old_phase_and_source_revision();
    test_repeated_frozen_runs_require_fresh_result_and_timing_keys();
    test_coverage_includes_nonemitted_pending_and_producer_unsupported();
    test_recorded_command_without_accepted_submission_is_not_compared();
    test_budgets_report_every_matched_use_or_an_explicit_omission();
    test_compile_failure_keeps_baseline_and_advances_to_other_uses();
    test_scopes_do_not_retarget_capture_or_execution_identity();
    test_captured_extent_is_exact_and_never_clamped();
    test_default_adapter_uses_original_sources_raw_pipeline_and_native_extent();
    test_packet_pair_preserves_extent_and_frozen_inputs();
    test_supersession_during_packet_build_does_not_publish_old_packets();
    std::cout << "capture comparison tests passed\n";
}

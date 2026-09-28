#include "../../ui/xui/shader-browser-preview-service.hh"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>
#include <utility>

using namespace xemu::shader_browser;

#define CHECK(...)                                                   \
    do {                                                             \
        if (!(__VA_ARGS__)) {                                        \
            std::cerr << "Check failed at line " << __LINE__ << ": " \
                      << #__VA_ARGS__ << '\n';                       \
            std::exit(EXIT_FAILURE);                                 \
        }                                                            \
    } while (false)

static std::shared_ptr<PreviewPacket> Packet(uint64_t input_revision = 1,
                                             bool animated = false)
{
    auto packet = std::make_shared<PreviewPacket>();
    packet->selection.scope.title_id = 0x4d530064;
    packet->selection.scope.executable_fingerprint_version = 1;
    packet->selection.scope.executable_fingerprint[0] = 1;
    packet->selection.shader.stage = Stage::Pixel;
    packet->selection.session_epoch = 3;
    packet->selection.renderer_epoch = 5;
    packet->selection.backend = PreviewBackend::OpenGL;
    packet->selection.mode = PreviewMode::Normal;
    packet->recipe_format_version = 1;
    packet->recipe = { 1, 3, 3, 7 };
    packet->selection.shader.hash = ComputeShaderHash(
        1, Stage::Pixel, 1, packet->recipe.data(), packet->recipe.size());
    packet->generator_abi = 2;
    packet->interface_abi = 1;
    packet->input_revision = input_revision;
    packet->view_revision = 1;
    packet->width = 320;
    packet->height = 320;
    packet->update_policy = animated ? PreviewUpdatePolicy::Continuous :
                                      PreviewUpdatePolicy::OnDirty;
    packet->packet_kind = PreviewPacketKind::Synthetic;
    packet->replay_class = PreviewReplayClass::Synthetic;
    packet->fixture_digest = ComputePreviewDigest(
        packet->fixture_bytes.data(), packet->fixture_bytes.size());
    return packet;
}

static void EnableAndSelect(PreviewService *service, uint64_t now)
{
    auto packet = Packet();
    service->SetEnabled(true);
    service->SetVisible(true, now);
    service->SetSelection(packet->selection, now);
    std::string error;
    CHECK(service->SubmitPacket(*packet, now, &error));
}

static void Prepare(PreviewService *service, uint64_t now)
{
    service->SetGuestPaused(true);
    service->EditClock(PreviewClockAction::Play, 0, now);
    std::string error;
    CHECK(service->RequestPreparation(&error));
    PreviewWorkItem work{};
    CHECK(service->TryClaimWork(now + kPreviewSelectionDebounceNs, &work));
    CHECK(work.kind == PreviewWorkKind::Prepare);
    CHECK(service->CompletePreparation(work.token, true, "prepared", now));
}

static void TestCapturedMaterialLatestWins()
{
    constexpr uint64_t start = UINT64_C(1000000000);
    const uint64_t settled = start + kPreviewSelectionDebounceNs;
    PreviewService service;
    EnableAndSelect(&service, start);
    Prepare(&service, start);
    auto packet = Packet();
    packet->packet_kind = PreviewPacketKind::Replay;
    packet->replay_class = PreviewReplayClass::Approximate;
    packet->fixture_bytes = { 1 };
    packet->fixture_digest = ComputePreviewDigest(packet->fixture_bytes.data(),
                                                  packet->fixture_bytes.size());
    packet->captured_mesh.positions = { { -1, -1, 0, 1 },
                                        { 1, -1, 0, 1 },
                                        { 0, 1, 0, 1 } };
    packet->captured_mesh.indices = { 0, 1, 2 };
    packet->mesh_digest = ComputeCapturedMeshDigest(packet->captured_mesh);
    OwnedDrawInputs inputs;
    inputs.complete = true;
    inputs.uniforms.push_back(
        { 2, "alphaRef", XEMU_SHADER_DRAW_UNIFORM_INT, 1, 1, { 64, 0, 0, 0 } });
    packet->captured_material = BuildPreviewCapturedMaterial(inputs);
    packet->material_digest =
        ComputePreviewCapturedMaterialDigest(*packet->captured_material);
    std::string error;
    CHECK(service.SubmitPacket(*packet, settled, &error));
    PreviewWorkItem old_work;
    CHECK(service.TryClaimWork(settled, &old_work));
    CHECK(old_work.kind == PreviewWorkKind::Render);
    inputs.uniforms[0].data[0] = 128;
    packet->captured_material = BuildPreviewCapturedMaterial(inputs);
    packet->material_digest =
        ComputePreviewCapturedMaterialDigest(*packet->captured_material);
    CHECK(packet->material_digest != old_work.result_key.material_digest);
    CHECK(old_work.packet->captured_material->uniforms[0].data[0] == 64);
    CHECK(service.SubmitPacket(*packet, settled, &error));
    CHECK(service.CompleteRender(old_work.token, true, "obsolete material",
                                 settled));
    PreviewFrameRef frame;
    CHECK(!service.TryAcquireReadyFrame(&frame, settled));
    PreviewWorkItem latest;
    CHECK(service.TryClaimWork(settled + UINT64_C(100000000), &latest));
    CHECK(latest.kind == PreviewWorkKind::Render);
    CHECK(latest.result_key.material_digest == packet->material_digest);
    const auto description =
        DescribePreviewCapturedMaterial(*packet->captured_material) +
        "; 2 reflected uniforms unapplied";
    CHECK(service.CompleteRender(latest.token, true, description, settled));
    PreviewStatus status;
    service.CopyStatus(&status);
    CHECK(status.message.find("Partial captured material") !=
          std::string::npos);
    CHECK(service.TryAcquireReadyFrame(&frame, settled));
    service.SetVisible(true, settled + UINT64_C(200000000));
    CHECK(!service.TryClaimWork(settled + UINT64_C(200000000), &latest));
    service.CopyStatus(&status);
    CHECK(status.message == "Preview sample is already current");
    CHECK(status.material_fidelity == description);
}

static void TestLastGoodAndAutomaticPreparation()
{
    constexpr uint64_t start = UINT64_C(1000000000);
    const uint64_t settled = start + kPreviewSelectionDebounceNs;
    PreviewService service;
    EnableAndSelect(&service, start);
    service.SetGuestPaused(true);
    PreviewWorkItem work{};
    CHECK(!service.RequestAutomaticPreparation(settled - 1));
    CHECK(!service.TryClaimWork(settled - 1, &work));
    // No explicit Prepare click is needed after paused selection settles.
    CHECK(service.RequestAutomaticPreparation(settled));
    CHECK(!service.RequestAutomaticPreparation(settled));
    CHECK(service.TryClaimWork(settled, &work));
    CHECK(work.kind == PreviewWorkKind::Prepare);
    CHECK(service.CompletePreparation(work.token, true, "prepared", settled));
    CHECK(service.TryClaimWork(settled, &work));
    CHECK(service.CompleteRender(work.token, true, "good", settled));
    PreviewFrameRef original{};
    CHECK(service.TryAcquireReadyFrame(&original, settled));

    auto replacement = Packet();
    replacement->selection.mode = PreviewMode::Replacement;
    replacement->replacement_id = 7;
    replacement->replacement_revision = 2;
    replacement->source = "invalid shader";
    replacement->source_digest = ComputePreviewDigest(
        reinterpret_cast<const uint8_t *>(replacement->source.data()),
        replacement->source.size());
    service.SetRequestedMode(PreviewMode::Replacement);
    service.SetSelection(replacement->selection, settled);
    std::string error;
    CHECK(service.SubmitPacket(*replacement, settled, &error));
    // A mode change must not retire a frame still sampled by the HUD.
    CHECK(!service.CompleteDisplayRetirement(original.slot,
                                             original.slot_generation));
    service.SetVisible(true, settled + kPreviewSelectionDebounceNs);
    CHECK(service.RequestAutomaticPreparation(settled +
                                              kPreviewSelectionDebounceNs));
    CHECK(service.TryClaimWork(settled + kPreviewSelectionDebounceNs, &work));
    CHECK(service.CompletePreparation(work.token, false, "bad replacement",
                                      settled));
    CHECK(!service.TryClaimWork(settled + kPreviewSelectionDebounceNs, &work));
    PreviewStatus status;
    service.CopyStatus(&status);
    CHECK(status.state == PreviewState::Failed);
    CHECK(status.message == "bad replacement");
    CHECK(status.leased_slots == 1 &&
          status.free_slots == kPreviewSlotCount - 1);
    CHECK(status.has_attempt &&
          status.attempted_compile.replacement_revision == 2);
    CHECK(!service.RequestPreparation(&error)); // No failed-compile retry loop.
    ++replacement->replacement_revision;
    CHECK(service.SubmitPacket(*replacement, settled, &error));
    CHECK(service.RequestAutomaticPreparation(settled +
                                              kPreviewSelectionDebounceNs));
    CHECK(service.TryClaimWork(settled + kPreviewSelectionDebounceNs, &work));
    CHECK(service.CompletePreparation(work.token, true, "fixed", settled));
    CHECK(service.TryClaimWork(settled + kPreviewSelectionDebounceNs, &work));
    CHECK(service.CompleteRender(work.token, true, "new Current", settled));
    PreviewFrameRef current{};
    CHECK(service.TryAcquireReadyFrame(&current, settled));
    // Original can be Frozen while the new Current owns a different slot.
    CHECK(current.slot != original.slot);
    CHECK(!service.CompleteDisplayRetirement(original.slot,
                                             original.slot_generation));
    CHECK(!service.CompleteDisplayRetirement(current.slot,
                                             current.slot_generation));
    service.RequestCurrentFrame();
    service.SetVisible(true, settled + 2 * kPreviewSelectionDebounceNs);
    CHECK(
        service.TryClaimWork(settled + 2 * kPreviewSelectionDebounceNs, &work));
    CHECK(service.CompleteRender(work.token, false, "render failure", settled));
    service.CopyStatus(&status);
    CHECK(status.leased_slots == 2 &&
          status.free_slots == kPreviewSlotCount - 2);
    CHECK(!service.TryClaimWork(settled + 2 * kPreviewSelectionDebounceNs,
                                &work));
    service.CopyStatus(&status);
    CHECK(status.message == "render failure");
    ++replacement->input_revision;
    CHECK(service.SubmitPacket(*replacement, settled, &error));
    CHECK(
        service.TryClaimWork(settled + 2 * kPreviewSelectionDebounceNs, &work));
    const auto obsolete_token = work.token;
    PreviewCompileKey attempted = BuildPreviewCompileKey(*replacement);
    ++attempted.replacement_revision;
    service.ReportInputFailure(attempted, "incompatible replacement");
    CHECK(service.CompleteRender(obsolete_token, true, "obsolete", settled));
    service.CopyStatus(&status);
    CHECK(status.message == "incompatible replacement");
    CHECK(status.ready_slots == 0 && status.leased_slots == 2);
    CHECK(!service.TryAcquireReadyFrame(&current, settled));
    // The delayed consumer fence keeps its slot unavailable until explicit
    // proof.
    CHECK(service.ReleaseDisplayLease(original.slot, original.slot_generation));
    service.CopyStatus(&status);
    CHECK(status.free_slots == kPreviewSlotCount - 2);
    CHECK(service.CompleteDisplayRetirement(original.slot,
                                            original.slot_generation));
    service.SetVisible(false, settled);
    CHECK(service.CompleteDisplayRetirement(current.slot,
                                            current.slot_generation));
    service.CopyStatus(&status);
    CHECK(status.free_slots == kPreviewSlotCount);
    service.BackendDestroyed();
    CHECK(!service.ReleaseDisplayLease(current.slot, current.slot_generation));
}

static void TestEditedRevisionKeepsDisplayedSource()
{
    constexpr uint64_t start = UINT64_C(3000000000);
    PreviewService service;
    auto edited = Packet();
    edited->source_variant = PreviewSourceVariant::Edited;
    edited->draft_id = 91;
    edited->draft_revision = 1;
    edited->draft_submission_id = 1;
    edited->source = "valid draft";
    edited->source_digest = ComputePreviewDigest(
        reinterpret_cast<const uint8_t *>(edited->source.data()),
        edited->source.size());
    service.SetEnabled(true);
    service.SetVisible(true, start);
    service.SetGuestPaused(true);
    service.SetSelection(edited->selection, start);
    std::string error;
    CHECK(service.SubmitPacket(*edited, start, &error));
    CHECK(service.RequestPreparation(&error));
    PreviewWorkItem work{};
    CHECK(service.TryClaimWork(start + kPreviewSelectionDebounceNs, &work));
    CHECK(service.CompletePreparation(work.token, true, "compiled", start));
    CHECK(service.TryClaimWork(start + kPreviewSelectionDebounceNs, &work));
    CHECK(service.CompleteRender(work.token, true, "display", start));
    PreviewFrameRef frame{};
    CHECK(service.TryAcquireReadyFrame(&frame, start));
    PreviewStatus status{};
    service.CopyStatus(&status);
    CHECK(status.has_displayed_source);
    CHECK(status.displayed_compile.draft_revision == 1);
    edited->draft_revision = 2;
    edited->draft_submission_id = 2;
    edited->source = "invalid draft";
    edited->source_digest = ComputePreviewDigest(
        reinterpret_cast<const uint8_t *>(edited->source.data()),
        edited->source.size());
    CHECK(service.SubmitPacket(*edited, start, &error));
    CHECK(service.RequestPreparation(&error));
    CHECK(service.TryClaimWork(start + kPreviewSelectionDebounceNs, &work));
    CHECK(
        service.CompletePreparation(work.token, false, "0:2: invalid", start));
    service.CopyStatus(&status);
    CHECK(status.attempted_compile.draft_revision == 2);
    CHECK(status.displayed_compile.draft_revision == 1);
    CHECK(status.has_displayed_source);
    CHECK(status.state == PreviewState::Failed);
    const uint64_t failed_token = work.token;
    CHECK(service.SubmitPacket(*edited, start, &error));
    CHECK(!service.RequestPreparation(&error));
    edited->draft_submission_id = 3;
    CHECK(service.SubmitPacket(*edited, start, &error));
    CHECK(service.RequestPreparation(&error));
    CHECK(service.TryClaimWork(start + kPreviewSelectionDebounceNs, &work));
    CHECK(work.kind == PreviewWorkKind::Prepare);
    CHECK(work.compile_key.draft_revision == 2);
    CHECK(work.compile_key.draft_submission_id == 3);
    CHECK(!service.CompletePreparation(failed_token, true, "late", start));
    CHECK(
        service.CompletePreparation(work.token, true, "retry compiled", start));
    CHECK(service.TryClaimWork(start + kPreviewSelectionDebounceNs, &work));
    CHECK(service.CompleteRender(work.token, true, "retry display", start));
    service.CopyStatus(&status);
    CHECK(status.attempted_compile.draft_submission_id == 3);
    CHECK(status.displayed_compile.draft_revision == 1);
    PreviewFrameRef retried{};
    CHECK(service.TryAcquireReadyFrame(&retried, start));
    service.CopyStatus(&status);
    CHECK(status.displayed_compile.draft_submission_id == 3);
}

static void TestOwnedOfflinePreparationWithoutGuestPause()
{
    constexpr uint64_t start = UINT64_C(1000000000);
    const uint64_t settled = start + kPreviewSelectionDebounceNs;
    PreviewService service;
    EnableAndSelect(&service, start);
    PreviewWorkItem work{};
    CHECK(!service.RequestAutomaticPreparation(settled));
    service.SetOfflineNoGuest(true);
    CHECK(service.RequestAutomaticPreparation(settled));
    CHECK(service.TryClaimWork(settled, &work));
    CHECK(work.kind == PreviewWorkKind::Prepare);
    CHECK(service.CompletePreparation(work.token, true, "offline", settled));
    CHECK(service.TryClaimWork(settled, &work));
    CHECK(work.kind == PreviewWorkKind::Render);
    CHECK(service.CompleteRender(work.token, true, "offline frame", settled));
    service.SetOfflineNoGuest(false);
    service.RequestCurrentFrame();
    CHECK(!service.TryClaimWork(settled + kPreviewPausedIntervalNs, &work));
}

static void TestPreparationStopsWhenInputsOrPauseChange()
{
    constexpr uint64_t start = UINT64_C(1000000000);
    PreviewService service;
    EnableAndSelect(&service, start);
    service.SetGuestPaused(true);
    std::string error;
    CHECK(service.RequestPreparation(&error));
    PreviewWorkItem work{};
    CHECK(service.TryClaimWork(start + kPreviewSelectionDebounceNs, &work));
    CHECK(service.PreparationStillAllowed(work.token));
    service.SetGuestPaused(false);
    CHECK(!service.PreparationStillAllowed(work.token));
    CHECK(service.CompletePreparation(
        work.token, PreviewPreparationOutcome::Cancelled, "cancelled", start));
    PreviewStatus status{};
    service.CopyStatus(&status);
    CHECK(status.state == PreviewState::NeedsPreparation &&
          !status.preparation_requested && !status.work_active);
    service.SetGuestPaused(true);
    CHECK(service.RequestAutomaticPreparation(start +
                                              kPreviewSelectionDebounceNs));
    CHECK(service.TryClaimWork(start + kPreviewSelectionDebounceNs, &work));
    CHECK(service.PreparationStillAllowed(work.token));
    service.ClearSelection();
    CHECK(!service.PreparationStillAllowed(work.token));
    CHECK(service.CompletePreparation(
        work.token, PreviewPreparationOutcome::Cancelled, "obsolete", start));
    service.CopyStatus(&status);
    CHECK(status.state == PreviewState::NoSelection);
}

static void TestDrawTimingOwnership()
{
    constexpr uint64_t start = UINT64_C(1000000000);
    const uint64_t settled = start + kPreviewSelectionDebounceNs;
    PreviewService service;
    EnableAndSelect(&service, start);
    Prepare(&service, start);
    auto packet = Packet();
    packet->profile_draw = true;
    std::string error;
    CHECK(service.SubmitPacket(*packet, settled, &error));
    PreviewWorkItem work;
    CHECK(service.TryClaimWork(settled, &work));
    CHECK(work.kind == PreviewWorkKind::Render);
    PreviewDrawTiming pending;
    pending.result = work.result_key;
    pending.backend = PreviewBackend::OpenGL;
    pending.provenance =
        PreviewDrawTimingProvenance::SelectedPreviewInstrumented;
    pending.status = PreviewDrawTimingStatus::Pending;
    CHECK(service.CompleteRender(work.token, true, "ready", settled, &pending));
    PreviewDrawTiming copied;
    CHECK(service.CopyDrawTiming(work.result_key, &copied));
    CHECK(copied.status == PreviewDrawTimingStatus::Pending);
    auto measured = pending;
    measured.status = PreviewDrawTimingStatus::Measured;
    measured.nanoseconds = 1234;
    measured.timestamp_valid_bits = 64;
    measured.timestamp_period_ns = 1;
    measured.actual_draw_commands = 1;
    auto wrong = measured;
    ++wrong.result.input_revision;
    CHECK(!service.CompleteDrawTiming(work, wrong));
    auto stale = work;
    ++stale.token;
    CHECK(!service.CompleteDrawTiming(stale, measured));
    ++stale.slot_generation;
    CHECK(!service.CompleteDrawTiming(stale, measured));
    CHECK(service.CompleteDrawTiming(work, measured));
    CHECK(!service.CompleteDrawTiming(work, measured));
    PreviewFrameRef frame;
    CHECK(service.TryAcquireReadyFrame(&frame, settled));
    PreviewStatus status;
    service.CopyStatus(&status);
    CHECK(status.draw_timing.nanoseconds == 1234 &&
          status.draw_timing.result == frame.result_key);
    CHECK(service.ReleaseDisplayLease(frame.slot, frame.slot_generation));
    CHECK(service.CompleteDisplayRetirement(frame.slot, frame.slot_generation));
    CHECK(!service.CopyDrawTiming(frame.result_key, &copied));
    CHECK(!service.CompleteDrawTiming(work, measured));
}

static void TestDrawTimingToggleAndSupersession()
{
    constexpr uint64_t start = UINT64_C(1000000000);
    const uint64_t settled = start + kPreviewSelectionDebounceNs;
    PreviewService service;
    EnableAndSelect(&service, start);
    Prepare(&service, start);
    PreviewWorkItem disarmed;
    CHECK(service.TryClaimWork(settled, &disarmed));
    CHECK(service.CompleteRender(disarmed.token, true, "ready", settled));
    PreviewFrameRef disarmed_frame;
    CHECK(service.TryAcquireReadyFrame(&disarmed_frame, settled));
    auto packet = Packet();
    packet->profile_draw = true;
    std::string error;
    CHECK(service.SubmitPacket(*packet, settled + 1, &error));
    PreviewWorkItem instrumented;
    CHECK(service.TryClaimWork(settled + 1, &instrumented));
    CHECK(instrumented.kind == PreviewWorkKind::Render &&
          instrumented.compile_key == disarmed.compile_key);
    CHECK(
        service.CompleteRender(instrumented.token, true, "ready", settled + 1));
    PreviewDrawTiming measured;
    measured.result = instrumented.result_key;
    measured.backend = PreviewBackend::OpenGL;
    measured.provenance =
        PreviewDrawTimingProvenance::SelectedPreviewInstrumented;
    measured.status = PreviewDrawTimingStatus::Measured;
    measured.nanoseconds = 1234;
    measured.timestamp_valid_bits = 64;
    measured.timestamp_period_ns = 1;
    measured.actual_draw_commands = 1;
    PreviewFrameRef frame;
    CHECK(service.TryAcquireReadyFrame(&frame, settled + 1));
    // Resubmitting the same result leaves the old image leased. Its late
    // timestamp still belongs to the old request and must be discarded.
    CHECK(service.SubmitPacket(*packet, settled + 2, &error));
    CHECK(!service.CompleteDrawTiming(instrumented, measured));
    PreviewDrawTiming copied;
    CHECK(service.CopyDrawTiming(instrumented.result_key, &copied));
    CHECK(copied.status == PreviewDrawTimingStatus::Pending);
    CHECK(service.ReleaseDisplayLease(frame.slot, frame.slot_generation));
    CHECK(service.CompleteDisplayRetirement(frame.slot, frame.slot_generation));
    CHECK(service.ReleaseDisplayLease(disarmed_frame.slot,
                                      disarmed_frame.slot_generation));
    CHECK(service.CompleteDisplayRetirement(disarmed_frame.slot,
                                            disarmed_frame.slot_generation));
}

static void TestHealthyRunningCadenceWithImmutableInputs()
{
    constexpr uint64_t start = UINT64_C(1000000000);
    constexpr uint64_t second = UINT64_C(1000000000);
    constexpr uint64_t polls[] = { 700000, 1300000, 900000, 1100000 };
    for (auto kind :
         { PreviewPacketKind::Synthetic, PreviewPacketKind::Replay }) {
        for (auto pressure :
             { PreviewPressure::Normal, PreviewPressure::Elevated,
               PreviewPressure::High }) {
            for (bool jittered : { false, true }) {
                auto packet = Packet(73, true);
                if (kind == PreviewPacketKind::Replay) {
                    packet->packet_kind = kind;
                    packet->replay_class = PreviewReplayClass::Approximate;
                    packet->fixture_bytes = { 1 };
                    packet->fixture_digest =
                        ComputePreviewDigest(packet->fixture_bytes.data(),
                                             packet->fixture_bytes.size());
                    packet->captured_mesh.positions = { { -1, -1, 0, 1 },
                                                        { 1, -1, 0, 1 },
                                                        { 0, 1, 0, 1 } };
                    packet->captured_mesh.indices = { 0, 1, 2 };
                    packet->mesh_digest =
                        ComputeCapturedMeshDigest(packet->captured_mesh);
                }
                const auto frozen = BuildPreviewResultKey(*packet);
                PreviewService service;
                service.SetEnabled(true);
                service.SetVisible(true, start);
                service.SetSelection(packet->selection, start);
                std::string error;
                CHECK(service.SubmitPacket(*packet, start, &error));
                Prepare(&service, start);
                service.SetGuestPaused(false);
                service.SetOfflineNoGuest(false);
                const uint64_t settled = start + kPreviewSelectionDebounceNs;
                std::shared_ptr<const PreviewPacket> admitted_packet;
                uint32_t admissions = 0;
                uint64_t previous_clock_revision = 0;
                size_t poll = 0;
                for (uint64_t elapsed = 0; elapsed < 10 * second;
                     elapsed +=
                     jittered ? polls[poll++ % 4] : UINT64_C(1000000)) {
                    const auto now = settled + elapsed;
                    service.SetVisible(true, now);
                    service.UpdateHealth({ now, pressure, true });
                    PreviewWorkItem work;
                    if (!service.TryClaimWork(now, &work))
                        continue;
                    CHECK(work.kind == PreviewWorkKind::Render);
                    CHECK(work.result_key.compile == frozen.compile);
                    CHECK(work.result_key.input_revision ==
                          frozen.input_revision);
                    CHECK(work.result_key.mesh_digest == frozen.mesh_digest);
                    CHECK(work.result_key.clock_revision >
                          previous_clock_revision);
                    if (admitted_packet)
                        CHECK(work.packet == admitted_packet);
                    else
                        admitted_packet = work.packet;
                    previous_clock_revision = work.result_key.clock_revision;
                    ++admissions;
                    // Simulated completion tests scheduler admission, not GPU
                    // FPS.
                    CHECK(service.CompleteRender(work.token, true,
                                                 "cadence unit", now));
                    PreviewFrameRef frame;
                    CHECK(service.TryAcquireReadyFrame(&frame, now));
                    CHECK(service.ReleaseDisplayLease(frame.slot,
                                                      frame.slot_generation));
                    CHECK(service.CompleteDisplayRetirement(
                        frame.slot, frame.slot_generation));
                }
                std::cout << "Running "
                          << (kind == PreviewPacketKind::Replay ? "Replay" :
                                                                  "Synthetic")
                          << " pressure " << unsigned(pressure) << ": "
                          << admissions << " admissions in ten seconds, "
                          << (jittered ? "jittered" : "regular") << " polls\n";
                const uint32_t expected =
                    pressure == PreviewPressure::Normal ? 600 : 300;
                CHECK(admissions >= expected - 1);
                CHECK(admissions <= expected + 1);
                PreviewStatus status;
                service.CopyStatus(&status);
                CHECK(!status.guest_paused && status.prepared);
                CHECK(status.update_hz ==
                      (pressure == PreviewPressure::Normal ? 60 : 30));
                CHECK(status.dropped_no_slot == 0 &&
                      status.dropped_stale_health == 0);
                // A late poll admits one current sample, without a backlog.
                const auto late = settled + 20 * second;
                service.SetVisible(true, late);
                service.UpdateHealth({ late, pressure, true });
                PreviewWorkItem work;
                CHECK(service.TryClaimWork(late, &work));
                CHECK(work.packet == admitted_packet);
                CHECK(work.compile_key == frozen.compile);
                const auto token = work.token;
                PreviewWorkItem extra;
                CHECK(!service.TryClaimWork(late, &extra));
                CHECK(service.CompleteRender(token, true, "late sample", late));
                PreviewFrameRef frame;
                CHECK(service.TryAcquireReadyFrame(&frame, late));
                CHECK(service.ReleaseDisplayLease(frame.slot,
                                                  frame.slot_generation));
                CHECK(service.CompleteDisplayRetirement(frame.slot,
                                                        frame.slot_generation));
                CHECK(!service.TryClaimWork(late + 1, &extra));
                // A brief health freeze and a fresh input revision cannot
                // bypass the running admission budget or accrue frozen time.
                service.UpdateHealth({ late + 2, pressure, false });
                CHECK(!service.TryClaimWork(late + 2, &extra));
                service.CopyStatus(&status);
                const auto frozen_time = status.clock.time_seconds;
                auto changed_inputs = *packet;
                ++changed_inputs.input_revision;
                const auto resumed = late + 3;
                CHECK(service.SubmitPacket(changed_inputs, resumed, &error));
                service.UpdateHealth({ resumed, pressure, true });
                CHECK(!service.TryClaimWork(resumed, &extra));
                const auto interval = pressure == PreviewPressure::Normal ?
                                          kPreviewNormalIntervalNs :
                                          kPreviewHighIntervalNs;
                CHECK(!service.TryClaimWork(resumed + interval / 2, &extra));
                service.CopyStatus(&status);
                CHECK(status.clock.time_seconds == frozen_time);
                CHECK(service.TryClaimWork(resumed + interval, &extra));
                CHECK(extra.result_key.compile == frozen.compile);
                CHECK(extra.result_key.input_revision ==
                      changed_inputs.input_revision);
                CHECK(service.CompleteRender(extra.token, true, "resumed",
                                             resumed + interval));
            }
        }
    }
}

int main()
{
    TestHealthyRunningCadenceWithImmutableInputs();
    TestDrawTimingOwnership();
    TestDrawTimingToggleAndSupersession();
    {
        PreviewService restored;
        const uint64_t now = UINT64_C(1000000000);
        EnableAndSelect(&restored, now);
        Prepare(&restored, now);
        PreviewScene moved = Packet()->scene;
        moved.yaw += 0.5f;
        restored.EditScene(moved);
        restored.UsePacketView();
        PreviewWorkItem captured;
        CHECK(restored.TryClaimWork(now + kPreviewSelectionDebounceNs,
                                    &captured));
        CHECK(captured.result_key == BuildPreviewResultKey(*Packet()));
    }
    TestCapturedMaterialLatestWins();
    TestPreparationStopsWhenInputsOrPauseChange();
    TestLastGoodAndAutomaticPreparation();
    TestEditedRevisionKeepsDisplayedSource();
    TestOwnedOfflinePreparationWithoutGuestPause();
    constexpr uint64_t t0 = UINT64_C(1000000000);
    PreviewService service;
    PreviewWorkItem work{};
    PreviewStatus status{};

    // Disabled state never yields work.
    auto disabled_packet = Packet();
    service.SetSelection(disabled_packet->selection, t0);
    std::string error;
    CHECK(service.SubmitPacket(*disabled_packet, t0, &error));
    CHECK(!service.TryClaimWork(t0 + kPreviewSelectionDebounceNs, &work));

    // The service must own a copy that a caller cannot change after admission.
    disabled_packet->input_revision = 99;
    service.SetEnabled(true);
    service.SetVisible(true, t0);
    service.SetGuestPaused(true);
    CHECK(service.RequestPreparation(&error));
    CHECK(service.TryClaimWork(t0 + kPreviewSelectionDebounceNs, &work));
    CHECK(work.packet->input_revision == 1);
    CHECK(service.CompletePreparation(work.token, false, "cancelled", t0));

    // Unsupported source interfaces stay unsupported for input-only edits.
    service.ResetForTest();
    EnableAndSelect(&service, t0);
    service.SetGuestPaused(true);
    CHECK(service.RequestPreparation(&error));
    CHECK(service.TryClaimWork(t0 + kPreviewSelectionDebounceNs, &work));
    CHECK(service.CompletePreparation(work.token,
                                      PreviewPreparationOutcome::Unsupported,
                                      "Unsupported samplerCube input", t0));
    service.CopyStatus(&status);
    CHECK(status.state == PreviewState::Unsupported);
    CHECK(!service.TryClaimWork(t0 + kPreviewSelectionDebounceNs + 1, &work));
    service.CopyStatus(&status);
    CHECK(status.state == PreviewState::Unsupported);
    PreviewPacket unsupported_edit = *Packet(2);
    CHECK(service.SubmitPacket(unsupported_edit, t0, &error));
    service.CopyStatus(&status);
    CHECK(status.state == PreviewState::Unsupported);
    PreviewPacket different_source = *Packet(3);
    ++different_source.generator_abi;
    CHECK(service.SubmitPacket(different_source, t0, &error));
    service.CopyStatus(&status);
    CHECK(status.state == PreviewState::NeedsPreparation);

    // Moving a packet preserves reserved storage, so admission must account
    // for its capacity rather than its much smaller logical payload.
    PreviewPacket reserved = *Packet();
    reserved.fixture_bytes.reserve(kPreviewMaxOwnedPacketBytes + 1);
    CHECK(!service.SubmitPacket(std::move(reserved), t0, &error));
    CHECK(error.find("32 MiB") != std::string::npos);

    // Preparation is explicit and pause-gated.
    service.ResetForTest();
    EnableAndSelect(&service, t0);
    CHECK(!service.RequestPreparation(&error));
    CHECK(error.find("paused") != std::string::npos);
    service.SetGuestPaused(true);
    CHECK(service.RequestPreparation(&error));
    CHECK(!service.TryClaimWork(t0 + kPreviewSelectionDebounceNs, &work,
                                PreviewBackend::Vulkan));
    service.CopyStatus(&status);
    CHECK(status.preparation_requested);
    CHECK(service.TryClaimWork(t0 + kPreviewSelectionDebounceNs, &work,
                               PreviewBackend::OpenGL));
    CHECK(work.kind == PreviewWorkKind::Prepare);

    // A/B/C newest-only behavior: completion for A cannot publish after C.
    auto packet_b = Packet(2);
    auto packet_c = Packet(3);
    CHECK(service.SubmitPacket(*packet_b, t0 + 1, &error));
    CHECK(service.SubmitPacket(*packet_c, t0 + 2, &error));
    CHECK(service.CompletePreparation(work.token, true, "old", t0));
    service.CopyStatus(&status);
    CHECK(!status.prepared);
    CHECK(status.superseded_requests >= 2);
    CHECK(status.state == PreviewState::NeedsPreparation);
    CHECK(service.RequestPreparation(&error));
    CHECK(service.TryClaimWork(t0 + kPreviewSelectionDebounceNs + 1, &work));
    CHECK(work.packet->input_revision == 3);
    CHECK(service.CompletePreparation(work.token, true, "current", t0));
    service.CopyStatus(&status);
    CHECK(status.prepared);

    // A prepared static packet renders once until its result key changes.
    service.SetGuestPaused(false);
    service.UpdateHealth(PreviewHealth{t0 + kPreviewSelectionDebounceNs + 2,
                                      PreviewPressure::Normal, true});
    const uint64_t first_render_time = t0 + kPreviewSelectionDebounceNs + 3;
    CHECK(service.TryClaimWork(first_render_time, &work));
    CHECK(work.kind == PreviewWorkKind::Render);
    CHECK(service.CompleteRender(work.token, true, "ready", t0));
    CHECK(!service.TryClaimWork(first_render_time + kPreviewNormalIntervalNs,
                                &work));

    PreviewFrameRef frame{};
    CHECK(service.TryAcquireReadyFrame(&frame, t0));
    CHECK(frame.slot < kPreviewSlotCount);
    service.ReleaseDisplayLease(frame.slot, frame.slot_generation);
    CHECK(!service.CompleteDisplayRetirement(frame.slot,
                                             frame.slot_generation + 1));
    CHECK(service.CompleteDisplayRetirement(frame.slot, frame.slot_generation));

    // Input-only changes reuse the prepared compile identity.
    auto input_update = Packet(4);
    CHECK(service.SubmitPacket(*input_update, first_render_time + 1, &error));
    service.CopyStatus(&status);
    CHECK(status.prepared);
    service.SetVisible(true, first_render_time + kPreviewNormalIntervalNs);
    CHECK(service.TryClaimWork(first_render_time + kPreviewNormalIntervalNs,
                               &work));
    CHECK(work.kind == PreviewWorkKind::Render);
    CHECK(service.CompleteRender(work.token, true, "input update", t0));

    // Retirement of the only static output permits a replacement render.
    CHECK(service.TryAcquireReadyFrame(&frame, t0));
    CHECK(service.ReleaseDisplayLease(frame.slot, frame.slot_generation));
    CHECK(service.CompleteDisplayRetirement(frame.slot, frame.slot_generation));
    const uint64_t retry_time =
        first_render_time + 2 * kPreviewNormalIntervalNs;
    service.SetVisible(true, retry_time);
    service.UpdateHealth(PreviewHealth{retry_time, PreviewPressure::Normal,
                                       true});
    CHECK(service.TryClaimWork(retry_time, &work));
    CHECK(service.CompleteRender(work.token, false, "cancelled", retry_time));

    // Animated work is rate-limited, freezes under critical pressure, and
    // stale health never admits running work.
    service.ResetForTest();
    auto animated = Packet(1, true);
    service.SetEnabled(true);
    service.SetVisible(true, t0);
    service.SetSelection(animated->selection, t0);
    CHECK(service.SubmitPacket(*animated, t0, &error));
    Prepare(&service, t0);
    service.SetGuestPaused(false);
    uint64_t now = t0 + kPreviewSelectionDebounceNs + 10;
    service.UpdateHealth(PreviewHealth{now, PreviewPressure::Normal, true});
    CHECK(service.TryClaimWork(now, &work));
    CHECK(service.CompleteRender(work.token, true, "frame 1", t0));
    CHECK(!service.TryClaimWork(now + kPreviewNormalIntervalNs - 1, &work));
    CHECK(service.TryClaimWork(now + kPreviewNormalIntervalNs, &work));
    CHECK(service.CompleteRender(work.token, true, "frame 2", t0));

    service.UpdateHealth(PreviewHealth{
        now + kPreviewNormalIntervalNs + 1,
        PreviewPressure::Critical, true});
    service.SetVisible(true, now + 2 * kPreviewNormalIntervalNs);
    CHECK(!service.TryClaimWork(now + 2 * kPreviewNormalIntervalNs, &work));
    service.CopyStatus(&status);
    CHECK(status.state == PreviewState::Frozen);

    service.UpdateHealth(PreviewHealth{
        now + 2 * kPreviewNormalIntervalNs,
        PreviewPressure::Normal, true});
    service.SetVisible(true, now + 2 * kPreviewNormalIntervalNs + 1);
    CHECK(!service.TryClaimWork(now + 2 * kPreviewNormalIntervalNs + 1, &work));
    service.UpdateHealth(PreviewHealth{
        now + 2 * kPreviewNormalIntervalNs + kPreviewPressureRecoveryNs,
        PreviewPressure::Normal, true});
    service.SetVisible(
        true, now + 2 * kPreviewNormalIntervalNs + kPreviewPressureRecoveryNs);
    CHECK(!service.TryClaimWork(now + 2 * kPreviewNormalIntervalNs +
                                    kPreviewPressureRecoveryNs,
                                &work));
    // Recovery anchors time. Intermediate worker polls must not create a
    // result identity before the first complete cadence interval elapses.
    CHECK(!service.TryClaimWork(now + 2 * kPreviewNormalIntervalNs +
                                    kPreviewPressureRecoveryNs +
                                    kPreviewNormalIntervalNs / 2,
                                &work));
    CHECK(service.TryClaimWork(now + 3 * kPreviewNormalIntervalNs +
                                   kPreviewPressureRecoveryNs,
                               &work));
    CHECK(service.CompleteRender(work.token, true, "recovered", t0));

    const uint64_t stale_now =
        now + 2 * kPreviewNormalIntervalNs + kPreviewPressureRecoveryNs +
        kPreviewHealthStaleNs + 1;
    service.SetVisible(true, stale_now);
    CHECK(!service.TryClaimWork(stale_now, &work));

    // Acquiring the newest completed frame immediately reclaims older Ready
    // frames that were never sampled and therefore have no consumer lease.
    service.ResetForTest();
    animated = Packet(1, true);
    service.SetEnabled(true);
    service.SetVisible(true, t0);
    service.SetSelection(animated->selection, t0);
    CHECK(service.SubmitPacket(*animated, t0, &error));
    Prepare(&service, t0);
    service.SetGuestPaused(true);
    for (size_t i = 0; i < kPreviewSlotCount; ++i) {
        const uint64_t render_time =
            t0 + kPreviewSelectionDebounceNs + 20 +
            i * kPreviewNormalIntervalNs;
        auto revision = Packet(i + 1, true);
        CHECK(service.SubmitPacket(*revision, render_time, &error));
        service.SetVisible(true, render_time);
        CHECK(service.TryClaimWork(render_time, &work));
        CHECK(service.CompleteRender(work.token, true, "queued frame", t0));
    }
    service.CopyStatus(&status);
    CHECK(status.ready_slots == 1);
    PreviewFrameRef newest{};
    CHECK(service.TryAcquireReadyFrame(&newest, t0));
    CHECK(newest.result_key.input_revision == kPreviewSlotCount);
    service.CopyStatus(&status);
    CHECK(status.ready_slots == 0);
    CHECK(status.free_slots == kPreviewSlotCount - 1);
    CHECK(status.leased_slots == 1);
    CHECK(service.ReleaseDisplayLease(newest.slot, newest.slot_generation));
    CHECK(
        service.CompleteDisplayRetirement(newest.slot, newest.slot_generation));

    // An unleased older Ready frame must not stall continuous playback when
    // the HUD still owns one frame and a newer Ready frame is available.
    service.ResetForTest();
    animated = Packet(1, true);
    service.SetEnabled(true);
    service.SetVisible(true, t0);
    service.SetSelection(animated->selection, t0);
    CHECK(service.SubmitPacket(*animated, t0, &error));
    Prepare(&service, t0);
    const uint64_t first_live = t0 + kPreviewSelectionDebounceNs + 10;
    service.SetVisible(true, first_live);
    CHECK(service.TryClaimWork(first_live, &work));
    CHECK(service.CompleteRender(work.token, true, "first", first_live));
    PreviewFrameRef held_live{};
    CHECK(service.TryAcquireReadyFrame(&held_live, first_live));
    for (size_t i = 1; i < kPreviewSlotCount; ++i) {
        const uint64_t live_now = first_live + i * kPreviewPausedIntervalNs;
        service.SetVisible(true, live_now);
        CHECK(service.TryClaimWork(live_now, &work));
        CHECK(service.CompleteRender(work.token, true, "queued", live_now));
    }
    service.CopyStatus(&status);
    CHECK(status.leased_slots == 1 &&
          status.ready_slots == kPreviewSlotCount - 1 &&
          status.free_slots == 0);
    const uint64_t next_live =
        first_live + kPreviewSlotCount * kPreviewPausedIntervalNs;
    service.SetVisible(true, next_live);
    CHECK(service.TryClaimWork(next_live, &work));
    CHECK(service.CompleteRender(work.token, true, "reclaimed", next_live));
    PreviewFrameRef latest_live{};
    CHECK(service.TryAcquireReadyFrame(&latest_live, next_live));
    CHECK(latest_live.result_key.time_seconds >
          held_live.result_key.time_seconds);
    CHECK(
        service.ReleaseDisplayLease(held_live.slot, held_live.slot_generation));
    CHECK(service.CompleteDisplayRetirement(held_live.slot,
                                            held_live.slot_generation));
    CHECK(service.ReleaseDisplayLease(latest_live.slot,
                                      latest_live.slot_generation));
    CHECK(service.CompleteDisplayRetirement(latest_live.slot,
                                            latest_live.slot_generation));

    // Comparison may lease Original and Edited simultaneously. Keep another
    // completed frame visible and one slot available for the next update.
    service.ResetForTest();
    animated = Packet(1, true);
    service.SetEnabled(true);
    service.SetVisible(true, t0);
    service.SetSelection(animated->selection, t0);
    CHECK(service.SubmitPacket(*animated, t0, &error));
    Prepare(&service, t0);
    const uint64_t comparison_start = t0 + kPreviewSelectionDebounceNs + 100;
    PreviewFrameRef comparison_held[2]{};
    for (unsigned i = 0; i < 2; ++i) {
        const uint64_t comparison_now =
            comparison_start + i * kPreviewPausedIntervalNs;
        service.SetVisible(true, comparison_now);
        CHECK(service.TryClaimWork(comparison_now, &work));
        CHECK(service.CompleteRender(work.token, true, "comparison",
                                     comparison_now));
        CHECK(
            service.TryAcquireReadyFrame(&comparison_held[i], comparison_now));
    }
    const uint64_t queued_comparison =
        comparison_start + 2 * kPreviewPausedIntervalNs;
    service.SetVisible(true, queued_comparison);
    CHECK(service.TryClaimWork(queued_comparison, &work));
    CHECK(service.CompleteRender(work.token, true, "queued comparison",
                                 queued_comparison));
    const uint64_t next_comparison =
        comparison_start + 3 * kPreviewPausedIntervalNs;
    service.SetVisible(true, next_comparison);
    CHECK(service.TryClaimWork(next_comparison, &work));
    CHECK(service.CompleteRender(work.token, true, "live comparison",
                                 next_comparison));
    service.CopyStatus(&status);
    CHECK(status.leased_slots == 2 && status.ready_slots == 2 &&
          status.free_slots == 0);
    const uint64_t recycled_comparison =
        comparison_start + 4 * kPreviewPausedIntervalNs;
    service.SetVisible(true, recycled_comparison);
    CHECK(service.TryClaimWork(recycled_comparison, &work));
    CHECK(service.CompleteRender(work.token, true, "recycled comparison",
                                 recycled_comparison));
    service.CopyStatus(&status);
    CHECK(status.leased_slots == 2 && status.ready_slots == 2);
    CHECK(service.TryAcquireReadyFrame(&newest, recycled_comparison));
    CHECK(newest.slot != comparison_held[0].slot &&
          newest.slot != comparison_held[1].slot);
    CHECK(service.ReleaseDisplayLease(newest.slot, newest.slot_generation));
    CHECK(
        service.CompleteDisplayRetirement(newest.slot, newest.slot_generation));
    for (const auto &lease : comparison_held) {
        CHECK(service.ReleaseDisplayLease(lease.slot, lease.slot_generation));
        CHECK(service.CompleteDisplayRetirement(lease.slot,
                                                lease.slot_generation));
    }

    // Returning to a completed-but-discarded input must produce a new frame.
    service.ResetForTest();
    EnableAndSelect(&service, t0);
    Prepare(&service, t0);
    const auto settled = t0 + kPreviewSelectionDebounceNs;
    CHECK(service.TryClaimWork(settled, &work));
    CHECK(service.CompleteRender(work.token, true, "reference", settled));
    PreviewFrameRef reference{};
    CHECK(service.TryAcquireReadyFrame(&reference, settled));
    CHECK(service.SubmitPacket(*Packet(10), settled, &error));
    CHECK(service.TryClaimWork(settled, &work));
    CHECK(service.CompleteRender(work.token, true, "current", settled));
    PreviewFrameRef retained_current{};
    CHECK(service.TryAcquireReadyFrame(&retained_current, settled));
    CHECK(service.SubmitPacket(*Packet(1), settled, &error));
    CHECK(service.TryClaimWork(settled, &work));
    CHECK(service.CompleteRender(work.token, true, "unacquired", settled));
    CHECK(service.SubmitPacket(*Packet(2), settled, &error));
    CHECK(!service.TryAcquireReadyFrame(&newest, settled));
    CHECK(service.SubmitPacket(*Packet(1), settled, &error));
    service.CopyStatus(&status);
    CHECK(status.leased_slots == 2 &&
          status.free_slots == kPreviewSlotCount - 2);
    CHECK(service.TryClaimWork(settled, &work));
    CHECK(service.CompleteRender(work.token, true, "restored", settled));
    CHECK(service.TryAcquireReadyFrame(&newest, settled));

    // A reused slot's larger lease generation must not beat a newer frame.
    service.ResetForTest();
    EnableAndSelect(&service, t0);
    Prepare(&service, t0);
    service.SetGuestPaused(true);
    uint64_t sequence_time = t0 + kPreviewSelectionDebounceNs + 100;
    for (uint64_t revision = 1; revision <= 3; ++revision) {
        auto candidate = Packet(revision, true);
        CHECK(service.SubmitPacket(*candidate, sequence_time, &error));
        service.SetVisible(true, sequence_time);
        CHECK(service.TryClaimWork(sequence_time, &work));
        CHECK(
            service.CompleteRender(work.token, true, "reused", sequence_time));
        CHECK(service.TryAcquireReadyFrame(&frame, sequence_time));
        CHECK(frame.result_key.input_revision == revision);
        CHECK(service.ReleaseDisplayLease(frame.slot, frame.slot_generation));
        CHECK(service.CompleteDisplayRetirement(frame.slot,
                                                frame.slot_generation));
        sequence_time += kPreviewNormalIntervalNs;
    }
    for (uint64_t revision = 4; revision <= 5; ++revision) {
        auto candidate = Packet(revision, true);
        CHECK(service.SubmitPacket(*candidate, sequence_time, &error));
        service.SetVisible(true, sequence_time);
        CHECK(service.TryClaimWork(sequence_time, &work));
        CHECK(
            service.CompleteRender(work.token, true, "queued", sequence_time));
        sequence_time += kPreviewNormalIntervalNs;
    }
    CHECK(service.TryAcquireReadyFrame(&newest, sequence_time));
    CHECK(newest.result_key.input_revision == 5);
    service.SetVisible(false, sequence_time);
    CHECK(!service.TryAcquireReadyFrame(&frame, sequence_time));
    service.CopyStatus(&status);
    CHECK(status.state == PreviewState::Hidden);
    CHECK(status.ready_slots == 0);
    CHECK(status.leased_slots == 1);
    CHECK(service.ReleaseDisplayLease(newest.slot, newest.slot_generation));
    service.CopyStatus(&status);
    CHECK(status.state == PreviewState::Hidden);
    CHECK(
        service.CompleteDisplayRetirement(newest.slot, newest.slot_generation));

    // Consumer-owned slots are never reused until retirement.
    service.ResetForTest();
    animated = Packet(1, true);
    service.SetEnabled(true);
    service.SetVisible(true, t0);
    service.SetSelection(animated->selection, t0);
    CHECK(service.SubmitPacket(*animated, t0, &error));
    Prepare(&service, t0);
    service.SetGuestPaused(true); // Paused work does not require health.
    PreviewFrameRef held[kPreviewSlotCount]{};
    for (size_t i = 0; i < kPreviewSlotCount; ++i) {
        const uint64_t render_time =
            t0 + kPreviewSelectionDebounceNs + 100 +
            i * kPreviewNormalIntervalNs;
        service.SetVisible(true, render_time);
        CHECK(service.TryClaimWork(render_time, &work));
        CHECK(service.CompleteRender(work.token, true, "slot", t0));
        CHECK(service.TryAcquireReadyFrame(&held[i], t0));
    }
    const uint64_t full_time =
        t0 + kPreviewSelectionDebounceNs + 1000 * kPreviewNormalIntervalNs;
    service.SetVisible(true, full_time);
    CHECK(!service.TryClaimWork(full_time, &work));
    service.CopyStatus(&status);
    CHECK(status.dropped_no_slot >= 1);
    service.ReleaseDisplayLease(held[0].slot, held[0].slot_generation);
    CHECK(service.CompleteDisplayRetirement(held[0].slot,
                                            held[0].slot_generation));
    const uint64_t reused_time = full_time;
    service.SetVisible(true, reused_time);
    // Slot pressure can skip frames, but a freed slot should not add a full
    // clock interval before the next paused private preview frame.
    CHECK(service.TryClaimWork(reused_time, &work));
    CHECK(service.CompleteRender(work.token, true, "resumed", reused_time));

    // Hiding the panel immediately prevents admission.
    service.SetVisible(false, t0 + 9999999999ULL);
    CHECK(!service.TryClaimWork(t0 + 10000000000ULL, &work));

    // A missed UI heartbeat also fails closed without explicit tab plumbing.
    service.ResetForTest();
    EnableAndSelect(&service, t0);
    service.SetVisible(true, t0);
    CHECK(!service.TryClaimWork(t0 + kPreviewVisibilityStaleNs + 1, &work));
    service.CopyStatus(&status);
    CHECK(status.state == PreviewState::Hidden);

    // A completion itself must expire visibility, without another poll.
    service.ResetForTest();
    EnableAndSelect(&service, t0);
    service.SetGuestPaused(true);
    CHECK(service.RequestPreparation(&error));
    CHECK(service.TryClaimWork(t0 + kPreviewSelectionDebounceNs, &work));
    CHECK(service.CompletePreparation(work.token, true, "late preparation",
                                      t0 + kPreviewVisibilityStaleNs + 1));
    service.CopyStatus(&status);
    CHECK(status.state == PreviewState::Hidden);
    CHECK(!status.prepared);

    service.ResetForTest();
    EnableAndSelect(&service, t0);
    Prepare(&service, t0);
    CHECK(service.TryClaimWork(t0 + kPreviewSelectionDebounceNs + 1, &work));
    CHECK(service.CompleteRender(work.token, true, "late render",
                                 t0 + kPreviewVisibilityStaleNs + 1));
    service.CopyStatus(&status);
    CHECK(status.state == PreviewState::Hidden);
    CHECK(status.ready_slots == 0);
    CHECK(status.free_slots == kPreviewSlotCount);

    // Active plus replacement pending payloads share the aggregate 32 MiB cap.
    service.ResetForTest();
    auto large_a = Packet();
    large_a->fixture_bytes.resize(20U * 1024U * 1024U);
    large_a->fixture_digest = ComputePreviewDigest(
        large_a->fixture_bytes.data(), large_a->fixture_bytes.size());
    service.SetEnabled(true);
    service.SetVisible(true, t0);
    service.SetSelection(large_a->selection, t0);
    CHECK(service.SubmitPacket(*large_a, t0, &error));
    service.SetGuestPaused(true);
    CHECK(service.RequestPreparation(&error));
    CHECK(service.TryClaimWork(t0 + kPreviewSelectionDebounceNs, &work));
    auto large_b = Packet(2);
    large_b->fixture_bytes.resize(20U * 1024U * 1024U);
    large_b->fixture_digest = ComputePreviewDigest(
        large_b->fixture_bytes.data(), large_b->fixture_bytes.size());
    CHECK(!service.SubmitPacket(*large_b, t0 + 1, &error));
    CHECK(error.find("32 MiB") != std::string::npos);
    CHECK(service.CompletePreparation(work.token, false, "cancelled", t0));

    // A completion already in flight cannot re-enable a disabled preview.
    service.ResetForTest();
    EnableAndSelect(&service, t0);
    service.SetGuestPaused(true);
    CHECK(service.RequestPreparation(&error));
    CHECK(service.TryClaimWork(t0 + kPreviewSelectionDebounceNs, &work));
    service.SetEnabled(false);
    CHECK(service.CompletePreparation(work.token, true, "late", t0));
    service.CopyStatus(&status);
    CHECK(status.state == PreviewState::Disabled);
    CHECK(!status.prepared);

    // A paused guest permits a 60 Hz continuous preview, and a new user
    // input renders immediately without opening an unbounded failure retry.
    service.ResetForTest();
    auto paused_animation = Packet(1, true);
    service.SetEnabled(true);
    service.SetVisible(true, t0);
    service.SetSelection(paused_animation->selection, t0);
    CHECK(service.SubmitPacket(*paused_animation, t0, &error));
    Prepare(&service, t0);
    const uint64_t paused_first = t0 + kPreviewSelectionDebounceNs + 10;
    service.SetVisible(true, paused_first);
    CHECK(service.TryClaimWork(paused_first, &work));
    CHECK(
        service.CompleteRender(work.token, true, "paused frame", paused_first));
    service.CopyStatus(&status);
    CHECK(status.update_hz == 60);
    service.SetVisible(true, paused_first + kPreviewPausedIntervalNs - 1);
    CHECK(!service.TryClaimWork(paused_first + kPreviewPausedIntervalNs - 1,
                                &work));
    service.SetVisible(true, paused_first + kPreviewPausedIntervalNs);
    CHECK(service.TryClaimWork(paused_first + kPreviewPausedIntervalNs, &work));
    CHECK(service.CompleteRender(work.token, true, "paused frame 2",
                                 paused_first + kPreviewPausedIntervalNs));

    service.ResetForTest();
    EnableAndSelect(&service, t0);
    Prepare(&service, t0);
    service.SetVisible(true, paused_first);
    CHECK(service.TryClaimWork(paused_first, &work));
    CHECK(service.CompleteRender(work.token, true, "first", paused_first));
    PreviewPacket input_edit = *Packet(2);
    CHECK(service.SubmitPacket(input_edit, paused_first + 1, &error));
    service.SetVisible(true, paused_first + 1);
    CHECK(service.TryClaimWork(paused_first + 1, &work));
    CHECK(work.result_key.input_revision == 2);
    CHECK(service.CompleteRender(work.token, false, "bad input",
                                 paused_first + 1));
    service.SetVisible(true, paused_first + 2);
    CHECK(!service.TryClaimWork(paused_first + 2, &work));

    // Clock samples reuse immutable source storage and compile identity. Slow
    // completions remain publishable; explicit scrubs obsolete old samples.
    service.ResetForTest();
    service.SetEnabled(true);
    service.SetVisible(true, t0);
    service.SetSelection(paused_animation->selection, t0);
    CHECK(service.SubmitPacket(*paused_animation, t0, &error));
    Prepare(&service, t0);
    service.SetVisible(true, paused_first);
    CHECK(service.TryClaimWork(paused_first, &work));
    const auto storage = work.packet;
    const auto compiled = work.compile_key;
    const auto first_key = work.result_key;
    const auto token = work.token;
    service.SetVisible(true, paused_first + 100000000);
    CHECK(!service.TryClaimWork(paused_first + 100000000, &work));
    CHECK(
        service.CompleteRender(token, true, "slow", paused_first + 100000000));
    CHECK(service.TryClaimWork(paused_first + 100000001, &work));
    CHECK(work.packet == storage);
    CHECK(work.compile_key == compiled);
    CHECK(work.result_key != first_key);
    CHECK(work.result_key.time_seconds > first_key.time_seconds);
    CHECK(service.CompleteRender(work.token, true, "second",
                                 paused_first + 100000001));
    service.EditClock(PreviewClockAction::Pause, 0, paused_first + 100000002);
    // Pause requests one final sample, then remains idle.
    CHECK(service.TryClaimWork(paused_first + 100000002, &work));
    CHECK(service.CompleteRender(work.token, true, "pause",
                                 paused_first + 100000002));
    service.SetVisible(true, paused_first + 200000000);
    CHECK(!service.TryClaimWork(paused_first + 200000000, &work));
    PreviewFrameRef clock_frame{};
    CHECK(service.TryAcquireReadyFrame(&clock_frame, paused_first + 200000000));
    CHECK(service.ReleaseDisplayLease(clock_frame.slot,
                                      clock_frame.slot_generation));
    CHECK(service.CompleteDisplayRetirement(clock_frame.slot,
                                            clock_frame.slot_generation));
    service.EditClock(PreviewClockAction::Scrub, 3.0, paused_first + 200000001);
    CHECK(service.TryClaimWork(paused_first + 200000001, &work));
    CHECK(work.result_key.time_seconds == 3.0);
    CHECK(work.compile_key == compiled);
    service.EditClock(PreviewClockAction::Scrub, 4.0, paused_first + 200000002);
    CHECK(service.CompleteRender(work.token, true, "obsolete scrub",
                                 paused_first + 200000002));
    service.CopyStatus(&status);
    CHECK(status.stale_completions == 1);

    // Scene edits invalidate only results and use the same immutable packet.
    PreviewScene scene;
    scene.mesh = PreviewMesh::Sphere;
    scene.yaw = 30;
    service.EditScene(scene);
    CHECK(service.TryClaimWork(paused_first + 200000003, &work));
    CHECK(work.packet == storage && work.compile_key == compiled);
    CHECK(work.result_key.scene == scene);
    const auto view_key = work.result_key;
    scene.distance = 5;
    service.EditScene(scene);
    CHECK(service.CompleteRender(work.token, true, "obsolete camera",
                                 paused_first + 200000004));
    CHECK(service.TryClaimWork(paused_first + 200000005, &work));
    CHECK(work.result_key != view_key);
    CHECK(work.packet == storage && work.compile_key == compiled);
    CHECK(service.CompleteRender(work.token, true, "camera",
                                 paused_first + 200000005));

    service.EditChannel(PreviewChannel::Red);
    CHECK(service.TryClaimWork(paused_first + 200000006, &work));
    CHECK(work.packet == storage && work.compile_key == compiled);
    CHECK(work.result_key.channel == PreviewChannel::Red);
    service.EditChannel(PreviewChannel::UV);
    CHECK(service.CompleteRender(work.token, true, "obsolete channel",
                                 paused_first + 200000007));
    service.CopyStatus(&status);
    CHECK(status.stale_completions == 3);
    CHECK(service.TryClaimWork(paused_first + 200000008, &work));
    CHECK(work.packet == storage && work.compile_key == compiled);
    CHECK(work.result_key.channel == PreviewChannel::UV);
    CHECK(service.CompleteRender(work.token, true, "chart",
                                 paused_first + 200000008));
    service.EditChannel(PreviewChannel::ShaderDiscard);
    CHECK(!service.TryClaimWork(paused_first + 200000009, &work));

    PreviewFrameRef frozen_channel{};
    CHECK(service.TryAcquireReadyFrame(&frozen_channel,
                                       paused_first + 200000010));
    CHECK(frozen_channel.result_key.channel == PreviewChannel::UV);
    service.EditChannel(PreviewChannel::Alpha);
    CHECK(service.TryClaimWork(paused_first + 200000011, &work));
    CHECK(work.slot != frozen_channel.slot);
    CHECK(work.packet == storage && work.compile_key == compiled);
    CHECK(service.CompleteRender(work.token, true, "alpha",
                                 paused_first + 200000011));
    PreviewFrameRef current_channel{};
    CHECK(service.TryAcquireReadyFrame(&current_channel,
                                       paused_first + 200000012));
    CHECK(current_channel.result_key.channel == PreviewChannel::Alpha);
    CHECK(frozen_channel.result_key.channel == PreviewChannel::UV);
    // A completed but unacquired channel must not appear after a channel edit.
    // Keep both displayed frames leased while using the third slot.
    service.EditChannel(PreviewChannel::Red);
    CHECK(service.TryClaimWork(paused_first + 200000013, &work));
    const auto obsolete_slot = work.slot;
    const auto obsolete_generation = work.slot_generation;
    CHECK(service.CompleteRender(work.token, true, "unacquired red",
                                 paused_first + 200000013));
    service.EditChannel(PreviewChannel::Green);
    PreviewFrameRef obsolete_channel{};
    CHECK(!service.TryAcquireReadyFrame(&obsolete_channel,
                                        paused_first + 200000014));
    service.CopyStatus(&status);
    CHECK(status.ready_slots == 0 && status.leased_slots == 2 &&
          status.free_slots == kPreviewSlotCount - 2);
    CHECK(frozen_channel.result_key.channel == PreviewChannel::UV);
    CHECK(current_channel.result_key.channel == PreviewChannel::Alpha);
    // Returning to the discarded result must rerender immediately while paused.
    service.EditChannel(PreviewChannel::Red);
    CHECK(service.TryClaimWork(paused_first + 200000015, &work));
    CHECK(work.slot == obsolete_slot &&
          work.slot_generation > obsolete_generation);
    CHECK(work.packet == storage && work.compile_key == compiled);
    CHECK(service.CompleteRender(work.token, true, "new red",
                                 paused_first + 200000015));
    PreviewFrameRef new_channel{};
    CHECK(service.TryAcquireReadyFrame(&new_channel, paused_first + 200000016));
    CHECK(new_channel.result_key.channel == PreviewChannel::Red);
    CHECK(service.ReleaseDisplayLease(new_channel.slot,
                                      new_channel.slot_generation));
    CHECK(service.CompleteDisplayRetirement(new_channel.slot,
                                            new_channel.slot_generation));
    CHECK(service.ReleaseDisplayLease(frozen_channel.slot,
                                      frozen_channel.slot_generation));
    CHECK(service.CompleteDisplayRetirement(frozen_channel.slot,
                                            frozen_channel.slot_generation));
    CHECK(service.ReleaseDisplayLease(current_channel.slot,
                                      current_channel.slot_generation));
    CHECK(service.CompleteDisplayRetirement(current_channel.slot,
                                            current_channel.slot_generation));
    service.CopyStatus(&status);
    CHECK(status.free_slots == kPreviewSlotCount);

    // Continuous playback has no duration cap and retains one immutable packet.
    service.ResetForTest();
    service.SetEnabled(true);
    service.SetVisible(true, t0);
    service.SetSelection(paused_animation->selection, t0);
    CHECK(service.SubmitPacket(*paused_animation, t0, &error));
    Prepare(&service, t0);
    service.EditClock(PreviewClockAction::Loop, 0, t0);
    uint64_t long_now = paused_first;
    const unsigned long_playback_samples =
        static_cast<unsigned>(UINT64_C(300000000000) /
                              kPreviewPausedIntervalNs) + 2;
    for (unsigned i = 0; i < long_playback_samples; ++i) {
        long_now += kPreviewPausedIntervalNs;
        service.SetVisible(true, long_now);
        CHECK(
            !service.TryClaimWork(long_now - 1, &work, PreviewBackend::Vulkan));
        CHECK(service.TryClaimWork(long_now, &work, PreviewBackend::OpenGL));
        CHECK(service.CompleteRender(work.token, true, "continuous", long_now));
        CHECK(service.TryAcquireReadyFrame(&clock_frame, long_now));
        CHECK(service.ReleaseDisplayLease(clock_frame.slot,
                                          clock_frame.slot_generation));
        CHECK(service.CompleteDisplayRetirement(clock_frame.slot,
                                                clock_frame.slot_generation));
    }
    service.CopyStatus(&status);
    CHECK(status.clock.time_seconds > 300);
    CHECK(status.clock.playing);
    const double visible_time = status.clock.time_seconds;
    service.SetVisible(false, long_now);
    long_now += 100000000000ULL;
    service.SetVisible(true, long_now);
    CHECK(service.SubmitPacket(*paused_animation, long_now, &error));
    Prepare(&service, long_now);
    service.SetVisible(true, long_now + kPreviewSelectionDebounceNs);
    CHECK(service.TryClaimWork(long_now + kPreviewSelectionDebounceNs, &work));
    CHECK(work.result_key.time_seconds == visible_time);

    // Status readers and health/visibility publishers may run concurrently.
    service.ResetForTest();
    EnableAndSelect(&service, t0);
    std::thread publisher([&service] {
        for (uint64_t i = 0; i < 2000; ++i) {
            const uint64_t sample_ns = UINT64_C(2000000000) + i * 1000;
            service.SetVisible(true, sample_ns);
            service.UpdateHealth(
                PreviewHealth{sample_ns, PreviewPressure::Normal, true});
        }
    });
    std::thread reader([&service] {
        for (int i = 0; i < 2000; ++i) {
            PreviewStatus snapshot{};
            service.CopyStatus(&snapshot);
        }
    });
    publisher.join();
    reader.join();

    std::cout << "shader browser preview service tests passed\n";
    return 0;
}

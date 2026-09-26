#include "../../ui/xui/shader-browser-preview-service.hh"

#include <cassert>
#include <iostream>
#include <memory>
#include <thread>

using namespace xemu::shader_browser;

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
    packet->animated = animated;
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
    assert(service->SubmitPacket(packet, now, &error));
}

static void Prepare(PreviewService *service, uint64_t now)
{
    service->SetGuestPaused(true);
    std::string error;
    assert(service->RequestPreparation(&error));
    PreviewWorkItem work{};
    assert(service->TryClaimWork(now + kPreviewSelectionDebounceNs, &work));
    assert(work.kind == PreviewWorkKind::Prepare);
    assert(service->CompletePreparation(work.token, true, "prepared"));
}

int main()
{
    constexpr uint64_t t0 = UINT64_C(1000000000);
    PreviewService service;
    PreviewWorkItem work{};
    PreviewStatus status{};

    // Disabled state never yields work.
    auto disabled_packet = Packet();
    service.SetSelection(disabled_packet->selection, t0);
    std::string error;
    assert(service.SubmitPacket(disabled_packet, t0, &error));
    assert(!service.TryClaimWork(t0 + kPreviewSelectionDebounceNs, &work));

    // Preparation is explicit and pause-gated.
    service.ResetForTest();
    EnableAndSelect(&service, t0);
    assert(!service.RequestPreparation(&error));
    assert(error.find("paused") != std::string::npos);
    service.SetGuestPaused(true);
    assert(service.RequestPreparation(&error));
    assert(service.TryClaimWork(t0 + kPreviewSelectionDebounceNs, &work));
    assert(work.kind == PreviewWorkKind::Prepare);

    // A/B/C newest-only behavior: completion for A cannot publish after C.
    auto packet_b = Packet(2);
    auto packet_c = Packet(3);
    assert(service.SubmitPacket(packet_b, t0 + 1, &error));
    assert(service.SubmitPacket(packet_c, t0 + 2, &error));
    assert(service.CompletePreparation(work.token, true, "old"));
    service.CopyStatus(&status);
    assert(!status.prepared);
    assert(status.superseded_requests >= 2);
    assert(status.state == PreviewState::NeedsPreparation);
    assert(service.RequestPreparation(&error));
    assert(service.TryClaimWork(t0 + kPreviewSelectionDebounceNs + 1, &work));
    assert(work.packet->input_revision == 3);
    assert(service.CompletePreparation(work.token, true, "current"));
    service.CopyStatus(&status);
    assert(status.prepared);

    // A prepared static packet renders once until its result key changes.
    service.SetGuestPaused(false);
    service.UpdateHealth(PreviewHealth{t0 + kPreviewSelectionDebounceNs + 2,
                                      PreviewPressure::Normal, true});
    const uint64_t first_render_time = t0 + kPreviewSelectionDebounceNs + 3;
    assert(service.TryClaimWork(first_render_time, &work));
    assert(work.kind == PreviewWorkKind::Render);
    assert(service.CompleteRender(work.token, true, "ready"));
    assert(!service.TryClaimWork(first_render_time + kPreviewNormalIntervalNs,
                                 &work));

    PreviewFrameRef frame{};
    assert(service.TryAcquireReadyFrame(&frame));
    assert(frame.slot < kPreviewSlotCount);
    service.ReleaseDisplayLease(frame.slot, frame.slot_generation);
    assert(!service.CompleteDisplayRetirement(frame.slot,
                                              frame.slot_generation + 1));
    assert(service.CompleteDisplayRetirement(frame.slot,
                                             frame.slot_generation));

    // Input-only changes reuse the prepared compile identity.
    auto input_update = Packet(4);
    assert(service.SubmitPacket(input_update, first_render_time + 1, &error));
    service.CopyStatus(&status);
    assert(status.prepared);
    service.SetVisible(true, first_render_time + kPreviewNormalIntervalNs);
    assert(service.TryClaimWork(first_render_time + kPreviewNormalIntervalNs,
                                &work));
    assert(work.kind == PreviewWorkKind::Render);
    assert(service.CompleteRender(work.token, true, "input update"));

    // Animated work is rate-limited, freezes under critical pressure, and
    // stale health never admits running work.
    service.ResetForTest();
    auto animated = Packet(1, true);
    service.SetEnabled(true);
    service.SetVisible(true, t0);
    service.SetSelection(animated->selection, t0);
    assert(service.SubmitPacket(animated, t0, &error));
    Prepare(&service, t0);
    service.SetGuestPaused(false);
    uint64_t now = t0 + kPreviewSelectionDebounceNs + 10;
    service.UpdateHealth(PreviewHealth{now, PreviewPressure::Normal, true});
    assert(service.TryClaimWork(now, &work));
    assert(service.CompleteRender(work.token, true, "frame 1"));
    assert(!service.TryClaimWork(now + kPreviewNormalIntervalNs - 1, &work));
    assert(service.TryClaimWork(now + kPreviewNormalIntervalNs, &work));
    assert(service.CompleteRender(work.token, true, "frame 2"));

    service.UpdateHealth(PreviewHealth{
        now + kPreviewNormalIntervalNs + 1,
        PreviewPressure::Critical, true});
    service.SetVisible(true, now + 2 * kPreviewNormalIntervalNs);
    assert(!service.TryClaimWork(now + 2 * kPreviewNormalIntervalNs, &work));
    service.CopyStatus(&status);
    assert(status.state == PreviewState::Frozen);

    service.UpdateHealth(PreviewHealth{
        now + 2 * kPreviewNormalIntervalNs,
        PreviewPressure::Normal, true});
    service.SetVisible(true, now + 2 * kPreviewNormalIntervalNs + 1);
    assert(!service.TryClaimWork(now + 2 * kPreviewNormalIntervalNs + 1,
                                &work));
    service.UpdateHealth(PreviewHealth{
        now + 2 * kPreviewNormalIntervalNs + kPreviewPressureRecoveryNs,
        PreviewPressure::Normal, true});
    service.SetVisible(
        true, now + 2 * kPreviewNormalIntervalNs + kPreviewPressureRecoveryNs);
    assert(service.TryClaimWork(
        now + 2 * kPreviewNormalIntervalNs + kPreviewPressureRecoveryNs,
        &work));
    assert(service.CompleteRender(work.token, true, "recovered"));

    const uint64_t stale_now =
        now + 2 * kPreviewNormalIntervalNs + kPreviewPressureRecoveryNs +
        kPreviewHealthStaleNs + 1;
    service.SetVisible(true, stale_now);
    assert(!service.TryClaimWork(stale_now, &work));

    // Acquiring the newest completed frame immediately reclaims older Ready
    // frames that were never sampled and therefore have no consumer lease.
    service.ResetForTest();
    animated = Packet(1, true);
    service.SetEnabled(true);
    service.SetVisible(true, t0);
    service.SetSelection(animated->selection, t0);
    assert(service.SubmitPacket(animated, t0, &error));
    Prepare(&service, t0);
    service.SetGuestPaused(true);
    for (size_t i = 0; i < kPreviewSlotCount; ++i) {
        const uint64_t render_time =
            t0 + kPreviewSelectionDebounceNs + 20 +
            i * kPreviewNormalIntervalNs;
        service.SetVisible(true, render_time);
        assert(service.TryClaimWork(render_time, &work));
        assert(service.CompleteRender(work.token, true, "queued frame"));
    }
    service.CopyStatus(&status);
    assert(status.ready_slots == kPreviewSlotCount);
    PreviewFrameRef newest{};
    assert(service.TryAcquireReadyFrame(&newest));
    service.CopyStatus(&status);
    assert(status.ready_slots == 0);
    assert(status.free_slots == kPreviewSlotCount - 1);
    assert(status.leased_slots == 1);
    assert(service.ReleaseDisplayLease(newest.slot, newest.slot_generation));
    assert(service.CompleteDisplayRetirement(newest.slot,
                                             newest.slot_generation));

    // Three consumer-owned slots are never reused until retirement.
    service.ResetForTest();
    animated = Packet(1, true);
    service.SetEnabled(true);
    service.SetVisible(true, t0);
    service.SetSelection(animated->selection, t0);
    assert(service.SubmitPacket(animated, t0, &error));
    Prepare(&service, t0);
    service.SetGuestPaused(true); // Paused work does not require health.
    PreviewFrameRef held[kPreviewSlotCount]{};
    for (size_t i = 0; i < kPreviewSlotCount; ++i) {
        const uint64_t render_time =
            t0 + kPreviewSelectionDebounceNs + 100 +
            i * kPreviewNormalIntervalNs;
        service.SetVisible(true, render_time);
        assert(service.TryClaimWork(render_time, &work));
        assert(service.CompleteRender(work.token, true, "slot"));
        assert(service.TryAcquireReadyFrame(&held[i]));
    }
    const uint64_t full_time =
        t0 + kPreviewSelectionDebounceNs + 1000 * kPreviewNormalIntervalNs;
    service.SetVisible(true, full_time);
    assert(!service.TryClaimWork(full_time, &work));
    service.CopyStatus(&status);
    assert(status.dropped_no_slot >= 1);
    service.ReleaseDisplayLease(held[0].slot, held[0].slot_generation);
    assert(service.CompleteDisplayRetirement(held[0].slot,
                                             held[0].slot_generation));
    const uint64_t reused_time =
        t0 + kPreviewSelectionDebounceNs + 1001 * kPreviewNormalIntervalNs;
    service.SetVisible(true, reused_time);
    assert(service.TryClaimWork(reused_time, &work));

    // Hiding the panel immediately prevents admission.
    service.SetVisible(false, t0 + 9999999999ULL);
    assert(!service.TryClaimWork(t0 + 10000000000ULL, &work));

    // A missed UI heartbeat also fails closed without explicit tab plumbing.
    service.ResetForTest();
    EnableAndSelect(&service, t0);
    service.SetVisible(true, t0);
    assert(!service.TryClaimWork(t0 + kPreviewVisibilityStaleNs + 1, &work));
    service.CopyStatus(&status);
    assert(status.state == PreviewState::Hidden);

    // Active plus replacement pending payloads share the aggregate 32 MiB cap.
    service.ResetForTest();
    auto large_a = Packet();
    large_a->fixture_bytes.resize(20U * 1024U * 1024U);
    large_a->fixture_digest = ComputePreviewDigest(
        large_a->fixture_bytes.data(), large_a->fixture_bytes.size());
    service.SetEnabled(true);
    service.SetVisible(true, t0);
    service.SetSelection(large_a->selection, t0);
    assert(service.SubmitPacket(large_a, t0, &error));
    service.SetGuestPaused(true);
    assert(service.RequestPreparation(&error));
    assert(service.TryClaimWork(t0 + kPreviewSelectionDebounceNs, &work));
    auto large_b = Packet(2);
    large_b->fixture_bytes.resize(20U * 1024U * 1024U);
    large_b->fixture_digest = ComputePreviewDigest(
        large_b->fixture_bytes.data(), large_b->fixture_bytes.size());
    assert(!service.SubmitPacket(large_b, t0 + 1, &error));
    assert(error.find("32 MiB") != std::string::npos);
    assert(service.CompletePreparation(work.token, false, "cancelled"));

    // A completion already in flight cannot re-enable a disabled preview.
    service.ResetForTest();
    EnableAndSelect(&service, t0);
    service.SetGuestPaused(true);
    assert(service.RequestPreparation(&error));
    assert(service.TryClaimWork(t0 + kPreviewSelectionDebounceNs, &work));
    service.SetEnabled(false);
    assert(service.CompletePreparation(work.token, true, "late"));
    service.CopyStatus(&status);
    assert(status.state == PreviewState::Disabled);
    assert(!status.prepared);

    // Status readers and health/visibility publishers may run concurrently.
    service.ResetForTest();
    EnableAndSelect(&service, t0);
    std::thread publisher([&service] {
        for (uint64_t i = 0; i < 2000; ++i) {
            const uint64_t now = UINT64_C(2000000000) + i * 1000;
            service.SetVisible(true, now);
            service.UpdateHealth(
                PreviewHealth{now, PreviewPressure::Normal, true});
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

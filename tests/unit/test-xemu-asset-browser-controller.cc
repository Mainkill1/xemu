// SPDX-License-Identifier: GPL-2.0-or-later
#include "../../ui/xui/asset-browser-live.hh"
#include "../../ui/xui/asset-browser-placement.hh"
#include <glib.h>

using namespace xemu::asset_browser;
static capture::CaptureSessionContext Context()
{
    capture::CaptureSessionContext c;
    c.scope.title_id = 17;
    c.session_epoch = 1;
    c.renderer_epoch = 2;
    c.scope_generation = 3;
    c.generation = 4;
    c.backend = 2;
    return c;
}
static AssetCatalog
Catalog(uint64_t frame,
        std::initializer_list<std::pair<uint64_t, float>> values)
{
    AssetCatalog catalog;
    catalog.context = Context();
    catalog.frame = frame;
    catalog.complete_frame = true;
    auto recording = std::make_shared<capture::CaptureSessionSnapshot>();
    recording->context = catalog.context;
    recording->cpu_bytes = 1024;
    for (auto [id, x] : values) {
        auto part = std::make_shared<AssetPart>();
        part->id = id;
        part->frame = frame;
        part->status = AssetStatus::Ready;
        auto event = std::make_shared<capture::CaptureOccurrence>();
        event->event_id = id;
        event->summary.key.frame = frame;
        part->occurrence = event;
        recording->events.push_back(event);
        part->vertices.resize(3);
        part->vertices[0].position = { x, 0, 0 };
        part->vertices[1].position = { x + 1, 0, 0 };
        part->vertices[2].position = { x, 1, 0 };
        part->indices = { 0, 1, 2 };
        part->geometry_signature[0] = uint8_t(x + 1);
        part->bounds.valid = true;
        part->bounds.minimum = { x, 0, 0 };
        part->bounds.maximum = { x + 1, 1, 0 };
        catalog.parts.push_back(part);
        catalog.entries.push_back(MakeAssetAssembly(catalog, { id }, "Part"));
    }
    catalog.recording = recording;
    return catalog;
}
static AssetCatalog PlacedCatalog(uint64_t frame)
{
    auto c = Catalog(frame, { { 10 * frame, 0 },
                              { 10 * frame + 1, 0 },
                              { 10 * frame + 2, 3 },
                              { 10 * frame + 3, 3 } });
    for (size_t i = 0; i < c.parts.size(); ++i) {
        auto p = std::make_shared<AssetPart>(*c.parts[i]);
        p->placement.valid = true;
        p->placement.clip_from_local = { 1, 0, 0, 0, 0, 1, 0, 0,
                                         0, 0, 1, 0, 0, 0, 0, 1 };
        p->placement.clip_from_local[3] = i == 0 ? 0 :
                                          i == 1 ? 10 :
                                          i == 2 ? -2.75f :
                                                   7.25f;
        if (i == 2 && frame > 1)
            p->placement.clip_from_local[3] += .1f;
        c.parts[i] = p;
    }
    c.entries.clear();
    for (auto &p : c.parts)
        c.entries.push_back(MakeAssetAssembly(c, { p->id }, "Part"));
    return c;
}
static void TestPlacedFollow()
{
    AssetController controller;
    const auto generation = controller.Begin(Context());
    auto initial = PlacedCatalog(1);
    const auto related = SuggestRelatedAssetParts(initial, 10);
    g_assert_cmpuint(related.size(), ==, 2);
    g_assert_cmpuint(related[1], ==, 12);
    auto other_target = initial;
    for (size_t i : { size_t(0), size_t(2) }) {
        auto p = std::make_shared<AssetPart>(*other_target.parts[i]);
        auto e = std::make_shared<capture::CaptureOccurrence>(*p->occurrence);
        capture::CaptureOwnedBlob blob;
        blob.name = "vk.viewport";
        auto bytes = std::make_shared<capture::CaptureImmutableBlock>();
        bytes->bytes = { uint8_t(i) };
        blob.data = bytes;
        e->inputs.blobs.push_back(blob);
        p->occurrence = e;
        other_target.parts[i] = p;
    }
    g_assert_cmpuint(SuggestRelatedAssetParts(other_target, 10).size(), ==, 1);
    auto separate_target = initial;
    for (size_t i : { size_t(0), size_t(2) }) {
        auto p = std::make_shared<AssetPart>(*separate_target.parts[i]);
        auto e = std::make_shared<capture::CaptureOccurrence>(*p->occurrence);
        auto resources = std::make_shared<capture::CaptureResourceEvent>();
        resources->domain_id = 1;
        capture::CaptureResourceAccess access{};
        access.type = capture::CaptureResourceOperationType::Read;
        access.kind = XEMU_SHADER_CAPTURE_RESOURCE_COLOR;
        access.allocation_id = i + 1;
        access.view_id = i + 1;
        resources->accesses.push_back(access);
        e->resource_evidence = resources;
        p->occurrence = e;
        separate_target.parts[i] = p;
    }
    g_assert_cmpuint(SuggestRelatedAssetParts(separate_target, 10).size(), ==,
                     1);
    const auto placed = MakeAssetAssembly(initial, { 10, 12 }, "Car");
    g_assert_true(placed.captured_placement);
    g_assert_cmpfloat(placed.bounds.maximum[0], ==, 1.25f);
    g_assert_true(controller.Publish(initial, generation));
    g_assert_true(controller.Assemble({ 10, 12 }, "Car"));
    controller.Pin(true);
    g_assert_true(controller.Publish(PlacedCatalog(2), generation));
    g_assert_true(controller.State() ==
                  AssetSelectionState::FollowingCandidate);
    g_assert_cmpuint(controller.Selected()->id, ==, 20);
    g_assert_cmpuint(controller.Selected()->parts.size(), ==, 2);
    const auto coherent = controller.Selected();
    auto ambiguous = PlacedCatalog(3);
    auto duplicate = std::make_shared<AssetPart>(*ambiguous.parts[1]);
    duplicate->placement = ambiguous.parts[0]->placement;
    ambiguous.parts[1] = duplicate;
    g_assert_true(controller.Publish(ambiguous, generation));
    g_assert_true(controller.State() == AssetSelectionState::Ambiguous);
    g_assert_true(controller.Selected() == coherent);
    auto missing = PlacedCatalog(4);
    missing.parts.resize(2);
    g_assert_true(controller.Publish(missing, generation));
    g_assert_true(controller.State() == AssetSelectionState::Missing);
    g_assert_true(controller.Selected() == coherent);
}
static AssetCatalog DeformingCatalog(uint64_t frame)
{
    auto catalog = PlacedCatalog(frame);
    for (size_t i = 0; i < catalog.parts.size(); ++i) {
        auto part = std::make_shared<AssetPart>(*catalog.parts[i]);
        auto event =
            std::make_shared<capture::CaptureOccurrence>(*part->occurrence);
        event->summary.shader_count = 1;
        event->summary.shaders[0].stage = capture::Stage::Pixel;
        event->summary.shaders[0].hash.bytes[0] = 1;
        capture::CaptureOwnedBlob blob;
        blob.name = "vertex.attribute0";
        blob.format = 106;
        blob.components = 3;
        blob.stride = 12;
        blob.count = 3;
        blob.offset = 1024 + i * 128;
        auto data = std::make_shared<capture::CaptureImmutableBlock>();
        data->bytes.resize(3 * 12);
        blob.data = data;
        event->inputs.blobs.push_back(blob);
        part->occurrence = event;
        part->source_vertices = { 0, 1, 2 };
        catalog.parts[i] = part;
    }
    return catalog;
}
static void TestPlacedDeformation()
{
    AssetController controller;
    const auto generation = controller.Begin(Context());
    g_assert_true(controller.Publish(DeformingCatalog(1), generation));
    g_assert_true(controller.Assemble({ 10, 12 }, "Car"));
    controller.Pin(true);
    auto next = DeformingCatalog(2);
    auto body = std::make_shared<AssetPart>(*next.parts[0]);
    body->vertices[1].position[0] += .01f;
    body->geometry_signature[0] ^= 0x80;
    next.parts[0] = body;
    g_assert_true(controller.Publish(next, generation));
    g_assert_cmpuint(controller.Selected()->frame, ==, 2);
    g_assert_cmpfloat(controller.Selected()->parts[0]->vertices[1].position[0],
                      ==, 1.01f);
    const auto coherent = controller.Selected();
    auto incompatible = DeformingCatalog(3);
    body = std::make_shared<AssetPart>(*incompatible.parts[0]);
    body->vertices[1].position[0] += .02f;
    body->geometry_signature[0] ^= 0x40;
    auto event =
        std::make_shared<capture::CaptureOccurrence>(*body->occurrence);
    event->inputs.blobs[0].offset += 64;
    body->occurrence = event;
    incompatible.parts[0] = body;
    g_assert_true(controller.Publish(incompatible, generation));
    g_assert_true(controller.Selected() == coherent);
    g_assert_true(controller.State() == AssetSelectionState::Missing);
    auto large = DeformingCatalog(4);
    body = std::make_shared<AssetPart>(*large.parts[0]);
    body->vertices[1].position[0] += 1;
    body->geometry_signature[0] ^= 0x80;
    large.parts[0] = body;
    g_assert_true(controller.Publish(large, generation));
    g_assert_true(controller.Selected() == coherent);
    g_assert_true(controller.State() == AssetSelectionState::Missing);
}
static void TestFollow()
{
    AssetController controller;
    const auto generation = controller.Begin(Context());
    g_assert_true(
        controller.Publish(Catalog(1, { { 1, 0 }, { 2, 10 } }), generation));
    g_assert_true(controller.Select(1));
    g_assert_true(controller.Rename("My car"));
    controller.Pin(true);
    g_assert_true(
        controller.Publish(Catalog(2, { { 20, 10 }, { 10, 0 } }), generation));
    g_assert_cmpuint(controller.Selected()->parts[0]->id, ==, 10);
    g_assert_cmpstr(controller.Selected()->label.c_str(), ==, "My car");
    g_assert_true(controller.State() ==
                  AssetSelectionState::FollowingCandidate);
}
static void TestAmbiguous()
{
    AssetController controller;
    auto generation = controller.Begin(Context());
    g_assert_true(controller.Publish(Catalog(1, { { 1, 0 } }), generation));
    g_assert_true(controller.Select(1));
    controller.Pin(true);
    const auto selected = controller.Selected();
    g_assert_true(
        controller.Publish(Catalog(2, { { 10, 0 }, { 20, 0 } }), generation));
    g_assert_true(controller.Selected() == selected);
    g_assert_true(controller.State() == AssetSelectionState::Ambiguous);
}
static void TestFreezeAndStale()
{
    AssetController controller;
    auto generation = controller.Begin(Context());
    g_assert_true(controller.Publish(Catalog(1, { { 1, 0 } }), generation));
    g_assert_true(controller.Select(1));
    controller.Freeze(true);
    const auto selected = controller.Selected();
    g_assert_false(controller.Publish(Catalog(2, { { 2, 0 } }), generation));
    g_assert_true(controller.Selected() == selected);
    g_assert_true(controller.State() == AssetSelectionState::Frozen);
    controller.Freeze(false);
    g_assert_false(controller.Publish(Catalog(3, { { 3, 0 } }), generation));
    auto fresh = controller.Generation();
    g_assert_true(fresh != generation);
    g_assert_true(controller.Publish(Catalog(3, { { 3, 0 } }), fresh));
}
static void TestCoherentAssembly()
{
    AssetController controller;
    auto generation = controller.Begin(Context());
    g_assert_true(
        controller.Publish(Catalog(1, { { 1, 0 }, { 2, 5 } }), generation));
    g_assert_true(controller.Assemble({ 1, 2 }, "Car"));
    controller.Pin(true);
    g_assert_cmpfloat(controller.Selected()->bounds.maximum[0], ==, 6);
    auto selected = controller.Selected();
    g_assert_true(controller.Publish(Catalog(2, { { 3, 0 } }), generation));
    g_assert_true(controller.Selected() == selected);
    g_assert_true(controller.State() == AssetSelectionState::Missing);
    g_assert_true(
        controller.Publish(Catalog(3, { { 4, 0 }, { 5, 5 } }), generation));
    g_assert_cmpuint(controller.Selected()->frame, ==, 3);
    for (const auto &part : controller.Selected()->parts)
        g_assert_cmpuint(part->frame, ==, 3);
}
static void TestPartialAndEpoch()
{
    AssetController controller;
    auto generation = controller.Begin(Context());
    g_assert_true(controller.Publish(Catalog(1, { { 1, 0 } }), generation));
    g_assert_true(controller.Select(1));
    controller.Pin(true);
    auto selected = controller.Selected();
    auto partial = Catalog(2, { { 2, 0 } });
    partial.complete_frame = false;
    g_assert_true(controller.Publish(std::move(partial), generation));
    g_assert_true(controller.Selected() == selected);
    g_assert_true(controller.State() == AssetSelectionState::Incomplete);
    auto foreign = Catalog(3, { { 3, 0 } });
    foreign.context.renderer_epoch = 99;
    g_assert_false(controller.Publish(std::move(foreign), generation));
    g_assert_true(controller.State() == AssetSelectionState::ScopeChanged);
    g_assert_true(controller.Selected() == selected);
}
static void TestForeignCapture()
{
    capture::CaptureSession session;
    g_assert_true(session.Start(Context()));
    uint64_t claim = 0;
    session.Context(&claim);
    AssetLiveCapture live(session);
    g_assert_false(live.Enable(Context(), 1));
    live.Disable();
    uint64_t after = 0;
    session.Context(&after);
    g_assert_true(session.Active());
    g_assert_cmpuint(claim, ==, after);
}
static void TestRearmOwnership()
{
    capture::CaptureSession session;
    AssetController controller;
    controller.Begin(Context());
    AssetLiveCapture live(session);
    g_assert_true(live.Enable(Context(), 1));
    g_assert_true(live.OwnedGeneration() != 0);
    auto foreign = Context();
    foreign.generation = 99;
    g_assert_true(session.Start(foreign));
    live.Disable();
    g_assert_true(session.Active());
    g_assert_cmpuint(session.Context().generation, ==, 99);
}
static void TestPendingOwnership()
{
    capture::CaptureSession session;
    g_assert_true(session.Start(Context()));
    session.GuestFrameBoundary(1);
    capture::DrawCaptureSummary summary;
    summary.scope = Context().scope;
    summary.key = { 1, 2, 1, 1, 1 };
    auto token = session.BeginOccurrence(summary);
    g_assert_true(token != 0);
    session.Stop();
    g_assert_cmpuint(session.Snapshot().pending_events, ==, 1);
    uint64_t owned = 0;
    g_assert_false(session.TryStart(Context(), {}, &owned));
    g_assert_cmpuint(session.Snapshot().pending_events, ==, 1);
    g_assert_cmpuint(owned, ==, 0);
}
static void TestAsyncPublicationImpl(bool early_stop)
{
    capture::CaptureSession session;
    AssetController controller;
    auto context = Context();
    context.generation =
        0; // HUD supplies identity; acquisition owns its incarnation.
    controller.Begin(context);
    AssetLiveCapture live(session);
    g_assert_true(live.Enable(context, 1));
    session.GuestFrameBoundary(1);
    capture::DrawCaptureSummary summary;
    summary.scope = Context().scope;
    summary.key = { 1, 2, 1, 1, 1 };
    summary.primitive_mode = 5;
    auto token = session.BeginOccurrence(summary);
    float positions[] = { 0, 0, 0, 1, 0, 0, 0, 1, 0 };
    XemuShaderDrawBlob blob{};
    blob.name = "vertex.attribute0";
    blob.data = positions;
    blob.byte_count = sizeof(positions);
    blob.format = 106;
    blob.components = 3;
    blob.stride = 12;
    blob.count = 3;
    g_assert_true(session.StageBlob(token, blob));
    g_assert_true(session.StageRegister(token, "capture.vertices.first", 0));
    g_assert_true(session.StageRegister(token, "capture.vertices.count", 3));
    g_assert_true(session.InputsComplete(token));
    g_assert_true(session.Finish(token, true, 5, 3, 0));
    if (early_stop)
        session.Stop();
    else
        session.GuestFrameBoundary(2);
    for (size_t attempt = 0;
         attempt < 3000 && controller.Catalog().parts.empty(); ++attempt) {
        live.Tick(Context(), UINT64_C(1000000) + attempt * 1000, controller);
        g_usleep(1000);
    }
    g_assert_cmpuint(controller.Catalog().parts.size(), ==, 1);
    g_assert_true(controller.Catalog().complete_frame == !early_stop);
    g_assert_true(controller.Catalog().parts[0]->status == AssetStatus::Ready);
    live.Disable();
    g_assert_false(session.Active());
}
static void TestAsyncPublication()
{
    TestAsyncPublicationImpl(false);
}
static void TestEarlyStop()
{
    TestAsyncPublicationImpl(true);
}
static void TestLiveDrawInputs()
{
    capture::CaptureSession session;
    auto settings = capture::CaptureSessionSettings{};
    settings.mode = static_cast<capture::CaptureSessionMode>(2);
    g_assert_true(session.Start(Context(), settings));
    capture::DrawCaptureSummary draw;
    draw.scope = Context().scope;
    draw.key = { 1, 2, 10, 1, 1 };
    draw.shader_count = 1;
    draw.shaders[0].stage = capture::Stage::Pixel;
    g_assert_cmpuint(session.BeginOccurrence(draw), ==,
                     0); // Begin at a complete frame.
    session.GuestFrameBoundary(10);
    g_assert_cmpuint(
        session.BeginOccurrence(draw, capture::CaptureEventType::StateWrite),
        ==, 0);
    auto token = session.BeginOccurrence(draw);
    g_assert_cmpuint(token, !=, 0);
    g_assert_true(session.Finish(token, true, 5, 3, 0));
    g_assert_true(session.InputsComplete(token));
    draw.key.draw = 2;
    draw.key.submission = 2;
    token = session.BeginOccurrence(draw, capture::CaptureEventType::Draw,
                                    capture::CaptureUnsupported);
    g_assert_cmpuint(token, !=,
                     0); // Keep rejected draw evidence in this mode too.
    g_assert_true(session.Finish(token, false, 6, 0, 2));
    g_assert_true(session.InputsComplete(token));
    const auto count = session.Snapshot().events.size();
    session.ReleaseResource(1, 64, 0, 0);
    g_assert_cmpuint(session.Snapshot().events.size(), ==, count);
    session.GuestFrameBoundary(11);
    auto snap = session.Snapshot();
    g_assert_true(snap.state == capture::CaptureSessionState::Ready);
    g_assert_true(snap.frame_window_complete);
    g_assert_false(snap.execution_order_complete);
    size_t draws = 0;
    for (const auto &e : snap.events)
        if (e->type == capture::CaptureEventType::Draw) {
            ++draws;
            g_assert_true(e->limitations & capture::CaptureMissingDependencies);
        }
    g_assert_cmpuint(draws, ==, 2);
    settings.mode = capture::CaptureSessionMode::NextFrame;
    g_assert_true(session.Start(Context(), settings));
    session.GuestFrameBoundary(10);
    token =
        session.BeginOccurrence(draw, capture::CaptureEventType::StateWrite);
    g_assert_cmpuint(token, !=,
                     0); // Full forensic acquisition remains unchanged.
    session.Finish(token, false, 0, 0, 0);
    session.InputsComplete(token);
    session.Stop();
}
static void TestLiveStageFilter()
{
    capture::CaptureSession session;
    capture::CaptureSessionSettings settings;
    settings.mode = capture::CaptureSessionMode::LiveDrawInputs;
    capture::ShaderKey vs, ps;
    vs.stage = capture::Stage::Vertex;
    ps.stage = capture::Stage::Pixel;
    vs.hash.bytes[0] = 1;
    ps.hash.bytes[0] = 2;
    settings.live_stage_sets = { { vs, ps } };
    uint64_t claim = 0;
    g_assert_true(session.TryStart(Context(), settings, &claim));
    session.GuestFrameBoundary(10);
    capture::DrawCaptureSummary draw;
    draw.scope = Context().scope;
    draw.key = { 1, 2, 10, 1, 1 };
    draw.shader_count = 2;
    draw.shaders[0] = ps;
    draw.shaders[1] = vs; // Stage ordering does not matter.
    auto wrong = draw;
    wrong.shaders[0].hash.bytes[0] = 3;
    g_assert_cmpuint(session.BeginOccurrence(wrong), ==, 0);
    g_assert_cmpuint(
        session.BeginOccurrence(draw, capture::CaptureEventType::Upload), ==,
        0);
    auto token = session.BeginOccurrence(draw);
    g_assert_cmpuint(token, !=, 0);
    session.Finish(token, true, 5, 3, 0);
    session.InputsComplete(token);
    session.GuestFrameBoundary(11);
    std::string error;
    auto *tmp = g_dir_make_tmp("xemu-live-stage-filter-XXXXXX", nullptr);
    g_assert_nonnull(tmp);
    const auto path = std::filesystem::path(tmp) / "capture";
    g_free(tmp);
    g_assert_true(session.Save(path, &error));
    capture::CaptureSessionSnapshot reopened;
    g_assert_true(capture::CaptureSession::Reopen(path, &reopened, &error));
    g_assert_true(reopened.settings.mode ==
                  capture::CaptureSessionMode::LiveDrawInputs);
    g_assert_true(reopened.settings.live_stage_sets ==
                  settings.live_stage_sets);
    g_assert_false(reopened.execution_order_complete);
    std::filesystem::remove_all(path.parent_path());
    g_assert_true(session.TryStart(Context(), settings, &claim));
    session.GuestFrameBoundary(10);
    wrong.key.renderer_epoch = 3;
    g_assert_cmpuint(session.BeginOccurrence(wrong,
                                             capture::CaptureEventType::Draw, 0,
                                             claim, Context().scope_generation),
                     ==, 0);
    g_assert_true(session.Snapshot().state ==
                  capture::CaptureSessionState::Cancelled);
    settings.live_stage_sets = { { vs, vs } };
    g_assert_false(session.Start(Context(), settings));
    settings.live_stage_sets = { {} };
    g_assert_false(session.Start(Context(), settings));
    settings.live_stage_sets =
        std::vector<std::vector<capture::ShaderKey>>(129, { vs });
    g_assert_false(session.Start(Context(), settings));
    settings.live_stage_sets = { { vs, ps } };
    settings.mode = capture::CaptureSessionMode::NextFrame;
    g_assert_false(session.Start(Context(), settings));
}
static void TestWatchdog()
{
    capture::CaptureSession session;
    AssetController controller;
    controller.Begin(Context());
    AssetLiveCapture live(session);
    AssetLiveSettings settings;
    settings.progress_timeout_ns = 100;
    g_assert_true(live.Enable(Context(), 1, settings));
    live.Tick(Context(), 102, controller);
    g_assert_false(live.Enabled());
    g_assert_false(session.Active());
}
static void TestFirstFollowRequest(bool pinned)
{
    capture::CaptureSession session;
    AssetController controller;
    const auto generation = controller.Begin(Context());
    auto catalog = Catalog(7, { { 1, 0 } });
    auto part = std::make_shared<AssetPart>(*catalog.parts[0]);
    auto event =
        std::make_shared<capture::CaptureOccurrence>(*part->occurrence);
    capture::ShaderKey vs, ps;
    vs.stage = capture::Stage::Vertex;
    ps.stage = capture::Stage::Pixel;
    vs.hash.bytes[0] = 1;
    ps.hash.bytes[0] = 2;
    event->summary.shader_count = 2;
    event->summary.shaders[0] = ps;
    event->summary.shaders[1] = vs;
    part->occurrence = event;
    catalog.parts[0] = part;
    g_assert_true(controller.Publish(std::move(catalog), generation));
    g_assert_true(controller.Assemble({ 1 }, "Car"));
    controller.Pin(pinned);
    AssetLiveCapture live(session);
    g_assert_true(live.Enable(Context(), 1, {}, &controller));
    const auto snapshot = session.Snapshot();
    g_assert_cmpuint(snapshot.settings.live_stage_sets.size(), ==,
                     pinned ? 1 : 0);
    g_assert_true(snapshot.settings.live_continuous == pinned);
    session.GuestFrameBoundary(10);
    auto draw = event->summary;
    draw.scope = Context().scope;
    draw.key = { 1, 2, 10, 1, 1 };
    draw.shaders[0].hash.bytes[0] = 3;
    const auto other = session.BeginOccurrence(draw);
    g_assert_true(pinned ? other == 0 : other != 0);
    if (other) {
        session.Finish(other, true, 5, 3, 0);
        session.InputsComplete(other);
    }
    draw.shaders[0] = ps;
    const auto matching = session.BeginOccurrence(draw);
    g_assert_cmpuint(matching, !=, 0);
    session.Finish(matching, true, 5, 3, 0);
    session.InputsComplete(matching);
    live.Disable();
}
static void TestContinuousFrames()
{
    capture::CaptureSession session;
    capture::CaptureSessionSettings settings;
    settings.mode = capture::CaptureSessionMode::LiveDrawInputs;
    settings.live_continuous = true;
    settings.history_frames = 3;
    uint64_t claim = 0;
    g_assert_true(session.TryStart(Context(), settings, &claim));
    capture::DrawCaptureSummary draw;
    draw.scope = Context().scope;
    draw.key = { 1, 2, 10, 1, 1 };
    session.GuestFrameBoundary(10);
    const auto first = session.BeginOccurrence(draw);
    uint32_t value = 17;
    XemuShaderDrawBlob blob{};
    blob.name = "continuous.owned";
    blob.data = &value;
    blob.byte_count = sizeof(value);
    g_assert_true(session.StageBlob(first, blob));
    g_assert_true(session.Finish(first, true, 5, 3, 0));
    session.GuestFrameBoundary(11);
    g_assert_true(session.Active());
    capture::CaptureSessionSnapshot ready;
    g_assert_false(session.SnapshotCompletedLiveFrame(0, &ready, claim));
    g_assert_true(session.InputsComplete(first));
    g_assert_true(session.SnapshotCompletedLiveFrame(0, &ready, claim));
    g_assert_cmpuint(ready.last_frame, ==, 10);
    g_assert_true(ready.frame_window_complete);
    g_assert_true(ready.state == capture::CaptureSessionState::Ready);
    g_assert_false(ready.execution_order_complete);
    const auto owned = ready.events.back();
    g_assert_cmpuint(owned->inputs.blobs[0].data->bytes[0], ==, 17);
    g_assert_false(session.SnapshotCompletedLiveFrame(10, &ready, claim));
    g_assert_false(session.SnapshotCompletedLiveFrame(0, &ready, claim + 1));
    std::string error;
    auto *temporary = g_dir_make_tmp("xemu-live-completed-XXXXXX", nullptr);
    g_assert_nonnull(temporary);
    const auto path = std::filesystem::path(temporary) / "capture";
    g_free(temporary);
    g_assert_true(capture::CaptureSession::SaveSnapshot(ready, path, &error));
    capture::CaptureSessionSnapshot reopened;
    g_assert_true(capture::CaptureSession::Reopen(path, &reopened, &error));
    g_assert_true(reopened.settings.live_continuous);
    g_assert_cmpuint(reopened.last_frame, ==, 10);
    g_assert_cmpuint(reopened.events.back()->inputs.blobs[0].data->bytes[0], ==,
                     17);
    std::filesystem::remove_all(path.parent_path());
    for (uint64_t frame = 11; frame != 20; ++frame) {
        draw.key.frame = frame;
        const auto token = session.BeginOccurrence(draw);
        g_assert_cmpuint(token, !=, 0);
        value = uint32_t(frame);
        g_assert_true(session.StageBlob(token, blob));
        g_assert_true(session.Finish(token, true, 5, 3, 0));
        g_assert_true(session.InputsComplete(token));
        session.GuestFrameBoundary(frame + 1);
        g_assert_true(session.Active());
        g_assert_true(
            session.SnapshotCompletedLiveFrame(frame - 1, &ready, claim));
        g_assert_cmpuint(ready.last_frame, ==, frame);
        g_assert_cmpuint(session.Snapshot().retained_frames.size(), <=, 3);
    }
    g_assert_null(session.Find(first & ~capture::kCaptureSessionTokenBit));
    g_assert_cmpuint(owned->inputs.blobs[0].data->bytes[0], ==, 17);
    session.StopIfCurrent(claim);
    g_assert_false(session.Active());
    settings.mode = capture::CaptureSessionMode::NextFrame;
    g_assert_false(session.Start(Context(), settings));
    settings.mode = capture::CaptureSessionMode::LiveDrawInputs;
    settings.history_frames = 600;
    g_assert_false(session.Start(Context(), settings));
}
static void TestContinuousPendingFrame()
{
    capture::CaptureSession session;
    capture::CaptureSessionSettings settings;
    settings.mode = capture::CaptureSessionMode::LiveDrawInputs;
    settings.live_continuous = true;
    settings.history_frames = 3;
    uint64_t claim = 0;
    g_assert_true(session.TryStart(Context(), settings, &claim));
    session.GuestFrameBoundary(10);
    capture::DrawCaptureSummary draw;
    draw.scope = Context().scope;
    draw.key = { 1, 2, 10, 1, 1 };
    const auto token = session.BeginOccurrence(draw);
    g_assert_true(session.Finish(token, true, 5, 3, 0));
    for (uint64_t frame = 11; frame <= 20; ++frame)
        session.GuestFrameBoundary(frame);
    const auto id = token & ~capture::kCaptureSessionTokenBit;
    g_assert_nonnull(session.Find(id).get());
    g_assert_true(session.Find(id)->pending);
    // GPU ownership protects the entire old frame, including its boundary.
    const auto snapshot = session.Snapshot();
    g_assert_cmpuint(snapshot.retained_frames.front(), ==, 10);
    g_assert_cmpuint(snapshot.pending_events, ==, 1);
    g_assert_true(session.InputsComplete(token));
    session.GuestFrameBoundary(21);
    g_assert_null(session.Find(id).get());
    session.StopIfCurrent(claim);
    g_assert_false(session.Active());
}
static void TestContinuousPublication(bool stop_recorder = false)
{
    capture::CaptureSession session;
    AssetController controller;
    const auto generation = controller.Begin(Context());
    auto initial = Catalog(7, { { 1, 0 } });
    auto part = std::make_shared<AssetPart>(*initial.parts[0]);
    auto event =
        std::make_shared<capture::CaptureOccurrence>(*part->occurrence);
    event->summary.shader_count = 1;
    event->summary.shaders[0].stage = capture::Stage::Pixel;
    event->summary.shaders[0].hash.bytes[0] = 1;
    part->occurrence = event;
    initial.parts[0] = part;
    g_assert_true(controller.Publish(initial, generation));
    g_assert_true(controller.Assemble({ 1 }, "Car"));
    controller.Pin(true);
    AssetLiveCapture live(session);
    g_assert_true(live.Enable(Context(), 1, {}, &controller));
    const auto claim = live.OwnedGeneration();
    auto emit = [&](uint64_t frame) {
        auto draw = event->summary;
        draw.scope = Context().scope;
        draw.key = { 1, 2, frame, uint32_t(frame), frame };
        const auto token = session.BeginOccurrence(draw);
        g_assert_cmpuint(token, !=, 0);
        float positions[] = { 0, 0, 0, 1, 0, 0, 0, 1, 0 };
        XemuShaderDrawBlob blob{};
        blob.name = "vertex.attribute0";
        blob.data = positions;
        blob.byte_count = sizeof(positions);
        blob.format = 106;
        blob.components = 3;
        blob.stride = 12;
        blob.count = 3;
        g_assert_true(session.StageBlob(token, blob));
        g_assert_true(
            session.StageRegister(token, "capture.vertices.first", 0));
        g_assert_true(
            session.StageRegister(token, "capture.vertices.count", 3));
        g_assert_true(session.Finish(token, true, 5, 3, 0));
        g_assert_true(session.InputsComplete(token));
    };
    session.GuestFrameBoundary(10);
    for (uint64_t frame = 10; frame <= 11; ++frame) {
        emit(frame);
        session.GuestFrameBoundary(frame + 1);
        for (uint64_t attempt = 0;
             attempt < 3000 && controller.Catalog().frame != frame; ++attempt) {
            live.Tick(Context(),
                      (frame - 9) * UINT64_C(33000000) + attempt * 1000,
                      controller);
            g_usleep(1000);
        }
        g_assert_cmpuint(controller.Catalog().frame, ==, frame);
        g_assert_true(controller.Catalog().complete_frame);
        g_assert_true(session.Active());
        g_assert_cmpuint(live.OwnedGeneration(), ==, claim);
    }
    if (stop_recorder)
        session.StopIfCurrent(claim);
    else
        controller.Pin(false);
    for (uint64_t attempt = 0; attempt < 3000 && live.Enabled(); ++attempt) {
        live.Tick(Context(), UINT64_C(100000000) + attempt * 1000, controller);
        g_usleep(1000);
    }
    g_assert_false(live.Enabled());
    g_assert_false(session.Active());
    g_assert_cmpuint(controller.Catalog().frame, ==, 11);
}
static void TestFirstBudgetReason()
{
    capture::CaptureSession session;
    g_assert_true(session.Start(Context()));
    session.GuestFrameBoundary(10);
    capture::DrawCaptureSummary draw;
    draw.scope = Context().scope;
    draw.key = { 1, 2, 10, 1, 1 };
    const auto token = session.BeginOccurrence(draw);
    g_assert_cmpuint(token, !=, 0);
    g_assert_false(
        session.ReservePayload(token, capture::kDrawInputBudget + 1));
    const auto reason = session.Snapshot().reason;
    session.BudgetExceeded(
        token, "Vulkan capture could not retain all requested evidence");
    g_assert_true(session.Snapshot().reason == reason);
    g_assert_true(
        session.Find(token & ~capture::kCaptureSessionTokenBit)->failure ==
        reason);
}
static void TestDiscoveryRetainsCompleteFrame()
{
    capture::CaptureSession session;
    AssetController controller;
    const auto generation = controller.Begin(Context());
    g_assert_true(
        controller.Publish(Catalog(10, { { 1, 0 }, { 2, 2 } }), generation));
    AssetLiveCapture live(session);
    g_assert_true(live.Enable(Context(), 1));
    session.GuestFrameBoundary(11);
    capture::DrawCaptureSummary draw;
    draw.scope = Context().scope;
    draw.key = { 1, 2, 11, 1, 1 };
    const auto token = session.BeginOccurrence(draw);
    g_assert_cmpuint(token, !=, 0);
    session.BudgetExceeded(token, "In-flight readback byte budget exceeded");
    g_assert_true(session.Finish(token, true, 5, 3, 0));
    g_assert_true(session.InputsComplete(token));
    for (uint64_t attempt = 0; attempt < 3000 && live.Enabled(); ++attempt) {
        live.Tick(Context(), UINT64_C(1000000) + attempt * 1000, controller);
        g_usleep(1000);
    }
    g_assert_false(live.Enabled());
    g_assert_true(
        live.Message().find("In-flight readback byte budget exceeded") !=
        std::string::npos);
    g_assert_cmpuint(controller.Catalog().frame, ==, 10);
    g_assert_cmpuint(controller.Catalog().parts.size(), ==, 2);
    g_assert_true(controller.Catalog().complete_frame);
    // The failed frame remains owned by the recorder for diagnostics.
    g_assert_cmpuint(session.Snapshot().last_frame, ==, 11);
}
static void TestDiscoveryDecodeLimitReason()
{
    capture::CaptureSession session;
    AssetController controller;
    controller.Begin(Context());
    AssetLiveCapture live(session);
    AssetLiveSettings settings;
    settings.assets.maximum_parts = 1;
    g_assert_true(live.Enable(Context(), 1, settings));
    session.GuestFrameBoundary(10);
    capture::DrawCaptureSummary draw;
    draw.scope = Context().scope;
    for (uint32_t i = 1; i <= 2; ++i) {
        draw.key = { 1, 2, 10, i, i };
        auto token = session.BeginOccurrence(draw);
        g_assert_cmpuint(token, !=, 0);
        g_assert_true(session.Finish(token, true, 5, 3, 0));
        g_assert_true(session.InputsComplete(token));
    }
    session.GuestFrameBoundary(11);
    g_assert_true(session.Snapshot().state ==
                  capture::CaptureSessionState::Ready);
    for (uint64_t attempt = 0; attempt < 3000 && live.Enabled(); ++attempt) {
        live.Tick(Context(), UINT64_C(1000000) + attempt * 1000, controller);
        g_usleep(1000);
    }
    g_assert_false(live.Enabled());
    g_assert_true(live.Message().find("Parts per frame") != std::string::npos);
    g_assert_true(live.Message().find("1/1") != std::string::npos);
    g_assert_cmpuint(controller.Catalog().parts.size(), ==, 1);
}
static void TestNamedAssembly()
{
    AssetController controller;
    auto generation = controller.Begin(Context());
    g_assert_true(
        controller.Publish(Catalog(1, { { 1, 0 }, { 2, 5 } }), generation));
    g_assert_true(controller.Assemble({ 1, 2 }, "My car"));
    g_assert_true(controller.RememberSelected());
    g_assert_cmpuint(controller.NamedAssemblies().size(), ==, 1);
    auto car = controller.Selected();
    auto car_recording = controller.SelectedRecording();
    g_assert_true(controller.Select(1));
    g_assert_true(controller.Recall(0));
    g_assert_true(controller.Selected() == car);
    controller.Pin(true);
    g_assert_true(
        controller.Publish(Catalog(2, { { 20, 5 }, { 10, 0 } }), generation));
    g_assert_cmpuint(controller.NamedAssemblies()[0]->frame, ==, 2);
    car_recording = controller.SelectedRecording();
    g_assert_true(controller.Publish(Catalog(3, { { 30, 15 } }), generation));
    g_assert_true(controller.Select(30));
    g_assert_true(controller.Recall(0));
    g_assert_true(controller.SelectedRecording() == car_recording);
    auto oversized = Catalog(4, { { 40, 20 } });
    auto huge =
        std::make_shared<capture::CaptureSessionSnapshot>(*oversized.recording);
    huge->cpu_bytes = UINT64_C(300) * 1024 * 1024;
    oversized.recording = huge;
    g_assert_true(controller.Publish(std::move(oversized), generation));
    g_assert_true(controller.Select(40));
    g_assert_true(controller.Rename("Over budget"));
    g_assert_false(controller.RememberSelected());
    controller.Forget(0);
    g_assert_true(controller.NamedAssemblies().empty());
}
static void TestSharedNamedBudget()
{
    auto large_catalog = [](uint64_t frame) {
        auto catalog =
            Catalog(frame, { { frame * 10, 0 }, { frame * 10 + 1, 5 } });
        catalog.entries.clear();
        for (auto &part : catalog.parts) {
            auto large = std::make_shared<AssetPart>(*part);
            large->vertices.resize(750000);
            large->indices.resize(750000);
            for (size_t i = 0; i < large->indices.size(); ++i)
                large->indices[i] = uint32_t(i);
            part = large;
            catalog.entries.push_back(
                MakeAssetAssembly(catalog, { part->id }, "Part"));
        }
        return catalog;
    };
    AssetController controller;
    auto generation = controller.Begin(Context());
    g_assert_true(controller.Publish(large_catalog(1), generation));
    g_assert_true(controller.Assemble({ 10, 11 }, "First"));
    g_assert_true(controller.RememberSelected());
    g_assert_true(controller.Rename("Second"));
    g_assert_true(controller.RememberSelected());
    controller.Pin(true);
    g_assert_true(controller.Publish(large_catalog(2), generation));
    g_assert_cmpuint(controller.Selected()->frame, ==, 2);
    g_assert_cmpuint(controller.NamedAssemblies()[1]->frame, ==, 1);
    g_assert_true(controller.Message().find("retention budget") !=
                  std::string::npos);
}
int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, nullptr);
    g_test_add_func("/asset/follow/placed-car", TestPlacedFollow);
    g_test_add_func("/asset/follow/deforming-car", TestPlacedDeformation);
    g_test_add_func("/asset/controller/follow", TestFollow);
    g_test_add_func("/asset/controller/ambiguous", TestAmbiguous);
    g_test_add_func("/asset/controller/freeze-stale", TestFreezeAndStale);
    g_test_add_func("/asset/controller/coherent", TestCoherentAssembly);
    g_test_add_func("/asset/controller/partial-epoch", TestPartialAndEpoch);
    g_test_add_func("/asset/live/foreign", TestForeignCapture);
    g_test_add_func("/asset/live/rearm-ownership", TestRearmOwnership);
    g_test_add_func("/asset/live/pending-ownership", TestPendingOwnership);
    g_test_add_func("/asset/live/async-publication", TestAsyncPublication);
    g_test_add_func("/asset/live/early-stop", TestEarlyStop);
    g_test_add_func("/asset/live/draw-inputs", TestLiveDrawInputs);
    g_test_add_func("/asset/live/stage-filter", TestLiveStageFilter);
    g_test_add_func("/asset/live/continuous-frames", TestContinuousFrames);
    g_test_add_func("/asset/live/continuous-pending-frame",
                    TestContinuousPendingFrame);
    g_test_add_func("/asset/live/continuous-publication",
                    [] { TestContinuousPublication(); });
    g_test_add_func("/asset/live/continuous-stopped",
                    [] { TestContinuousPublication(true); });
    g_test_add_func("/asset/live/watchdog", TestWatchdog);
    g_test_add_func("/asset/live/first-follow-filter",
                    [] { TestFirstFollowRequest(true); });
    g_test_add_func("/asset/live/first-unpinned-discovery",
                    [] { TestFirstFollowRequest(false); });
    g_test_add_func("/asset/controller/named", TestNamedAssembly);
    g_test_add_func("/asset/controller/shared-named-budget",
                    TestSharedNamedBudget);
    g_test_add_func("/asset/live/first-budget-reason", TestFirstBudgetReason);
    g_test_add_func("/asset/live/retain-complete-after-budget",
                    TestDiscoveryRetainsCompleteFrame);
    g_test_add_func("/asset/live/decode-limit-reason",
                    TestDiscoveryDecodeLimitReason);
    return g_test_run();
}

// SPDX-License-Identifier: GPL-2.0-or-later
#include "../../ui/xui/asset-browser-live.hh"
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
    for (auto [id, x] : values) {
        auto part = std::make_shared<AssetPart>();
        part->id = id;
        part->frame = frame;
        part->status = AssetStatus::Ready;
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
    return catalog;
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
static void TestAsyncPublication()
{
    capture::CaptureSession session;
    AssetController controller;
    controller.Begin(Context());
    AssetLiveCapture live(session);
    g_assert_true(live.Enable(Context(), 1));
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
    session.GuestFrameBoundary(2);
    for (size_t attempt = 0;
         attempt < 3000 && controller.Catalog().parts.empty(); ++attempt) {
        live.Tick(Context(), UINT64_C(1000000) + attempt * 1000, controller);
        g_usleep(1000);
    }
    g_assert_cmpuint(controller.Catalog().parts.size(), ==, 1);
    g_assert_true(controller.Catalog().complete_frame);
    g_assert_true(controller.Catalog().parts[0]->status == AssetStatus::Ready);
    live.Disable();
    g_assert_false(session.Active());
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
int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, nullptr);
    g_test_add_func("/asset/controller/follow", TestFollow);
    g_test_add_func("/asset/controller/ambiguous", TestAmbiguous);
    g_test_add_func("/asset/controller/freeze-stale", TestFreezeAndStale);
    g_test_add_func("/asset/controller/coherent", TestCoherentAssembly);
    g_test_add_func("/asset/controller/partial-epoch", TestPartialAndEpoch);
    g_test_add_func("/asset/live/foreign", TestForeignCapture);
    g_test_add_func("/asset/live/rearm-ownership", TestRearmOwnership);
    g_test_add_func("/asset/live/pending-ownership", TestPendingOwnership);
    g_test_add_func("/asset/live/async-publication", TestAsyncPublication);
    g_test_add_func("/asset/live/watchdog", TestWatchdog);
    return g_test_run();
}

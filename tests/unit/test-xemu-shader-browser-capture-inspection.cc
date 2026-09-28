// SPDX-License-Identifier: GPL-2.0-or-later
#include "../../ui/xui/shader-browser-capture-inspection.hh"

#include <cassert>
#include <cstring>
#include <iostream>
#include <nlohmann/json.hpp>

using namespace xemu::shader_browser;
static ShaderKey Pixel()
{
    ShaderKey k;
    k.stage = Stage::Pixel;
    k.hash.bytes[0] = 9;
    return k;
}
static SharedCaptureBlock Block(uint64_t id, uint32_t bits)
{
    auto b = std::make_shared<CaptureImmutableBlock>();
    b->id = id;
    b->bytes = { uint8_t(bits), uint8_t(bits >> 8), uint8_t(bits >> 16),
                 uint8_t(bits >> 24) };
    return b;
}
static std::shared_ptr<CaptureOccurrence> Event(uint64_t id, uint64_t frame,
                                                uint32_t draw, uint32_t subject)
{
    auto e = std::make_shared<CaptureOccurrence>();
    e->event_id = id;
    e->summary.scope.title_id = 17;
    e->summary.key = { 2, 3, frame, draw, id };
    e->summary.shader_count = 1;
    e->summary.shaders[0] = Pixel();
    e->finished = e->emitted = e->inputs.complete = true;
    e->pending = false;
    e->geometry.positions = Block(id * 10, subject);
    e->geometry.position_count = 1;
    CaptureOwnedUniform u;
    u.stage = 2;
    u.name = "color";
    u.type = 1;
    u.components = u.count = 1;
    u.data = Block(id * 10 + 1, subject);
    e->inputs.uniforms.push_back(u);
    auto &t = e->inputs.textures[0];
    t.described = true;
    t.metadata.bound = 1;
    t.metadata.width = t.metadata.height = 1;
    t.metadata.coordinate_scale = 1;
    t.images.push_back({ 0, 0, { 1, 1, Block(id * 10 + 2, subject) } });
    e->inputs.sources[2] = Block(99, 0x70697865);
    e->inputs.registers.push_back({ "raster.mask", subject });
    return e;
}
static CaptureSessionSnapshot Recording()
{
    CaptureSessionSnapshot s;
    s.context.scope.title_id = 17;
    s.context.session_epoch = 2;
    s.context.renderer_epoch = 3;
    s.context.generation = 4;
    s.context.backend = 1;
    // B follows A in frame1; frame2 reorders them. Identical A draws in frame3
    // must remain ambiguous rather than becoming an inferred object identity.
    s.events = { Event(1, 1, 0, 10), Event(2, 1, 1, 20), Event(3, 2, 0, 20),
                 Event(4, 2, 1, 10), Event(5, 3, 0, 10), Event(6, 3, 1, 10) };
    return s;
}
static CaptureInspectionPage Finish(CaptureInspectionController &c,
                                    uint64_t job)
{
    assert(job);
    for (size_t i = 0; i < 10000; ++i) {
        c.Run(job, 1);
        auto page = c.Page(job);
        if (page.state != CaptureInspectionJobState::Running)
            return page;
    }
    assert(false);
    return {};
}
static void test_exact_diff_preserves_nan_signed_zero_and_missing_fields()
{
    auto s = Recording();
    auto a = std::make_shared<CaptureOccurrence>(*s.events[0]);
    auto b = std::make_shared<CaptureOccurrence>(*s.events[1]);
    a->inputs.uniforms[0].data = Block(100, 0x7fc00011);
    b->inputs.uniforms[0].data = Block(101, 0x7fc00012);
    a->inputs.registers = { { "pgraph.raw", 0 } };
    b->inputs.registers = { { "pgraph.raw", 0x80000000 } };
    a->inputs.textures[0].metadata.coordinate_scale = 0;
    uint32_t negative_zero = 0x80000000;
    std::memcpy(&b->inputs.textures[0].metadata.coordinate_scale,
                &negative_zero, 4);
    b->inputs.sources[1] = Block(102, 123);
    s.events = { a, b };
    CaptureInspectionController c;
    assert(c.Open(7, s));
    auto page = Finish(c, c.BeginWithinFrameComparison(1, 2));
    assert(page.state == CaptureInspectionJobState::Ready);
    bool uniform = false, reg = false, scale = false, source = false;
    for (const auto &f : page.fields) {
        assert(f.change != CaptureInspectionChange::Unchanged);
        if (f.path == "uniform/2/color/0/data") {
            uniform = true;
            assert(f.before.block->bytes[0] == 0x11 &&
                   f.after.block->bytes[0] == 0x12);
        }
        if (f.path == "register/pgraph.raw/0") {
            reg = true;
            assert(f.before.bits[3] == 0 && f.after.bits[3] == 0x80);
        }
        if (f.path == "texture/0/coordinate_scale") {
            scale = true;
            assert(f.before.bits[3] == 0 && f.after.bits[3] == 0x80);
        }
        if (f.path == "source/1") {
            source = true;
            assert(f.change == CaptureInspectionChange::Added);
        }
    }
    assert(uniform && reg && scale && source);
    assert(!c.BeginWithinFrameComparison(1, 4));
}
static void
test_tracks_require_confirmation_and_reordering_is_only_a_candidate()
{
    CaptureInspectionController c;
    assert(c.Open(7, Recording()));
    assert(!c.CreateTrack(1, "A", false));
    auto track = c.CreateTrack(1, "A", true);
    assert(track);
    auto candidates = Finish(c, c.BeginCandidates(track, 2));
    assert(candidates.candidates.size() == 1 &&
           candidates.candidates[0].event.event_id == 4);
    assert(c.Tracks()[0].frames[1].state == CaptureTrackFrameState::Candidate);
    assert(!c.BeginConfirmedTrackComparison(track, 2));
    assert(c.Confirm(track, 4, true));
    auto tracked = Finish(c, c.BeginConfirmedTrackComparison(track, 2));
    assert(tracked.relation == CaptureInspectionRelation::ConfirmedTrack);
    assert(tracked.before.event_id == 1 && tracked.after.event_id == 4);
    auto shader = Finish(c, c.BeginPriorShaderUseComparison(4, Pixel()));
    assert(shader.relation == CaptureInspectionRelation::PriorShaderUse);
    assert(shader.before.event_id == 3 && shader.after.event_id == 4);
    candidates = Finish(c, c.BeginCandidates(track, 3));
    assert(candidates.candidates.size() == 2);
    assert(c.Tracks()[0].frames[2].state == CaptureTrackFrameState::Ambiguous);
    assert(c.Confirm(track, 5, true));
    assert(!c.Confirm(track, 6, true));
    auto branch = c.Branch(track, 6, "A alternative", true);
    assert(branch && branch != track);
    assert(c.Tracks().size() == 2);
    auto trend = Finish(c, c.BeginTrend(track, "uniform/2/color/0/data"));
    assert(trend.trend.size() == 3 && trend.trend[1].event.event_id == 4);
    assert(trend.trend[1].comparable_to_previous &&
           !trend.trend[1].changed_from_previous);
}
static void test_jobs_pages_candidates_and_byte_work_are_bounded()
{
    CaptureInspectionSettings settings;
    settings.maximum_jobs = 1;
    settings.maximum_page = 1;
    CaptureInspectionController c;
    assert(c.Open(7, Recording(), settings));
    auto job = c.BeginWithinFrameComparison(1, 2, false);
    assert(job);
    assert(!c.BeginWithinFrameComparison(1, 2));
    c.Run(job, 1);
    auto page = c.Page(job, 0, UINT32_MAX);
    assert(page.state == CaptureInspectionJobState::Running &&
           page.processed <= 2 && page.fields.size() == 1);
    page = Finish(c, job);
    assert(page.result_count > 1 && page.has_more);
    c.Cancel(job);
    c.Drop(job);
    assert(c.BeginWithinFrameComparison(1, 2));
    settings = {};
    settings.maximum_results = 1;
    assert(c.Open(7, Recording(), settings));
    page = Finish(c, c.BeginWithinFrameComparison(1, 2));
    assert(page.state == CaptureInspectionJobState::Incomplete &&
           page.omitted_results == 1 && page.processed < page.total);
    settings = {};
    settings.maximum_candidates = 1;
    assert(c.Open(7, Recording(), settings));
    auto track = c.CreateTrack(1, "A", true);
    page = Finish(c, c.BeginCandidates(track, 3));
    assert(page.state == CaptureInspectionJobState::Incomplete &&
           page.candidates.size() == 1);
    auto tracks = c.Tracks();
    assert(tracks[0].frames.size() == 2);
    assert(tracks[0].frames[1].state == CaptureTrackFrameState::Incomplete);
    settings = {};
    settings.comparison_byte_budget = 1;
    assert(c.Open(7, Recording(), settings));
    page = Finish(c, c.BeginWithinFrameComparison(1, 2));
    assert(page.state == CaptureInspectionJobState::Incomplete &&
           page.compared_bytes <= 1);
    assert(page.fields.back().change == CaptureInspectionChange::Uncompared);
}
static void test_gaps_stale_jobs_and_archived_annotations_remain_explicit()
{
    auto recording = Recording();
    CaptureInspectionController c;
    assert(c.Open(7, recording));
    auto track = c.CreateTrack(1, "A", true);
    assert(c.Confirm(track, 5, true));
    auto page = Finish(c, c.BeginTrend(track, "uniform/2/color/0/data"));
    assert(page.trend.size() == 3 && page.trend[1].frame == 2);
    assert(page.trend[1].state == CaptureTrackFrameState::Missing &&
           !page.trend[1].value.present);
    assert(!page.trend[2].comparable_to_previous);
    auto stale = c.BeginCandidates(track, 2);
    assert(stale);
    assert(c.Confirm(track, 4, true));
    c.Run(stale, 1);
    assert(c.Page(stale).state == CaptureInspectionJobState::Cancelled);
    auto archived = c.Tracks();
    CaptureInspectionController reopened;
    assert(reopened.Open(7, recording));
    assert(reopened.RestoreTracks(archived));
    page = Finish(reopened, reopened.BeginConfirmedTrackComparison(track, 2));
    assert(page.before.event_id == 1 && page.after.event_id == 4);
    archived[0].frames[0].selected.recording_id = 8;
    assert(!reopened.RestoreTracks(archived));
    assert(reopened.Tracks()[0].frames[0].selected.recording_id == 7);
    assert(!reopened.Open(8, recording, CaptureInspectionSettings{ 0 }));
    assert(reopened.Reference(1).event_id == 0);
}
static void test_raw_stream_layout_and_sampler_changes_are_visible()
{
    auto s = Recording();
    auto a = std::make_shared<CaptureOccurrence>(*s.events[0]);
    auto b = std::make_shared<CaptureOccurrence>(*s.events[1]);
    CaptureOwnedBlob blob;
    blob.name = "vertex.ranges";
    blob.offset = UINT64_C(0x100000001);
    blob.data = Block(500, 0x7fc01234);
    a->inputs.blobs.push_back(blob);
    blob.offset++;
    blob.data = Block(501, 0x7fc04321);
    b->inputs.blobs.push_back(blob);
    b->inputs.textures[0].metadata.min_filter = 7;
    b->inputs.textures[0].metadata.face = 2;
    s.events = { a, b };
    CaptureInspectionController c;
    assert(c.Open(7, s));
    auto page = Finish(c, c.BeginWithinFrameComparison(1, 2));
    bool offset = false, data = false, sampler = false, face = false;
    for (const auto &field : page.fields) {
        if (field.path == "blob/vertex.ranges/0/offset") {
            offset = true;
            assert(field.before.bits.size() == 8 && field.before.bits[4] == 1 &&
                   field.after.bits[0] == 2);
        }
        if (field.path == "blob/vertex.ranges/0/data") {
            data = true;
            assert(field.before.block->bytes[0] == 0x34 &&
                   field.after.block->bytes[0] == 0x21);
        }
        if (field.path == "texture/0/min_filter")
            sampler = true;
        if (field.path == "texture/0/face")
            face = true;
    }
    assert(offset && data && sampler && face);
}
static void test_annotation_json_roundtrip_corruption_and_recording_ownership()
{
    auto recording = Recording();
    recording.context.scope_generation = UINT64_MAX;
    recording.context.current_frame = UINT64_MAX;
    CaptureInspectionController c;
    assert(c.Open(7, recording));
    auto track = c.CreateTrack(1, "A \"tracked\"\nsubject", true);
    assert(c.Confirm(track, 4, true));
    Finish(c, c.BeginCandidates(track, 3));
    assert(c.Branch(track, 6, "branch", true));
    std::string archive, error;
    assert(c.ExportAnnotations(&archive, &error));
    assert(archive.size() <= 4U * 1024U * 1024U);
    CaptureInspectionController reopened;
    assert(reopened.Open(7, recording));
    assert(reopened.RestoreAnnotations(archive, &error));
    auto tracks = reopened.Tracks();
    assert(tracks.size() == 2 && tracks[0].label == "A \"tracked\"\nsubject");
    assert(tracks[0].recording.context.scope_generation == UINT64_MAX);
    assert(tracks[0].recording.context.current_frame == UINT64_MAX);
    assert(tracks[0].frames[2].state == CaptureTrackFrameState::Ambiguous);
    assert(tracks[0].frames[2].candidates[0].event.event_id == 5);
    assert(tracks[1].parent_track_id == track &&
           tracks[1].frames[2].selected.event_id == 6);
    auto bad = nlohmann::json::parse(archive);
    bad["version"] = 2;
    assert(!reopened.RestoreAnnotations(bad.dump(), &error));
    assert(!error.empty());
    bad = nlohmann::json::parse(archive);
    bad["tracks"][0]["id"] = UINT64_MAX - 1;
    bad["tracks"][1]["parent"] = UINT64_MAX - 1;
    assert(!reopened.RestoreAnnotations(bad.dump(), &error));
    bad = nlohmann::json::parse(archive);
    bad["tracks"][0]["revision"] = UINT64_MAX;
    assert(!reopened.RestoreAnnotations(bad.dump(), &error));
    bad = nlohmann::json::parse(archive);
    bad["tracks"][0]["frames"][2]["candidates"][0]["event"]["event_id"] = 9999;
    assert(!reopened.RestoreAnnotations(bad.dump(), &error));
    bad = nlohmann::json::parse(archive);
    bad["tracks"][0]["frames"][0]["frame"] = -1;
    assert(!reopened.RestoreAnnotations(bad.dump(), &error));
    assert(!reopened.RestoreAnnotations("{", &error));
    assert(!reopened.RestoreAnnotations(
        std::string(4U * 1024U * 1024U + 1, ' '), &error));
    assert(reopened.Tracks().size() == 2 &&
           reopened.Tracks()[0].frames[2].state ==
               CaptureTrackFrameState::Ambiguous);
    CaptureInspectionController other;
    assert(other.Open(8, recording));
    assert(!other.RestoreAnnotations(archive, &error));
    assert(other.Tracks().empty());
    // The observed frame may advance while one recording remains open. Its
    // stable owner and epochs still match; preserve annotation-time raw
    // context.
    auto advanced = recording;
    advanced.context.current_frame = 42;
    CaptureInspectionController later;
    assert(later.Open(7, advanced));
    assert(later.RestoreAnnotations(archive, &error));
    assert(later.Tracks()[0].recording.context.current_frame == UINT64_MAX);
    assert(later.ExportAnnotations(&archive, &error));
    assert(later.RestoreAnnotations(archive, &error));
    assert(later.Tracks()[0].recording.context.current_frame == UINT64_MAX);
}
static void
test_refresh_keeps_only_unresolved_annotation_metadata_for_evicted_events()
{
    auto s = Recording();
    CaptureInspectionController c;
    assert(c.Open(7, s));
    auto track = c.CreateTrack(1, "A", true);
    assert(c.Confirm(track, 4, true));
    auto reduced = s;
    reduced.events.erase(reduced.events.begin(), reduced.events.begin() + 2);
    assert(c.Refresh(7, reduced));
    auto tracks = c.Tracks();
    assert(tracks[0].frames[0].selected.event_id == 1 &&
           !tracks[0].frames[0].evidence_retained);
    assert(c.Reference(1).event_id == 0 &&
           !c.BeginConfirmedTrackComparison(track, 2));
    auto trend = Finish(c, c.BeginTrend(track, "uniform/2/color/0/data"));
    assert(trend.trend[0].state == CaptureTrackFrameState::Incomplete &&
           !trend.trend[1].comparable_to_previous);
    std::string json, error;
    assert(c.ExportAnnotations(&json, &error));
    CaptureInspectionController reopened;
    assert(reopened.Open(7, reduced));
    assert(reopened.RestoreAnnotations(json, &error));
    assert(reopened.Tracks()[0].frames[0].selected.event_id == 1 &&
           !reopened.Tracks()[0].frames[0].evidence_retained);
    auto bad = nlohmann::json::parse(json);
    bad["tracks"][0]["frames"][0]["evidence_retained"] = true;
    assert(!reopened.RestoreAnnotations(bad.dump(), &error));
}
int main()
{
    test_exact_diff_preserves_nan_signed_zero_and_missing_fields();
    test_tracks_require_confirmation_and_reordering_is_only_a_candidate();
    test_jobs_pages_candidates_and_byte_work_are_bounded();
    test_gaps_stale_jobs_and_archived_annotations_remain_explicit();
    test_raw_stream_layout_and_sampler_changes_are_visible();
    test_annotation_json_roundtrip_corruption_and_recording_ownership();
    test_refresh_keeps_only_unresolved_annotation_metadata_for_evicted_events();
    std::cout << "capture inspection tests passed\n";
}

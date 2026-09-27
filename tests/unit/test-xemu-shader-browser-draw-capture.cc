// SPDX-License-Identifier: GPL-2.0-or-later
#include "../../ui/xui/shader-browser-draw-capture.hh"
#include "../../ui/xui/shader-browser-draw-request.hh"
#include "../../ui/xui/shader-browser-draw-request.h"
#include "../../hw/xbox/nv2a/pgraph/shader-browser-geometry-copy.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <limits>

using namespace xemu::shader_browser;

#define CHECK(expression) do { \
    if (!(expression)) { \
        std::cerr << __func__ << ':' << __LINE__ << ": " #expression "\n"; \
        std::exit(1); \
    } \
} while (false)

static CaptureDigest Digest(uint8_t value)
{
    CaptureDigest digest{};
    digest[0] = value;
    return digest;
}

static DrawCaptureSummary Draw(uint32_t id, uint8_t shader = 1)
{
    DrawCaptureSummary draw{};
    draw.key = {1, 2, 3, id};
    draw.scope.title_id = 0x12345678;
    draw.domain = DrawDomain::Geometry;
    draw.shader_count = 1;
    draw.shaders[0].stage = Stage::Pixel;
    draw.shaders[0].hash.bytes[0] = shader;
    draw.primitive_count = 1;
    draw.index_count = 3;
    DrawSegmentSummary segment{};
    segment.key.draw = draw.key;
    segment.geometry_digest = Digest(1);
    segment.transform_digest = Digest(1);
    segment.primitive_count = 1;
    segment.index_count = 3;
    draw.segments.push_back(segment);
    return draw;
}

static ResourceTouch Touch(ResourceAccess access, uint64_t read_version,
                           uint64_t write_version, uint64_t storage = 1)
{
    ResourceTouch touch{};
    touch.resource.kind = ResourceKind::Texture;
    touch.resource.guest = {0x1000, 256};
    touch.resource.storage_id = storage;
    touch.resource.storage_range = {0, 256};
    touch.access = access;
    touch.read_version = read_version;
    touch.write_version = write_version;
    return touch;
}

static ObjectRelationship Relation(const DrawCaptureSummary &a,
                                   const DrawCaptureSummary &b)
{
    return AnalyzeObjectRelationship(a.segments[0], a, b.segments[0], b);
}

static void TestShaderSeeds()
{
    auto a = Draw(1), b = Draw(2), c = Draw(3, 2);
    CHECK(FindDrawsUsingShader({a, b, c}, a.shaders[0]).size() == 2);
    const auto trace = TraceShaderObjectUsage({a, b, c}, a.shaders[0]);
    CHECK(trace.seed_draws.size() == 2);
    CHECK(trace.object_candidates.size() == 2);
}

static void TestInferenceIsNotMembership()
{
    auto a = Draw(1), b = Draw(2);
    CHECK(Relation(a, b).classification == ObjectLinkClass::SameObjectPass);
    CHECK(!Relation(a, b).automatic_group);
    CHECK(BuildObjectCandidates({a, b}).groups.size() == 2);
    b.segments[0].transform_digest = {};
    a.segments[0].skinning_digest = b.segments[0].skinning_digest = Digest(3);
    CHECK(!Relation(a, b).automatic_group);
}

static void TestScopeIsolation()
{
    auto a = Draw(1), original = Draw(2);
    a.segments[0].confirmed_object_id = 7;
    original.segments[0].confirmed_object_id = 7;
    for (unsigned variant = 0; variant < 6; ++variant) {
        auto b = original;
        switch (variant) {
        case 0: ++b.key.session_epoch; break;
        case 1: ++b.key.renderer_epoch; break;
        case 2: ++b.key.frame; break;
        case 3: ++b.scope.title_id; break;
        case 4: ++b.scope.executable_fingerprint_version; break;
        case 5: ++b.scope.executable_fingerprint[0]; break;
        }
        b.segments[0].key.draw = b.key;
        CHECK(!Relation(a, b).automatic_group);
        CHECK(BuildObjectCandidates({a, b}).groups.size() == 2);
    }
}

static void TestConfirmedPartsAndInstances()
{
    auto a = Draw(1), b = Draw(2, 2), c = Draw(3);
    a.segments[0].confirmed_object_id = b.segments[0].confirmed_object_id = 4;
    b.segments[0].transform_digest = Digest(8); // Other pass/camera.
    c.segments[0].confirmed_object_id = 5; // Same mesh, different instance.
    CHECK(Relation(a, b).automatic_group);
    CHECK(Relation(a, c).blocked);
    const auto result = BuildObjectCandidates({c, b, a});
    CHECK(result.groups.size() == 2);
    CHECK(result.groups[0].segments.size() == 2);
    CHECK(result.groups[0].membership_confirmed);
    CHECK(TraceShaderObjectUsage({a, b, c}, a.shaders[0])
              .object_candidates.size() == 2);
}

static void TestBatchedAndScreenSpace()
{
    auto batch = Draw(1);
    batch.batched_geometry_suspected = true;
    CHECK(BuildObjectCandidates({batch}).groups.empty());
    CHECK(BuildObjectCandidates({batch}).unresolved_segments.size() == 1);
    batch.segments[0].origin = DrawSegmentOrigin::ConnectedIndexComponent;
    auto second = batch.segments[0];
    second.key.segment = 1;
    batch.segments.push_back(second);
    CHECK(BuildObjectCandidates({batch}).groups.size() == 2);
    auto screen = Draw(2);
    screen.domain = DrawDomain::ScreenSpace;
    screen.segments[0].confirmed_object_id = 4;
    CHECK(BuildObjectCandidates({screen}).groups.empty());
    CHECK(TraceShaderObjectUsage({screen}, screen.shaders[0])
              .unresolved_segments.size() == 1);
}

static void TestSharedResourcesAreNotOwners()
{
    auto a = Draw(1), b = Draw(2);
    a.resources.push_back(Touch(ResourceAccess::Read, 1, 0));
    b.resources = a.resources;
    a.segments[0].geometry_digest = Digest(1);
    b.segments[0].geometry_digest = Digest(2);
    a.segments[0].transform_digest = Digest(1);
    b.segments[0].transform_digest = Digest(2);
    CHECK(Relation(a, b).classification == ObjectLinkClass::SharedResourceOnly);
    CHECK(!Relation(a, b).automatic_group);
    CHECK(BuildResourceDependencies({a, b}).edges.empty());
}

static void TestSegmentation()
{
    auto split = SegmentTriangleList({0, 1, 2, 10, 11, 12, 2, 3, 0, 4, 4, 5, 99});
    CHECK(split.islands.size() == 2);
    CHECK(split.islands[0].primitive_indices == std::vector<uint32_t>({0, 2}));
    CHECK(split.islands[1].primitive_indices == std::vector<uint32_t>({1}));
    CHECK(split.degenerate_primitives == std::vector<uint32_t>({3}));
    CHECK(split.trailing_index_count == 1);
    CHECK(SegmentTriangleList({}).islands.empty());
    CHECK(SegmentTriangleList({1, 1, 1}).islands.empty());
}

static void TestResourceVersionsAndViews()
{
    auto a = Draw(1), b = Draw(2, 2);
    a.resources.push_back(Touch(ResourceAccess::Write, 0, 1));
    a.resources[0].resource.kind = ResourceKind::ColorTarget;
    b.resources.push_back(Touch(ResourceAccess::Read, 1, 0));
    b.resources[0].resource.guest.address = 0x8000; // A resolved alias/view.
    auto graph = BuildResourceDependencies({b, a});
    CHECK(graph.edges.size() == 1);
    CHECK(graph.unresolved_reads.empty());
    CHECK(graph.edges[0].producer == a.key);
    CHECK(graph.edges[0].consumer == b.key);
    b.resources[0].resource.storage_id = 2;
    graph = BuildResourceDependencies({a, b});
    CHECK(graph.edges.empty());
    CHECK(graph.unresolved_reads.size() == 1);
    b.resources[0].resource.storage_id = 1;
    b.resources[0].read_version = 2;
    CHECK(BuildResourceDependencies({a, b}).edges.empty());
}

static void TestReadWriteAndTransitiveInfluence()
{
    auto a = Draw(1), b = Draw(2, 2), c = Draw(3, 2), unrelated = Draw(4, 2);
    a.resources.push_back(Touch(ResourceAccess::Write, 0, 1));
    b.resources.push_back(Touch(ResourceAccess::ReadWrite, 1, 2));
    c.resources.push_back(Touch(ResourceAccess::Read, 2, 0));
    unrelated.resources.push_back(Touch(ResourceAccess::Read, 8, 0, 5));
    auto graph = BuildResourceDependencies({c, unrelated, b, a});
    CHECK(graph.edges.size() == 2);
    const auto reached = TraceResourceInfluence(graph, {a.key});
    CHECK(reached == std::vector<DrawEventKey>({b.key, c.key}));
    const auto trace = TraceShaderObjectUsage({c, unrelated, b, a}, a.shaders[0]);
    CHECK(trace.object_candidates.size() == 1);
    CHECK(trace.potentially_affected_draws == reached);
    CHECK(trace.dependencies.unresolved_reads.size() == 1);
}

static void TestDependencyIsolationAndOrdering()
{
    auto a = Draw(1), b = Draw(2);
    a.resources.push_back(Touch(ResourceAccess::Write, 0, 1));
    b.resources.push_back(Touch(ResourceAccess::Read, 1, 0));
    b.key.session_epoch++;
    b.segments[0].key.draw = b.key;
    CHECK(BuildResourceDependencies({a, b}).edges.empty());
    b.key = {1, 2, 3, 0};
    b.segments[0].key.draw = b.key;
    CHECK(BuildResourceDependencies({a, b}).edges.empty());
    b.key = {1, 2, 4, 0}; // Explicit version may persist across frames.
    b.segments[0].key.draw = b.key;
    CHECK(BuildResourceDependencies({b, a}).edges.size() == 1);
    b.scope.title_id++;
    CHECK(BuildResourceDependencies({a, b}).edges.empty());
}

static void TestUnresolvedAndInvalidRanges()
{
    auto a = Draw(1), b = Draw(2);
    a.resources.push_back(Touch(ResourceAccess::Write, 0, 1));
    b.resources.push_back(Touch(ResourceAccess::Read, 0, 0));
    auto graph = BuildResourceDependencies({a, b});
    CHECK(graph.edges.empty());
    CHECK(graph.unresolved_reads.size() == 1);
    b.resources[0].read_version = 1;
    b.resources[0].resource.storage_range = {256, 1};
    CHECK(BuildResourceDependencies({a, b}).edges.empty());
    b.resources[0].resource.storage_range = {128, 256}; // Only half covered.
    graph = BuildResourceDependencies({a, b});
    CHECK(graph.edges.size() == 1);
    CHECK(graph.unresolved_reads.size() == 1);
    b.resources[0].resource.storage_range = {std::numeric_limits<uint64_t>::max(), 2};
    CHECK(BuildResourceDependencies({a, b}).edges.empty());
    b.resources[0].access = ResourceAccess::BindOnly;
    CHECK(BuildResourceDependencies({a, b}).unresolved_reads.empty());
}

static void TestDuplicateKeysAndLimits()
{
    auto a = Draw(1);
    CHECK(BuildObjectCandidates({a, a}).invalid_input);
    CHECK(BuildResourceDependencies({a, a}).invalid_input);
    std::vector<DrawCaptureSummary> too_many(kCaptureMaxAnalysisDraws + 1, a);
    CHECK(BuildObjectCandidates(too_many).limit_exceeded);
    CHECK(BuildResourceDependencies(too_many).limit_exceeded);
}

static void TestAmbiguousAndFragmentedProvenance()
{
    auto a = Draw(1), b = Draw(2), c = Draw(3);
    a.resources.push_back(Touch(ResourceAccess::Write, 0, 1));
    b.resources = a.resources;
    c.resources.push_back(Touch(ResourceAccess::Read, 1, 0));
    auto graph = BuildResourceDependencies({a, b, c});
    CHECK(graph.edges.empty());
    CHECK(graph.unresolved_reads.size() == 1);
    CHECK(graph.unresolved_reads[0].reason == DependencyGap::AmbiguousProducer);
    a.resources[0].resource.storage_range = {0, 128};
    b.resources[0].resource.storage_range = {128, 128};
    graph = BuildResourceDependencies({a, b, c});
    CHECK(graph.edges.size() == 2);
    CHECK(graph.unresolved_reads.empty());
}

static void TestMultipleObjectsAndDownstreamComposite()
{
    auto batch = Draw(1), composite = Draw(2, 2);
    batch.batched_geometry_suspected = true;
    batch.segments[0].origin = DrawSegmentOrigin::UserDefined;
    batch.segments[0].confirmed_object_id = 7;
    auto second = batch.segments[0];
    second.key.segment = 1;
    second.confirmed_object_id = 8;
    batch.segments.push_back(second);
    batch.resources.push_back(Touch(ResourceAccess::Write, 0, 1));
    composite.domain = DrawDomain::ScreenSpace;
    composite.resources.push_back(Touch(ResourceAccess::Read, 1, 0));
    const auto trace = TraceShaderObjectUsage({batch, composite}, batch.shaders[0]);
    CHECK(trace.seed_draws.size() == 1);
    CHECK(trace.object_candidates.size() == 2);
    CHECK(trace.potentially_affected_draws ==
          std::vector<DrawEventKey>({composite.key}));
}

static void TestReferenceIdentityAndBoundsSpace()
{
    auto a = Touch(ResourceAccess::Read, 1, 0).resource;
    a.descriptor_digest = Digest(1);
    a.content_digest = Digest(1);
    auto b = a;
    b.content_digest = Digest(2);
    CHECK(!SameResourceIdentity(a, b));
    b = a;
    b.storage_id++;
    CHECK(!SameResourceIdentity(a, b));
    b = a;
    b.guest.address++;
    CHECK(!SameResourceIdentity(a, b));
    ResourceIdentity empty{};
    empty.descriptor_digest = Digest(1);
    CHECK(!SameResourceIdentity(empty, empty));
    auto x = Draw(1), y = Draw(5);
    x.segments[0].bounds = y.segments[0].bounds =
        Bounds3::FromMinMax({0, 0, 0}, {1, 1, 1});
    x.segments[0].bounds_space_digest = Digest(1);
    y.segments[0].bounds_space_digest = Digest(2);
    CHECK(!(Relation(x, y).evidence & ObjectEvidenceOverlappingBounds));
}

static void TestValidationAndSegmentationLimit()
{
    auto draw = Draw(1);
    draw.segments.push_back(draw.segments[0]);
    CHECK(BuildObjectCandidates({draw}).invalid_input);
    draw = Draw(1);
    draw.resources.push_back(Touch(ResourceAccess::ReadWrite, 1, 1));
    CHECK(BuildResourceDependencies({draw}).invalid_input);
    const std::vector<uint32_t> oversized(kCaptureMaxSegmentationIndices + 1, 0);
    CHECK(SegmentTriangleList(oversized).limit_exceeded);
}

static void TestUpstreamInputsRemainSeparate()
{
    auto caster = Draw(1, 1), receiver = Draw(2, 2), composite = Draw(3, 3);
    caster.resources.push_back(Touch(ResourceAccess::Write, 0, 1));
    receiver.resources.push_back(Touch(ResourceAccess::ReadWrite, 1, 2));
    composite.resources.push_back(Touch(ResourceAccess::Read, 2, 0));
    composite.domain = DrawDomain::ScreenSpace;
    const auto trace = TraceShaderObjectUsage({caster, receiver, composite},
                                             receiver.shaders[0]);
    CHECK(trace.required_producer_draws == std::vector<DrawEventKey>({caster.key}));
    CHECK(trace.potentially_affected_draws ==
          std::vector<DrawEventKey>({composite.key}));
    CHECK(trace.object_candidates.size() == 1);
    CHECK(TraceResourceInputs(trace.dependencies, {composite.key}) ==
          std::vector<DrawEventKey>({caster.key, receiver.key}));
}

static void TestOneShotDrawRequest()
{
    DrawCaptureRequest request;
    auto captured = Draw(17);
    DrawRequestTarget target{};
    target.shader = captured.shaders[0];
    target.scope = captured.scope;
    target.scope_generation = 4;
    target.session_epoch = captured.key.session_epoch;
    target.renderer_epoch = captured.key.renderer_epoch;
    const uint64_t first = request.Arm(target);
    CHECK(first != 0);
    CHECK(request.Status().state == DrawRequestState::Armed);
    uint64_t token = 0;
    auto different = Draw(18, 2);
    CHECK(!request.Begin(4, 2, &different.shaders[0], 1, 3, 18, &token));
    CHECK(request.Status().state == DrawRequestState::Armed);
    CHECK(request.Begin(4, 2, &captured.shaders[0], 1, 3, 17, &token));
    CHECK(token == first);
    CHECK(!request.Begin(4, 2, &captured.shaders[0], 1, 3, 18, &token));
    request.Cancel();
    CHECK(!request.Complete(first, captured));
    CHECK(request.Status().state == DrawRequestState::Cancelled);

    const uint64_t second = request.Arm(target);
    CHECK(second > first);
    CHECK(!request.Begin(4, 3, &captured.shaders[0], 1, 3, 17, &token));
    CHECK(request.Status().state == DrawRequestState::Cancelled);
    const uint64_t third = request.Arm(target);
    CHECK(!request.Begin(5, 2, &captured.shaders[0], 1, 3, 17, &token));
    CHECK(request.Status().state == DrawRequestState::Cancelled);

    const uint64_t fourth = request.Arm(target);
    CHECK(request.Begin(4, 2, &captured.shaders[0], 1, 3, 17, &token));
    CHECK(token == fourth);
    CHECK(!request.Complete(third, captured));
    CHECK(request.Complete(fourth, captured));
    CHECK(request.Status().state == DrawRequestState::Ready);
    CHECK(request.Status().draw == captured.key);
    CHECK(request.CopyCaptured().completeness ==
          CaptureCompleteness::MetadataOnly);
}

static void TestSubmittedDrawBridge()
{
    const auto selected = Draw(21);
    XemuShaderDrawRequestSpec spec{};
    std::copy(selected.shaders[0].hash.bytes.begin(),
              selected.shaders[0].hash.bytes.end(), spec.identity_hash);
    spec.stage = XEMU_SHADER_BROWSER_STAGE_PIXEL;
    spec.scope.title_id = selected.scope.title_id;
    spec.scope_generation = 7;
    spec.session_epoch = selected.key.session_epoch;
    spec.renderer_epoch = selected.key.renderer_epoch;
    const uint64_t token = xemu_shader_draw_request_arm(&spec);
    CHECK(token != 0);
    CHECK(xemu_shader_draw_request_is_armed());
    XemuShaderDrawIdentity other{};
    other.stage = XEMU_SHADER_BROWSER_STAGE_PIXEL;
    other.identity_hash[0] = 2;
    CHECK(!xemu_shader_draw_request_wants(7, 2, &other, 1));
    CHECK(!xemu_shader_draw_request_submitted(7, 2, &other, 1, 3, 21,
                                              4, 3, 0, nullptr));
    XemuShaderDrawIdentity matching{};
    matching.stage = XEMU_SHADER_BROWSER_STAGE_PIXEL;
    matching.identity_hash[0] = 1;
    CHECK(xemu_shader_draw_request_wants(7, 2, &matching, 1));
    CHECK(xemu_shader_draw_request_submitted(7, 2, &matching, 1, 3, 21,
                                             4, 3, 0, nullptr));
    XemuShaderDrawRequestStatus status{};
    CHECK(xemu_shader_draw_request_copy_status(&status));
    CHECK(status.request_id == token);
    CHECK(status.state == XEMU_SHADER_DRAW_REQUEST_READY);
    CHECK(!xemu_shader_draw_request_is_armed());
    CHECK(status.frame == 3 && status.draw == 21);
    CHECK(!xemu_shader_draw_request_submitted(7, 2, &matching, 1, 3, 22,
                                              4, 3, 0, nullptr));
    xemu_shader_draw_request_cancel();
}

static void TestDrawGeometryOwnsItsBytes()
{
    XemuShaderDrawRequestSpec spec{};
    spec.identity_hash[0] = 1;
    spec.stage = XEMU_SHADER_BROWSER_STAGE_PIXEL;
    spec.scope.title_id = 0x12345678;
    spec.scope_generation = 8;
    spec.session_epoch = 1;
    spec.renderer_epoch = 2;
    CHECK(xemu_shader_draw_request_arm(&spec));
    XemuShaderDrawIdentity matching{};
    matching.stage = spec.stage;
    matching.identity_hash[0] = 1;
    float positions[12] = {0, 0, 0, 1, 1, 0, 0, 1, 0, 1, 0, 1};
    uint32_t indices[3] = {0, 1, 2};
    XemuShaderDrawGeometry geometry{};
    geometry.positions = positions;
    geometry.position_count = 3;
    geometry.indices = indices;
    geometry.index_count = 3;
    CHECK(xemu_shader_draw_request_submitted(8, 2, &matching, 1, 4, 22,
                                             5, 3, 3, &geometry));
    positions[0] = 42;
    indices[0] = 2;
    const auto owned = GetDrawCaptureRequest().CopyGeometry();
    CHECK(owned.positions.size() == 3);
    CHECK(owned.positions[0][0] == 0);
    CHECK(owned.indices == std::vector<uint32_t>({0, 1, 2}));
    CHECK(GetDrawCaptureRequest().CopyCaptured().completeness ==
          CaptureCompleteness::GeometrySnapshot);
    CHECK(GetDrawCaptureRequest().CopyCaptured().segments.empty());
    CHECK(GetDrawCaptureRequest().CopyCaptured().primitive_count == 1);
    xemu_shader_draw_request_cancel();
    CHECK(xemu_shader_draw_request_arm(&spec));
    geometry.indices = nullptr;
    geometry.index_count = 0;
    CHECK(xemu_shader_draw_request_submitted(8, 2, &matching, 1, 4, 23,
                                             5, 3, 0, &geometry));
    CHECK(GetDrawCaptureRequest().CopyGeometry().indices.empty());
    CHECK(GetDrawCaptureRequest().CopyCaptured().completeness ==
          CaptureCompleteness::MetadataOnly);
    xemu_shader_draw_request_cancel();
}

static void TestBoundedFloatPositionCopy()
{
    const float raw[15] = {1, 2, 3, 90, 90, 4, 5, 6, 91, 91,
                           7, 8, 9, 92, 92};
    float output[8]{};
    CHECK(xemu_shader_draw_copy_float_positions(
        reinterpret_cast<const uint8_t *>(raw), sizeof(raw), 1, 2,
        5 * sizeof(float), 3, output, std::size(output)));
    CHECK(output[0] == 4 && output[1] == 5 && output[2] == 6 &&
          output[3] == 1);
    CHECK(output[4] == 7 && output[5] == 8 && output[6] == 9 &&
          output[7] == 1);
    CHECK(!xemu_shader_draw_copy_float_positions(
        reinterpret_cast<const uint8_t *>(raw), sizeof(raw), 2, 2,
        5 * sizeof(float), 3, output, std::size(output)));
    CHECK(!xemu_shader_draw_copy_float_positions(
        reinterpret_cast<const uint8_t *>(raw), sizeof(raw), 0, 3,
        5 * sizeof(float), 3, output, std::size(output)));
}

int main()
{
    void (*tests[])() = {
        TestShaderSeeds, TestInferenceIsNotMembership, TestScopeIsolation,
        TestConfirmedPartsAndInstances, TestBatchedAndScreenSpace,
        TestSharedResourcesAreNotOwners, TestSegmentation,
        TestResourceVersionsAndViews, TestReadWriteAndTransitiveInfluence,
        TestDependencyIsolationAndOrdering, TestUnresolvedAndInvalidRanges,
        TestDuplicateKeysAndLimits, TestAmbiguousAndFragmentedProvenance,
        TestMultipleObjectsAndDownstreamComposite,
        TestReferenceIdentityAndBoundsSpace, TestValidationAndSegmentationLimit,
        TestUpstreamInputsRemainSeparate, TestOneShotDrawRequest,
        TestSubmittedDrawBridge, TestDrawGeometryOwnsItsBytes,
        TestBoundedFloatPositionCopy,
    };
    for (auto test : tests) {
        test();
    }
    std::cout << "shader draw/object capture: " << std::size(tests)
              << " cases passed\n";
}

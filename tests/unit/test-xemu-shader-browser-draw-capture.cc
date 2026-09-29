// SPDX-License-Identifier: GPL-2.0-or-later
#include "../../ui/xui/shader-browser-draw-capture.hh"
#include "../../ui/xui/shader-browser-draw-request.hh"
#include "../../ui/xui/shader-browser-draw-request.h"
#include "../../ui/xui/shader-browser-capture-session.hh"
#include "../../hw/xbox/nv2a/pgraph/shader-browser-geometry-copy.h"
#include "../../hw/xbox/nv2a/pgraph/shader-browser-command-copy.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <iterator>
#include <limits>
#include <condition_variable>
#include <mutex>
#include <thread>

using namespace xemu::shader_browser;

#define CHECK(expression)                                                      \
    do {                                                                       \
        if (!(expression)) {                                                   \
            std::cerr << __func__ << ':' << __LINE__ << ": " #expression "\n"; \
            std::exit(1);                                                      \
        }                                                                      \
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
    draw.key = { 1, 2, 3, id };
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
    touch.resource.guest = { 0x1000, 256 };
    touch.resource.storage_id = storage;
    touch.resource.storage_range = { 0, 256 };
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
    CHECK(FindDrawsUsingShader({ a, b, c }, a.shaders[0]).size() == 2);
    const auto trace = TraceShaderObjectUsage({ a, b, c }, a.shaders[0]);
    CHECK(trace.seed_draws.size() == 2);
    CHECK(trace.object_candidates.size() == 2);
}

static void TestInferenceIsNotMembership()
{
    auto a = Draw(1), b = Draw(2);
    CHECK(Relation(a, b).classification == ObjectLinkClass::SameObjectPass);
    CHECK(!Relation(a, b).automatic_group);
    CHECK(BuildObjectCandidates({ a, b }).groups.size() == 2);
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
        case 0:
            ++b.key.session_epoch;
            break;
        case 1:
            ++b.key.renderer_epoch;
            break;
        case 2:
            ++b.key.frame;
            break;
        case 3:
            ++b.scope.title_id;
            break;
        case 4:
            ++b.scope.executable_fingerprint_version;
            break;
        case 5:
            ++b.scope.executable_fingerprint[0];
            break;
        }
        b.segments[0].key.draw = b.key;
        CHECK(!Relation(a, b).automatic_group);
        CHECK(BuildObjectCandidates({ a, b }).groups.size() == 2);
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
    const auto result = BuildObjectCandidates({ c, b, a });
    CHECK(result.groups.size() == 2);
    CHECK(result.groups[0].segments.size() == 2);
    CHECK(result.groups[0].membership_confirmed);
    CHECK(TraceShaderObjectUsage({ a, b, c }, a.shaders[0])
              .object_candidates.size() == 2);
}

static void TestBatchedAndScreenSpace()
{
    auto batch = Draw(1);
    batch.batched_geometry_suspected = true;
    CHECK(BuildObjectCandidates({ batch }).groups.empty());
    CHECK(BuildObjectCandidates({ batch }).unresolved_segments.size() == 1);
    batch.segments[0].origin = DrawSegmentOrigin::ConnectedIndexComponent;
    auto second = batch.segments[0];
    second.key.segment = 1;
    batch.segments.push_back(second);
    CHECK(BuildObjectCandidates({ batch }).groups.size() == 2);
    auto screen = Draw(2);
    screen.domain = DrawDomain::ScreenSpace;
    screen.segments[0].confirmed_object_id = 4;
    CHECK(BuildObjectCandidates({ screen }).groups.empty());
    CHECK(TraceShaderObjectUsage({ screen }, screen.shaders[0])
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
    CHECK(BuildResourceDependencies({ a, b }).edges.empty());
}

static void TestSegmentation()
{
    auto split =
        SegmentTriangleList({ 0, 1, 2, 10, 11, 12, 2, 3, 0, 4, 4, 5, 99 });
    CHECK(split.islands.size() == 2);
    CHECK(split.islands[0].primitive_indices ==
          std::vector<uint32_t>({ 0, 2 }));
    CHECK(split.islands[1].primitive_indices == std::vector<uint32_t>({ 1 }));
    CHECK(split.degenerate_primitives == std::vector<uint32_t>({ 3 }));
    CHECK(split.trailing_index_count == 1);
    CHECK(SegmentTriangleList({}).islands.empty());
    CHECK(SegmentTriangleList({ 1, 1, 1 }).islands.empty());
}

static void TestResourceVersionsAndViews()
{
    auto a = Draw(1), b = Draw(2, 2);
    a.resources.push_back(Touch(ResourceAccess::Write, 0, 1));
    a.resources[0].resource.kind = ResourceKind::ColorTarget;
    b.resources.push_back(Touch(ResourceAccess::Read, 1, 0));
    b.resources[0].resource.guest.address = 0x8000; // A resolved alias/view.
    auto graph = BuildResourceDependencies({ b, a });
    CHECK(graph.edges.size() == 1);
    CHECK(graph.unresolved_reads.empty());
    CHECK(graph.edges[0].producer == a.key);
    CHECK(graph.edges[0].consumer == b.key);
    b.resources[0].resource.storage_id = 2;
    graph = BuildResourceDependencies({ a, b });
    CHECK(graph.edges.empty());
    CHECK(graph.unresolved_reads.size() == 1);
    b.resources[0].resource.storage_id = 1;
    b.resources[0].read_version = 2;
    CHECK(BuildResourceDependencies({ a, b }).edges.empty());
}

static void TestReadWriteAndTransitiveInfluence()
{
    auto a = Draw(1), b = Draw(2, 2), c = Draw(3, 2), unrelated = Draw(4, 2);
    a.resources.push_back(Touch(ResourceAccess::Write, 0, 1));
    b.resources.push_back(Touch(ResourceAccess::ReadWrite, 1, 2));
    c.resources.push_back(Touch(ResourceAccess::Read, 2, 0));
    unrelated.resources.push_back(Touch(ResourceAccess::Read, 8, 0, 5));
    auto graph = BuildResourceDependencies({ c, unrelated, b, a });
    CHECK(graph.edges.size() == 2);
    const auto reached = TraceResourceInfluence(graph, { a.key });
    CHECK(reached == std::vector<DrawEventKey>({ b.key, c.key }));
    const auto trace =
        TraceShaderObjectUsage({ c, unrelated, b, a }, a.shaders[0]);
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
    CHECK(BuildResourceDependencies({ a, b }).edges.empty());
    b.key = { 1, 2, 3, 0 };
    b.segments[0].key.draw = b.key;
    CHECK(BuildResourceDependencies({ a, b }).edges.empty());
    b.key = { 1, 2, 4, 0 }; // Explicit version may persist across frames.
    b.segments[0].key.draw = b.key;
    CHECK(BuildResourceDependencies({ b, a }).edges.size() == 1);
    b.scope.title_id++;
    CHECK(BuildResourceDependencies({ a, b }).edges.empty());
}

static void TestUnresolvedAndInvalidRanges()
{
    auto a = Draw(1), b = Draw(2);
    a.resources.push_back(Touch(ResourceAccess::Write, 0, 1));
    b.resources.push_back(Touch(ResourceAccess::Read, 0, 0));
    auto graph = BuildResourceDependencies({ a, b });
    CHECK(graph.edges.empty());
    CHECK(graph.unresolved_reads.size() == 1);
    b.resources[0].read_version = 1;
    b.resources[0].resource.storage_range = { 256, 1 };
    CHECK(BuildResourceDependencies({ a, b }).edges.empty());
    b.resources[0].resource.storage_range = { 128, 256 }; // Only half covered.
    graph = BuildResourceDependencies({ a, b });
    CHECK(graph.edges.size() == 1);
    CHECK(graph.unresolved_reads.size() == 1);
    b.resources[0].resource.storage_range = {
        std::numeric_limits<uint64_t>::max(), 2
    };
    CHECK(BuildResourceDependencies({ a, b }).edges.empty());
    b.resources[0].access = ResourceAccess::BindOnly;
    CHECK(BuildResourceDependencies({ a, b }).unresolved_reads.empty());
}

static void TestDuplicateKeysAndLimits()
{
    auto a = Draw(1);
    CHECK(BuildObjectCandidates({ a, a }).invalid_input);
    CHECK(BuildResourceDependencies({ a, a }).invalid_input);
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
    auto graph = BuildResourceDependencies({ a, b, c });
    CHECK(graph.edges.empty());
    CHECK(graph.unresolved_reads.size() == 1);
    CHECK(graph.unresolved_reads[0].reason == DependencyGap::AmbiguousProducer);
    a.resources[0].resource.storage_range = { 0, 128 };
    b.resources[0].resource.storage_range = { 128, 128 };
    graph = BuildResourceDependencies({ a, b, c });
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
    const auto trace =
        TraceShaderObjectUsage({ batch, composite }, batch.shaders[0]);
    CHECK(trace.seed_draws.size() == 1);
    CHECK(trace.object_candidates.size() == 2);
    CHECK(trace.potentially_affected_draws ==
          std::vector<DrawEventKey>({ composite.key }));
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
        Bounds3::FromMinMax({ 0, 0, 0 }, { 1, 1, 1 });
    x.segments[0].bounds_space_digest = Digest(1);
    y.segments[0].bounds_space_digest = Digest(2);
    CHECK(!(Relation(x, y).evidence & ObjectEvidenceOverlappingBounds));
}

static void TestValidationAndSegmentationLimit()
{
    auto draw = Draw(1);
    draw.segments.push_back(draw.segments[0]);
    CHECK(BuildObjectCandidates({ draw }).invalid_input);
    draw = Draw(1);
    draw.resources.push_back(Touch(ResourceAccess::ReadWrite, 1, 1));
    CHECK(BuildResourceDependencies({ draw }).invalid_input);
    const std::vector<uint32_t> oversized(kCaptureMaxSegmentationIndices + 1,
                                          0);
    CHECK(SegmentTriangleList(oversized).limit_exceeded);
}

static void TestUpstreamInputsRemainSeparate()
{
    auto caster = Draw(1, 1), receiver = Draw(2, 2), composite = Draw(3, 3);
    caster.resources.push_back(Touch(ResourceAccess::Write, 0, 1));
    receiver.resources.push_back(Touch(ResourceAccess::ReadWrite, 1, 2));
    composite.resources.push_back(Touch(ResourceAccess::Read, 2, 0));
    composite.domain = DrawDomain::ScreenSpace;
    const auto trace = TraceShaderObjectUsage({ caster, receiver, composite },
                                              receiver.shaders[0]);
    CHECK(trace.required_producer_draws ==
          std::vector<DrawEventKey>({ caster.key }));
    CHECK(trace.potentially_affected_draws ==
          std::vector<DrawEventKey>({ composite.key }));
    CHECK(trace.object_candidates.size() == 1);
    CHECK(TraceResourceInputs(trace.dependencies, { composite.key }) ==
          std::vector<DrawEventKey>({ caster.key, receiver.key }));
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
    CHECK(!xemu_shader_draw_request_claim(7, 2, &other, 1, 3, 21, 1));
    XemuShaderDrawIdentity matching{};
    matching.stage = XEMU_SHADER_BROWSER_STAGE_PIXEL;
    matching.identity_hash[0] = 1;
    const uint64_t claim =
        xemu_shader_draw_request_claim(7, 2, &matching, 1, 3, 21, 1);
    CHECK(claim == token);
    CHECK(xemu_shader_draw_request_finish(claim, 1, 4, 3, 0));
    XemuShaderDrawRequestStatus status{};
    CHECK(xemu_shader_draw_request_copy_status(&status));
    CHECK(status.request_id == token);
    CHECK(status.state == XEMU_SHADER_DRAW_REQUEST_READY);
    CHECK(!xemu_shader_draw_request_is_armed());
    CHECK(status.frame == 3 && status.draw == 21);
    CHECK(!xemu_shader_draw_request_finish(claim, 1, 4, 3, 0));
    xemu_shader_draw_request_cancel();
}

static void TestSearchUntilSupportedGeometry()
{
    XemuShaderDrawRequestSpec spec{};
    spec.identity_hash[0] = 1;
    spec.stage = XEMU_SHADER_BROWSER_STAGE_PIXEL;
    spec.scope.title_id = 1;
    spec.scope_generation = 8;
    spec.session_epoch = 1;
    spec.renderer_epoch = 2;
    spec.require_geometry = 1;
    XemuShaderDrawIdentity identity{};
    identity.identity_hash[0] = 1;
    identity.stage = spec.stage;
    const uint64_t request = xemu_shader_draw_request_arm(&spec);
    const uint64_t first =
        xemu_shader_draw_request_claim(8, 2, &identity, 1, 1, 1, 1);
    CHECK(first);
    CHECK(xemu_shader_draw_request_finish(first, 1, 5, 3, 0));
    XemuShaderDrawRequestStatus status{};
    CHECK(xemu_shader_draw_request_copy_status(&status));
    CHECK(status.request_id == request);
    CHECK(status.state == XEMU_SHADER_DRAW_REQUEST_ARMED);
    CHECK(status.skipped_draws == 1);
    CHECK(GetDrawCaptureRequest().CopyCaptured().shader_count == 0);
    const uint64_t second =
        xemu_shader_draw_request_claim(8, 2, &identity, 1, 1, 2, 2);
    CHECK(second && second != first);
    const float positions[] = { 0, 0, 0, 1, 1, 0, 0, 1, 0, 1, 0, 1 };
    const uint32_t indices[] = { 0, 1, 2 };
    const XemuShaderDrawGeometry geometry{ positions, 3, indices, 3 };
    CHECK(!xemu_shader_draw_request_stage_geometry(first, &geometry));
    CHECK(!xemu_shader_draw_request_finish(first, 1, 5, 3, 3));
    CHECK(xemu_shader_draw_request_stage_geometry(second, &geometry));
    CHECK(xemu_shader_draw_request_finish(second, 1, 5, 3, 3));
    CHECK(xemu_shader_draw_request_copy_status(&status));
    CHECK(status.request_id == request && status.skipped_draws == 1);
    CHECK(status.state == XEMU_SHADER_DRAW_REQUEST_READY);
    CHECK(status.draw == 2 && status.submission == 2);
    xemu_shader_draw_request_cancel();
    CHECK(!xemu_shader_draw_request_is_armed());
}

static void TestDrawInputsWaitForEmissionAndOwnTheirBytes()
{
    XemuShaderDrawRequestSpec spec{};
    spec.identity_hash[0] = 1;
    spec.stage = XEMU_SHADER_BROWSER_STAGE_PIXEL;
    spec.scope.title_id = 1;
    spec.scope_generation = 8;
    spec.session_epoch = 1;
    spec.renderer_epoch = 2;
    spec.require_geometry = 1;
    spec.capture_inputs = 1;
    XemuShaderDrawIdentity identity{};
    identity.identity_hash[0] = 1;
    identity.stage = spec.stage;
    const uint64_t request = xemu_shader_draw_request_arm(&spec);
    const uint64_t token =
        xemu_shader_draw_request_claim(8, 2, &identity, 1, 1, 1, 1);
    CHECK(token == request && xemu_shader_draw_request_wants_inputs(token));
    const float positions[] = { 0, 0, 0, 1, 1, 0, 0, 1, 0, 1, 0, 1 };
    const uint32_t indices[] = { 0, 1, 2 };
    const XemuShaderDrawGeometry geometry{ positions, 3, indices, 3 };
    CHECK(xemu_shader_draw_request_stage_geometry(token, &geometry));
    CHECK(xemu_shader_draw_request_has_geometry(token));
    uint8_t pixels[16] = { 1, 2, 3, 4 };
    XemuShaderDrawImage image{ 2, 2, pixels, sizeof(pixels) };
    CHECK(xemu_shader_draw_request_stage_image(token, 1, &image));
    XemuShaderDrawTexture texture{};
    texture.bound = 1;
    texture.width = 2;
    texture.height = 2;
    texture.depth = 1;
    texture.face_count = 1;
    texture.mip_levels = 1;
    texture.image = image;
    CHECK(xemu_shader_draw_request_stage_texture(token, &texture));
    float constants[4] = { 0.25f, 0.5f, 0.75f, 1 };
    XemuShaderDrawUniform uniform{
        2,         "consts",         XEMU_SHADER_DRAW_UNIFORM_FLOAT, 4, 1,
        constants, sizeof(constants)
    };
    CHECK(xemu_shader_draw_request_stage_uniform(token, &uniform));
    std::string source = "void main() {}";
    CHECK(xemu_shader_draw_request_stage_source(token, 2, source.data(),
                                                source.size()));
    CHECK(xemu_shader_draw_request_stage_register(token, "CONTROL_0", 123));
    pixels[0] = 42;
    constants[0] = 42;
    source[0] = 'X';
    CHECK(xemu_shader_draw_request_finish(token, 1, 5, 3, 3));
    CHECK(GetDrawCaptureRequest().Status().state ==
          DrawRequestState::Capturing);
    CHECK(GetDrawCaptureRequest().CopyInputs().before.rgba.empty());
    CHECK(xemu_shader_draw_request_stage_image(token, 0, &image));
    CHECK(xemu_shader_draw_request_inputs_complete(token));
    CHECK(GetDrawCaptureRequest().Status().state == DrawRequestState::Ready);
    const auto owned = GetDrawCaptureRequest().CopyInputs();
    CHECK(owned.complete && owned.before.rgba[0] == 1 &&
          owned.after.rgba[0] == 42);
    CHECK(owned.textures[0].images[0].image.rgba[0] == 1);
    CHECK(owned.textures[0].metadata.image.rgba == nullptr);
    float stored = 0;
    std::memcpy(&stored, owned.uniforms[0].data.data(), sizeof(stored));
    CHECK(stored == 0.25f && owned.sources[2] == "void main() {}" &&
          owned.registers[0].value == 123);
    CaptureSessionContext context;
    context.scope = GetDrawCaptureRequest().CopyCaptured().scope;
    context.session_epoch = 1;
    context.renderer_epoch = 2;
    context.generation = request;
    context.backend = 1;
    const auto occurrence = GetDrawCaptureRequest().CopyOccurrence(&context);
    CHECK(occurrence && !occurrence->pending && occurrence->emitted);
    CHECK(occurrence->summary.key ==
          GetDrawCaptureRequest().CopyCaptured().key);
    CHECK(occurrence->inputs.before.rgba->bytes[0] == 1 &&
          occurrence->inputs.after.rgba->bytes[0] == 42);
    CHECK(occurrence->inputs.textures[0].metadata.image.rgba == nullptr);
    CHECK(occurrence->CopyGeometry().indices ==
          std::vector<uint32_t>({ 0, 1, 2 }));
    CaptureSessionSnapshot snapshot;
    snapshot.context = context;
    snapshot.state = CaptureSessionState::Ready;
    snapshot.events = { occurrence };
    snapshot.total_events = 1;
    snapshot.first_frame = snapshot.last_frame = occurrence->summary.key.frame;
    const auto path = std::filesystem::temp_directory_path() /
                      "xemu-single-draw-fitted-context";
    std::filesystem::remove_all(path);
    std::string error;
    CHECK(CaptureSession::SaveSnapshot(snapshot, path, &error));
    CaptureSessionSnapshot reopened;
    const bool reopened_ok = CaptureSession::Reopen(path, &reopened, &error);
    if (!reopened_ok)
        std::cerr << error << "\n";
    CHECK(reopened_ok);
    CHECK(reopened.context.scope_generation == 8 &&
          reopened.context.current_frame == occurrence->summary.key.frame);
    CHECK(reopened.events[0]->CopyGeometry().indices ==
          occurrence->CopyGeometry().indices);
    std::filesystem::remove_all(path);
    xemu_shader_draw_request_cancel();
    CHECK(!GetDrawCaptureRequest().CopyOccurrence());
    CHECK(occurrence->inputs.before.rgba->bytes[0] == 1);
    CHECK(!xemu_shader_draw_request_inputs_complete(token));
    CHECK(!xemu_shader_draw_request_stage_image(token, 1, &image));
    const auto next = xemu_shader_draw_request_arm(&spec);
    CHECK(next > token);
    const auto next_token =
        xemu_shader_draw_request_claim(8, 2, &identity, 1, 1, 2, 2);
    image.width = 2049;
    CHECK(!xemu_shader_draw_request_stage_image(next_token, 1, &image));
    texture.slot = 4;
    CHECK(!xemu_shader_draw_request_stage_texture(next_token, &texture));
    uniform.byte_count = 15;
    CHECK(!xemu_shader_draw_request_stage_uniform(next_token, &uniform));
    xemu_shader_draw_request_cancel();
    CHECK(GetDrawCaptureRequest().CopyInputs().textures[0].images.empty());
}

static void TestSearchRejectsDegenerateAndTracksReasons()
{
    DrawCaptureRequest request;
    DrawRequestTarget target;
    target.shader = Draw(1).shaders[0];
    target.scope.title_id = 1;
    target.scope_generation = 8;
    target.session_epoch = 1;
    target.renderer_epoch = 2;
    target.goal = XEMU_SHADER_CAPTURE_PREVIEWABLE_GEOMETRY;
    target.capture_inputs = true;
    CHECK(request.Arm(target));
    uint64_t token = 0;
    CHECK(request.Begin(8, 2, &target.shader, 1, 1, 1, &token));
    OwnedDrawGeometry degenerate;
    degenerate.positions = { { 0, 0, 0, 1 }, { 1, 0, 0, 1 }, { 2, 0, 0, 1 } };
    degenerate.indices = { 0, 1, 2 };
    CHECK(!request.StageGeometry(token, degenerate));
    CHECK(!request.HasGeometry(token));
    CHECK(request.Finish(token, true, 5, 3, 3));
    CHECK(request.Status().state == DrawRequestState::Capturing);
    CHECK(!request.Finish(token, true, 5, 3, 3));
    CHECK(request.InputsComplete(token));
    CHECK(request.Status().state == DrawRequestState::Armed);
    CHECK(request.Status()
              .rejection_counts[XEMU_SHADER_CAPTURE_REJECT_DEGENERATE] == 1);
    const auto old = token;
    CHECK(request.Begin(8, 2, &target.shader, 1, 1, 2, &token));
    CHECK(!request.NoteRejection(old, XEMU_SHADER_CAPTURE_REJECT_BUDGET));
    CHECK(
        request.NoteRejection(token, XEMU_SHADER_CAPTURE_REJECT_VERTEX_FORMAT));
    CHECK(request.Finish(token, true, 5, 3, 0));
    CHECK(request.InputsComplete(token));
    CHECK(request.Status()
              .rejection_counts[XEMU_SHADER_CAPTURE_REJECT_VERTEX_FORMAT] == 1);
    CHECK(request.Begin(8, 2, &target.shader, 1, 1, 3, &token));
    CHECK(request.Finish(token, false, 5, 3, 0));
    CHECK(request.Status()
              .rejection_counts[XEMU_SHADER_CAPTURE_REJECT_NOT_EMITTED] == 1);
    CHECK(request.Begin(8, 2, &target.shader, 1, 1, 4, &token));
    degenerate.positions[2] = { 0, 1, 0, 1 };
    CHECK(request.StageGeometry(token, degenerate));
    CHECK(request.Finish(token, true, 5, 3, 3));
    CHECK(request.InputsComplete(token));
    const auto status = request.Status();
    CHECK(status.state == DrawRequestState::Ready);
    CHECK(status.matching_draws == 4 && status.skipped_draws == 3);
    CHECK(status.rejection_counts[XEMU_SHADER_CAPTURE_REJECT_BUDGET] == 0);
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
    const uint64_t claim = xemu_shader_draw_request_arm(&spec);
    CHECK(claim);
    XemuShaderDrawIdentity matching{};
    matching.stage = spec.stage;
    matching.identity_hash[0] = 1;
    float positions[12] = { 0, 0, 0, 1, 1, 0, 0, 1, 0, 1, 0, 1 };
    uint32_t indices[3] = { 0, 1, 2 };
    XemuShaderDrawGeometry geometry{};
    geometry.positions = positions;
    geometry.position_count = 3;
    geometry.indices = indices;
    geometry.index_count = 3;
    CHECK(xemu_shader_draw_request_claim(8, 2, &matching, 1, 4, 22, 2) ==
          claim);
    CHECK(xemu_shader_draw_request_stage_geometry(claim, &geometry));
    positions[0] = 42;
    indices[0] = 2;
    CHECK(GetDrawCaptureRequest().CopyGeometry().positions.empty());
    CHECK(xemu_shader_draw_request_finish(claim, 1, 5, 3, 3));
    const auto owned = GetDrawCaptureRequest().CopyGeometry();
    CHECK(owned.positions.size() == 3);
    CHECK(owned.positions[0][0] == 0);
    CHECK(owned.indices == std::vector<uint32_t>({ 0, 1, 2 }));
    CHECK(GetDrawCaptureRequest().CopyCaptured().completeness ==
          CaptureCompleteness::GeometrySnapshot);
    CHECK(GetDrawCaptureRequest().CopyCaptured().segments.empty());
    CHECK(GetDrawCaptureRequest().CopyCaptured().primitive_count == 1);
    xemu_shader_draw_request_cancel();
    const uint64_t second = xemu_shader_draw_request_arm(&spec);
    CHECK(second);
    geometry.indices = nullptr;
    geometry.index_count = 0;
    CHECK(xemu_shader_draw_request_claim(8, 2, &matching, 1, 4, 23, 3) ==
          second);
    CHECK(xemu_shader_draw_request_stage_geometry(second, &geometry));
    CHECK(xemu_shader_draw_request_finish(second, 1, 5, 3, 0));
    CHECK(GetDrawCaptureRequest().CopyGeometry().indices.empty());
    CHECK(GetDrawCaptureRequest().CopyCaptured().completeness ==
          CaptureCompleteness::MetadataOnly);
    xemu_shader_draw_request_cancel();
}

static void TestClaimCancelRearmInterleaving()
{
    XemuShaderDrawRequestSpec spec{};
    spec.identity_hash[0] = 1;
    spec.stage = XEMU_SHADER_BROWSER_STAGE_PIXEL;
    spec.scope.title_id = 1;
    spec.scope_generation = 8;
    spec.session_epoch = 1;
    spec.renderer_epoch = 2;
    XemuShaderDrawIdentity identity{};
    identity.identity_hash[0] = 1;
    identity.stage = spec.stage;
    const uint64_t old = xemu_shader_draw_request_arm(&spec);
    std::mutex mutex;
    std::condition_variable changed;
    bool claimed = false, rearmed = false;
    std::thread renderer([&] {
        CHECK(xemu_shader_draw_request_claim(8, 2, &identity, 1, 1, 1, 1) ==
              old);
        std::unique_lock<std::mutex> lock(mutex);
        claimed = true;
        changed.notify_one();
        changed.wait(lock, [&] { return rearmed; });
        lock.unlock();
        CHECK(!xemu_shader_draw_request_finish(old, 1, 5, 3, 0));
    });
    std::unique_lock<std::mutex> lock(mutex);
    changed.wait(lock, [&] { return claimed; });
    xemu_shader_draw_request_cancel();
    const uint64_t next = xemu_shader_draw_request_arm(&spec);
    CHECK(next > old);
    rearmed = true;
    changed.notify_one();
    lock.unlock();
    renderer.join();
    CHECK(GetDrawCaptureRequest().Status().state == DrawRequestState::Armed);
    CHECK(xemu_shader_draw_request_claim(8, 2, &identity, 1, 1, 2, 2) == next);
    // An unsuccessful emission must not satisfy the request.
    CHECK(xemu_shader_draw_request_finish(next, 0, 5, 3, 0));
    CHECK(GetDrawCaptureRequest().Status().state == DrawRequestState::Armed);
    CHECK(xemu_shader_draw_request_claim(9, 2, &identity, 1, 1, 3, 3) == 0);
    CHECK(GetDrawCaptureRequest().Status().state ==
          DrawRequestState::Cancelled);
}

static void TestSubmittedGeometryRanges()
{
    XemuShaderDrawLayout layout{};
    int32_t starts[] = { 0, 4 };
    int32_t counts[] = { 4, 2 };
    CHECK(xemu_shader_draw_layout_arrays(&layout, starts, counts, 2));
    CHECK(layout.index_count == 3);
    CHECK(layout.first_vertex == 0 && layout.vertex_count == 3);
    CHECK(layout.indices[0] == 0 && layout.indices[2] == 2);
    counts[1] = 3;
    CHECK(xemu_shader_draw_layout_arrays(&layout, starts, counts, 2));
    CHECK(layout.index_count == 6);
    CHECK(layout.indices[3] == 4 && layout.indices[5] == 6);
    const uint32_t indices[] = { 2, 4, 6, 1000000 };
    CHECK(xemu_shader_draw_layout_elements(&layout, indices, 4));
    CHECK(layout.first_vertex == 2 && layout.vertex_count == 5);
    CHECK(layout.index_count == 3 && layout.indices[2] == 4);
    CHECK(!xemu_shader_draw_layout_elements(&layout, indices, 4097 * 3));
}

static void TestFilledStripDiagnosticLayout()
{
    XemuShaderDrawLayout layout{};
    const int32_t start = 10, count = 6;
    // A submitted strip needs four triangles, including alternating winding.
    CHECK(xemu_shader_draw_layout_arrays_topology(
        &layout, NV097_SET_BEGIN_END_OP_TRIANGLE_STRIP, &start, &count, 1));
    const std::vector<uint32_t> expected = {
        0, 1, 2, 2, 1, 3, 2, 3, 4, 4, 3, 5
    };
    CHECK(layout.first_vertex == 10 && layout.vertex_count == 6);
    CHECK(std::vector<uint32_t>(
              layout.indices, layout.indices + layout.index_count) == expected);
}

static std::vector<uint32_t> LayoutIndices(const XemuShaderDrawLayout &layout)
{
    return { layout.indices, layout.indices + layout.index_count };
}

static void TestFilledTopologySubdrawBoundaries()
{
    const int32_t starts[] = { 10, 20 }, counts[] = { 4, 2 };
    const std::vector<uint32_t> expected[] = {
        { 0, 1, 2 },          { 0, 1, 2, 2, 1, 3 }, { 0, 1, 2, 0, 2, 3 },
        { 1, 2, 0, 2, 3, 0 }, { 0, 1, 2, 2, 1, 3 }, { 0, 1, 2, 0, 2, 3 }
    };
    for (uint32_t primitive = 5; primitive <= 10; ++primitive) {
        XemuShaderDrawLayout layout{};
        CHECK(xemu_shader_draw_layout_arrays_topology(&layout, primitive,
                                                      starts, counts, 2));
        CHECK(LayoutIndices(layout) == expected[primitive - 5]);
        CHECK(layout.first_vertex == 10 &&
              layout.vertex_count == (primitive == 5 ? 3U : 4U));
    }
    XemuShaderDrawLayout layout{};
    const int32_t separate_counts[] = { 5, 4 };
    CHECK(xemu_shader_draw_layout_arrays_topology(&layout, 6, starts,
                                                  separate_counts, 2));
    CHECK(LayoutIndices(layout) ==
          std::vector<uint32_t>(
              { 0, 1, 2, 2, 1, 3, 2, 3, 4, 10, 11, 12, 12, 11, 13 }));
    CHECK(xemu_shader_draw_layout_arrays_topology(&layout, 7, starts,
                                                  separate_counts, 2));
    CHECK(LayoutIndices(layout) ==
          std::vector<uint32_t>(
              { 0, 1, 2, 0, 2, 3, 0, 3, 4, 10, 11, 12, 10, 12, 13 }));
    for (uint32_t primitive = 0; primitive < 5; ++primitive)
        CHECK(!xemu_shader_draw_layout_arrays_topology(&layout, primitive,
                                                       starts, counts, 2));
    CHECK(!xemu_shader_draw_layout_arrays_topology(&layout, 11, starts, counts,
                                                   2));
}

static void TestFilledTopologyIndexedBoundsAndDegenerates()
{
    XemuShaderDrawLayout layout{};
    const uint32_t sparse[] = { 102, 104, 106, 108, 110, 112 };
    const std::vector<uint32_t> expected[] = {
        { 0, 2, 4, 6, 8, 10 },
        { 0, 2, 4, 4, 2, 6, 4, 6, 8, 8, 6, 10 },
        { 0, 2, 4, 0, 4, 6, 0, 6, 8, 0, 8, 10 },
        { 2, 4, 0, 4, 6, 0 },
        { 0, 2, 4, 4, 2, 6, 4, 6, 8, 8, 6, 10 },
        { 0, 2, 4, 0, 4, 6, 0, 6, 8, 0, 8, 10 }
    };
    for (uint32_t primitive = 5; primitive <= 10; ++primitive) {
        CHECK(xemu_shader_draw_layout_elements_topology(
            &layout, primitive, sparse, std::size(sparse)));
        CHECK(LayoutIndices(layout) == expected[primitive - 5]);
        CHECK(layout.first_vertex == 102 &&
              layout.vertex_count == (primitive == 8 ? 7U : 11U));
    }
    const uint32_t degenerates[] = { 2, 2, 4, 6 };
    CHECK(xemu_shader_draw_layout_elements_topology(&layout, 6, degenerates,
                                                    std::size(degenerates)));
    CHECK(LayoutIndices(layout) == std::vector<uint32_t>({ 0, 0, 2, 2, 0, 4 }));
    CHECK(degenerates[0] == 2 && degenerates[3] == 6);
    const int32_t start = 0;
    int32_t count = 4096;
    CHECK(
        xemu_shader_draw_layout_arrays_topology(&layout, 6, &start, &count, 1));
    CHECK(layout.vertex_count == 4096 && layout.index_count == 12282);
    count = 4097;
    CHECK(!xemu_shader_draw_layout_arrays_topology(&layout, 6, &start, &count,
                                                   1));
    std::vector<uint32_t> repeated(8196, 1);
    CHECK(xemu_shader_draw_layout_elements_topology(&layout, 6, repeated.data(),
                                                    4098));
    CHECK(layout.vertex_count == 1 && layout.index_count == 12288);
    CHECK(!xemu_shader_draw_layout_elements_topology(&layout, 6,
                                                     repeated.data(), 4099));
    CHECK(xemu_shader_draw_layout_elements_topology(&layout, 8, repeated.data(),
                                                    8192));
    CHECK(layout.index_count == 12288);
    CHECK(!xemu_shader_draw_layout_elements_topology(
        &layout, 8, repeated.data(), repeated.size()));
    CHECK(!xemu_shader_draw_layout_elements_topology(&layout, 6, sparse,
                                                     SIZE_MAX));
    CHECK(!xemu_shader_draw_layout_arrays_topology(&layout, 6, &start, &count,
                                                   SIZE_MAX));
    const int32_t negative = -1, enormous = INT32_MAX;
    CHECK(!xemu_shader_draw_layout_arrays_topology(&layout, 6, &negative,
                                                   &count, 1));
    CHECK(!xemu_shader_draw_layout_arrays_topology(&layout, 6, &enormous,
                                                   &enormous, 1));
    // Restart is a backend state, not an invented UINT32_MAX sentinel.
    const uint32_t high[] = { UINT32_MAX - 2, UINT32_MAX - 1, UINT32_MAX };
    CHECK(xemu_shader_draw_layout_elements_topology(&layout, 6, high,
                                                    std::size(high)));
    CHECK(layout.first_vertex == UINT32_MAX - 2 && layout.vertex_count == 3);
    const uint32_t out_of_span[] = { 0, 1, UINT32_MAX, 2 };
    CHECK(!xemu_shader_draw_layout_elements_topology(&layout, 6, out_of_span,
                                                     std::size(out_of_span)));
}

static void TestFilledTopologySearchReasonsAndOwnedBoundary()
{
    DrawCaptureRequest request;
    DrawRequestTarget target{};
    target.shader = Draw(1).shaders[0];
    target.scope = Draw(1).scope;
    target.scope_generation = 8;
    target.session_epoch = 1;
    target.renderer_epoch = 2;
    target.require_geometry = true;
    CHECK(request.Arm(target));
    uint64_t token = 0;
    for (uint32_t primitive = 1; primitive <= 10; ++primitive) {
        CHECK(request.Begin(8, 2, &target.shader, 1, 1, primitive, &token));
        CHECK(request.Finish(token, true, primitive, 4, 4));
        CHECK(request.Status().state == DrawRequestState::Armed);
        CHECK(request.Status().last_rejection ==
              (primitive < 5 ?
                   XEMU_SHADER_CAPTURE_REJECT_TOPOLOGY :
                   XEMU_SHADER_CAPTURE_REJECT_POSITION_UNAVAILABLE));
    }
    OwnedDrawGeometry geometry;
    geometry.positions = { { 0, 0, 0, 1 }, { 1, 0, 0, 1 }, { 2, 0, 0, 1 } };
    geometry.indices = { 0, 1, 2, 2, 1, 0 };
    CHECK(request.Begin(8, 2, &target.shader, 1, 1, 11, &token));
    CHECK(!request.StageGeometry(token, geometry));
    CHECK(request.Finish(token, true, 6, 4, 4));
    CHECK(request.Status().last_rejection ==
          XEMU_SHADER_CAPTURE_REJECT_DEGENERATE);
    CHECK(request.Status().state == DrawRequestState::Armed);
    geometry.positions[2] = { 0, 1, 0, 1 };
    CHECK(request.Begin(8, 2, &target.shader, 1, 1, 12, &token));
    CHECK(request.StageGeometry(token, geometry));
    geometry.positions[0][0] = 1234;
    geometry.indices[0] = 2;
    CHECK(request.Finish(token, true, 6, 4, 4));
    CHECK(request.Status().state == DrawRequestState::Ready);
    CHECK(request.CopyCaptured().primitive_mode == 6);
    CHECK(request.CopyGeometry().positions[0][0] == 0 &&
          request.CopyGeometry().indices[0] == 0);
}

static void TestResolvedSubmissionGeneration()
{
    XemuShaderDrawRequestSpec spec{};
    spec.identity_hash[0] = 1;
    spec.stage = XEMU_SHADER_BROWSER_STAGE_PIXEL;
    spec.scope.title_id = 1;
    spec.scope_generation = 8;
    spec.session_epoch = 1;
    spec.renderer_epoch = 2;
    XemuShaderDrawIdentity identity{};
    identity.identity_hash[0] = 1;
    identity.stage = spec.stage;
    float guest[12] = { 0, 0, 0, 1, 1, 0, 0, 1, 0, 1, 0, 1 };
    float resolved[12];
    std::copy_n(guest, 12, resolved);
    guest[0] = 500; // Guest changes after backend owns its generation.
    const auto token = xemu_shader_draw_request_arm(&spec);
    CHECK(xemu_shader_draw_request_claim(8, 2, &identity, 1, 1, 1, 1) == token);
    XemuShaderDrawLayout layout{};
    const int32_t first = 0, count = 3;
    CHECK(xemu_shader_draw_layout_arrays(&layout, &first, &count, 1));
    // Same source contract for private/remapped and expanded inline streams.
    CHECK(xemu_shader_draw_stage_source(
        token, &layout, reinterpret_cast<const uint8_t *>(resolved),
        sizeof(resolved), 0, 16, 4));
    resolved[0] = 1000; // Snapshot remains owned through command emission.
    CHECK(GetDrawCaptureRequest().CopyGeometry().positions.empty());
    CHECK(xemu_shader_draw_request_finish(token, 1, 5, 3, 0));
    CHECK(GetDrawCaptureRequest().CopyGeometry().positions[0][0] == 0);
    // A later rejected flush must not replace the earlier successful capture.
    CHECK(!xemu_shader_draw_request_finish(token, 0, 5, 3, 0));
    CHECK(GetDrawCaptureRequest().Status().state == DrawRequestState::Ready);
    CHECK(GetDrawCaptureRequest().Status().draw.submission == 1);
    xemu_shader_draw_request_cancel();

    float strip[16] = { 0, 0, 0, 1, 1, 0, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1 };
    const auto strip_token = xemu_shader_draw_request_arm(&spec);
    CHECK(xemu_shader_draw_request_claim(8, 2, &identity, 1, 1, 2, 2) ==
          strip_token);
    const int32_t strip_count = 4;
    CHECK(xemu_shader_draw_layout_arrays_topology(&layout, 6, &first,
                                                  &strip_count, 1));
    CHECK(xemu_shader_draw_stage_source(
        strip_token, &layout, reinterpret_cast<const uint8_t *>(strip),
        sizeof(strip), 0, 16, 4));
    strip[0] = 500;
    layout.indices[0] = 3;
    CHECK(xemu_shader_draw_request_finish(strip_token, 1, 6, 4, 0));
    const auto owned_strip = GetDrawCaptureRequest().CopyGeometry();
    CHECK(owned_strip.positions[0][0] == 0);
    CHECK(owned_strip.indices == std::vector<uint32_t>({ 0, 1, 2, 2, 1, 3 }));
    xemu_shader_draw_request_cancel();
}

static void TestSparseUnusedPositions()
{
    DrawCaptureRequest request;
    DrawRequestTarget target{};
    target.shader = Draw(1).shaders[0];
    target.scope = Draw(1).scope;
    target.scope_generation = 8;
    target.session_epoch = 1;
    target.renderer_epoch = 2;
    uint64_t token = request.Arm(target);
    uint64_t claimed = 0;
    CHECK(request.Begin(8, 2, &target.shader, 1, 1, 1, &claimed));
    CHECK(claimed == token);
    OwnedDrawGeometry geometry;
    geometry.positions = { { 0, 0, 0, 1 },
                           { std::numeric_limits<float>::quiet_NaN(), 0, 0, 1 },
                           { 1, 0, 0, 1 },
                           { 1.0e20f, 0, 0, 1 },
                           { 0, 1, 0, 1 } };
    geometry.indices = { 0, 2, 4 };
    CHECK(request.StageGeometry(token, geometry));
    CHECK(request.Finish(token, true, 5, 5, 3));
    CHECK(request.CopyCaptured().completeness ==
          CaptureCompleteness::GeometrySnapshot);
    CHECK(request.CopyGeometry().positions[1][0] == 0);
}

static void TestBoundedFloatPositionCopy()
{
    const float raw[15] = { 1, 2, 3, 90, 90, 4, 5, 6, 91, 91, 7, 8, 9, 92, 92 };
    float output[8]{};
    CHECK(xemu_shader_draw_copy_float_positions(
        reinterpret_cast<const uint8_t *>(raw), sizeof(raw), 1, 2,
        5 * sizeof(float), 3, output, std::size(output)));
    CHECK(output[0] == 4 && output[1] == 5 && output[2] == 6 && output[3] == 1);
    CHECK(output[4] == 7 && output[5] == 8 && output[6] == 9 && output[7] == 1);
    CHECK(!xemu_shader_draw_copy_float_positions(
        reinterpret_cast<const uint8_t *>(raw), sizeof(raw), 2, 2,
        5 * sizeof(float), 3, output, std::size(output)));
    CHECK(!xemu_shader_draw_copy_float_positions(
        reinterpret_cast<const uint8_t *>(raw), sizeof(raw), 0, 3,
        5 * sizeof(float), 3, output, std::size(output)));
}

static void TestCommandSnapshotDoesNotCopyRemainingFifo()
{
    constexpr size_t remaining_fifo = 32U * 1024U * 1024U;
    CHECK(xemu_shader_capture_command_words(1, remaining_fifo, false) == 1);
    CHECK(xemu_shader_capture_command_words(1, remaining_fifo, true) == 7);
    CHECK(xemu_shader_capture_command_words(2047, remaining_fifo, true) ==
          2047);
    CHECK(xemu_shader_capture_command_words(1, 3, true) == 3);
    CHECK(xemu_shader_capture_command_words(1, 0, true) == 1);
}

int main()
{
    void (*tests[])() = {
        TestShaderSeeds,
        TestInferenceIsNotMembership,
        TestScopeIsolation,
        TestConfirmedPartsAndInstances,
        TestBatchedAndScreenSpace,
        TestSharedResourcesAreNotOwners,
        TestSegmentation,
        TestResourceVersionsAndViews,
        TestReadWriteAndTransitiveInfluence,
        TestDependencyIsolationAndOrdering,
        TestUnresolvedAndInvalidRanges,
        TestDuplicateKeysAndLimits,
        TestAmbiguousAndFragmentedProvenance,
        TestMultipleObjectsAndDownstreamComposite,
        TestReferenceIdentityAndBoundsSpace,
        TestValidationAndSegmentationLimit,
        TestUpstreamInputsRemainSeparate,
        TestOneShotDrawRequest,
        TestSubmittedDrawBridge,
        TestSearchUntilSupportedGeometry,
        TestDrawInputsWaitForEmissionAndOwnTheirBytes,
        TestDrawGeometryOwnsItsBytes,
        TestBoundedFloatPositionCopy,
        TestClaimCancelRearmInterleaving,
        TestSubmittedGeometryRanges,
        TestFilledStripDiagnosticLayout,
        TestFilledTopologySubdrawBoundaries,
        TestFilledTopologyIndexedBoundsAndDegenerates,
        TestFilledTopologySearchReasonsAndOwnedBoundary,
        TestResolvedSubmissionGeneration,
        TestSparseUnusedPositions,
        TestCommandSnapshotDoesNotCopyRemainingFifo,
        TestSearchRejectsDegenerateAndTracksReasons,
    };
    for (auto test : tests) {
        test();
    }
    std::cout << "shader draw/object capture: " << std::size(tests)
              << " cases passed\n";
}

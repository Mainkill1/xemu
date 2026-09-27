#include "../../ui/xui/shader-browser-draw-capture.hh"

#include <algorithm>
#include <cassert>
#include <iostream>

using namespace xemu::shader_browser;

static CaptureDigest Digest(uint8_t value)
{
    CaptureDigest result{};
    result[0] = value;
    return result;
}

static ShaderKey Shader(Stage stage, uint8_t value)
{
    ShaderKey result{};
    result.stage = stage;
    result.hash.version = 1;
    result.hash.bytes[0] = value;
    return result;
}

static DrawCaptureSummary Draw(uint64_t frame, uint32_t draw,
                               const ShaderKey &shader,
                               uint8_t geometry, uint8_t transform)
{
    DrawCaptureSummary result{};
    result.key.session_epoch = 1;
    result.key.renderer_epoch = 2;
    result.key.frame = frame;
    result.key.draw = draw;
    result.shader_count = 1;
    result.shaders[0] = shader;
    result.primitive_count = 4;
    result.vertex_count = 8;
    result.index_count = 12;

    DrawSegmentSummary segment{};
    segment.key.draw = result.key;
    segment.key.segment = 0;
    segment.origin = DrawSegmentOrigin::WholeDraw;
    segment.primitive_count = result.primitive_count;
    segment.index_count = result.index_count;
    segment.geometry_digest = Digest(geometry);
    segment.transform_digest = Digest(transform);
    result.segments.push_back(segment);
    return result;
}

static ResourceTouch Resource(ResourceKind kind, uint32_t slot,
                              uint64_t address, uint64_t length,
                              uint8_t descriptor, uint8_t content)
{
    ResourceTouch touch{};
    touch.resource.kind = kind;
    touch.resource.guest.address = address;
    touch.resource.guest.length = length;
    touch.resource.descriptor_digest = Digest(descriptor);
    touch.resource.content_digest = Digest(content);
    touch.slot = slot;
    touch.access = ResourceAccess::Read;
    return touch;
}

void RunShaderDrawObjectCaptureTests()
{
    const ShaderKey selected = Shader(Stage::Pixel, 0x31);
    const ShaderKey other = Shader(Stage::Pixel, 0x32);

    DrawCaptureSummary first = Draw(100, 10, selected, 1, 1);
    DrawCaptureSummary second = Draw(100, 20, selected, 2, 2);
    assert(DrawUsesShader(first, selected));
    assert(!DrawUsesShader(first, other));

    std::vector<DrawCaptureSummary> two_objects = { first, second };
    auto matching = FindDrawsUsingShader(two_objects, selected);
    assert(matching.size() == 2);
    ObjectGroupingResult separate = BuildObjectCandidates(two_objects);
    assert(separate.groups.size() == 2);
    assert(separate.suggestions.empty());
    assert(separate.resource_only.empty());

    ShaderObjectTrace trace = TraceShaderObjectUsage(two_objects, selected);
    assert(trace.seed_draws.size() == 2);
    assert(trace.object_candidates.size() == 2);

    DrawCaptureSummary replay = Draw(100, 11, other, 1, 1);
    ObjectRelationship same_pass = AnalyzeObjectRelationship(
        first.segments[0], first, replay.segments[0], replay);
    assert(same_pass.classification == ObjectLinkClass::SameObjectPass);
    assert(same_pass.automatic_group);

    ObjectGroupingResult grouped = BuildObjectCandidates({ first, replay });
    assert(grouped.groups.size() == 1);
    assert(grouped.groups[0].segments.size() == 2);

    DrawCaptureSummary reused = Draw(100, 12, other, 3, 3);
    first.resources.push_back(Resource(ResourceKind::VertexStream, 0,
                                       0x1000, 0x200, 1, 9));
    reused.resources.push_back(Resource(ResourceKind::VertexStream, 0,
                                        0x1000, 0x200, 1, 9));
    ObjectRelationship shared_only = AnalyzeObjectRelationship(
        first.segments[0], first, reused.segments[0], reused);
    assert(shared_only.classification == ObjectLinkClass::SharedResourceOnly);
    assert(shared_only.blocked);
    assert(!shared_only.automatic_group);

    DrawCaptureSummary attachment = Draw(100, 11, other, 4, 1);
    first.segments[0].bounds = Bounds3::FromMinMax({ -1, -1, -1 },
                                                   { 1, 1, 1 });
    attachment.segments[0].bounds = Bounds3::FromMinMax({ 0, 0, 0 },
                                                        { 2, 2, 2 });
    ObjectRelationship attached = AnalyzeObjectRelationship(
        first.segments[0], first, attachment.segments[0], attachment);
    assert(attached.classification == ObjectLinkClass::AttachedPartCandidate);
    assert(!attached.automatic_group);
    ObjectGroupingResult attachment_result =
        BuildObjectCandidates({ first, attachment });
    assert(attachment_result.groups.size() == 2);
    assert(attachment_result.suggestions.size() == 1);

    DrawCaptureSummary batched = Draw(200, 1, selected, 8, 8);
    batched.segments.clear();
    DrawSegmentSummary island_a{};
    island_a.key.draw = batched.key;
    island_a.key.segment = 0;
    island_a.origin = DrawSegmentOrigin::ConnectedIndexComponent;
    island_a.geometry_digest = Digest(8);
    island_a.transform_digest = Digest(8);
    DrawSegmentSummary island_b = island_a;
    island_b.key.segment = 1;
    island_b.geometry_digest = Digest(9);
    batched.segments = { island_a, island_b };
    ObjectGroupingResult batched_result = BuildObjectCandidates({ batched });
    assert(batched_result.groups.size() == 2);

    DrawCaptureSummary unsegmented = Draw(201, 1, selected, 0, 0);
    unsegmented.segments.clear();
    auto effective = EffectiveDrawSegments(unsegmented);
    assert(effective.size() == 1);
    assert(effective[0].origin == DrawSegmentOrigin::WholeDraw);
    assert(effective[0].key.draw == unsegmented.key);

    PrimitiveSegmentation islands = SegmentTriangleList(
        { 0, 1, 2, 2, 3, 0, 10, 11, 12 });
    assert(islands.trailing_index_count == 0);
    assert(islands.degenerate_primitives.empty());
    assert(islands.islands.size() == 2);
    assert(islands.islands[0].primitive_indices.size() == 2);
    assert(islands.islands[1].primitive_indices.size() == 1);

    PrimitiveSegmentation with_separator = SegmentTriangleList(
        { 0, 1, 2, 2, 2, 3, 10, 11, 12, 99 });
    assert(with_separator.islands.size() == 2);
    assert(with_separator.degenerate_primitives.size() == 1);
    assert(with_separator.degenerate_primitives[0] == 1);
    assert(with_separator.trailing_index_count == 1);

    ResourceIdentity same_address_a =
        Resource(ResourceKind::Texture, 0, 0x3000, 0x100, 4, 1).resource;
    ResourceIdentity same_address_b = same_address_a;
    same_address_b.content_digest = Digest(2);
    assert(!SameResourceIdentity(same_address_a, same_address_b));
    ResourceIdentity copied_bytes = same_address_a;
    copied_bytes.guest.address = 0x4000;
    assert(!SameResourceIdentity(same_address_a, copied_bytes));

    DrawCaptureSummary shared_transform_a = Draw(300, 1, selected, 20, 7);
    DrawCaptureSummary shared_transform_b = Draw(300, 2, other, 21, 7);
    shared_transform_a.resources.push_back(Resource(
        ResourceKind::VertexStream, 0, 0x5000, 0x200, 8, 8));
    shared_transform_b.resources.push_back(Resource(
        ResourceKind::VertexStream, 0, 0x5000, 0x200, 8, 8));
    ObjectRelationship cautious_candidate = AnalyzeObjectRelationship(
        shared_transform_a.segments[0], shared_transform_a,
        shared_transform_b.segments[0], shared_transform_b);
    assert(cautious_candidate.classification ==
           ObjectLinkClass::SameObjectCandidate);
    assert(!cautious_candidate.automatic_group);
    assert(BuildObjectCandidates({ shared_transform_a, shared_transform_b })
               .groups.size() == 2);

    std::vector<DrawCaptureSummary> expanded = { first, replay, attachment,
                                                 second };
    ShaderObjectTrace expanded_trace =
        TraceShaderObjectUsage(expanded, selected);
    assert(expanded_trace.seed_draws.size() == 2);
    assert(expanded_trace.object_candidates.size() == 2);
    assert(!expanded_trace.related_suggestions.empty());

    std::cout << "shader draw/object capture model tests passed\n";
}

#ifndef XEMU_SHADER_DRAW_CAPTURE_EMBEDDED_TEST
int main()
{
    RunShaderDrawObjectCaptureTests();
    return 0;
}
#endif

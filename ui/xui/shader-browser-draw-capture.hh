// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "shader-browser-model.hh"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace xemu::shader_browser {

// Capture identity is deliberately layered:
//
//   ShaderKey -> DrawEventKey -> DrawSegmentKey -> ObjectCandidate
//
// A shader can be used by many unrelated draws. A draw is exact, while an
// object candidate is inferred from one or more draw segments. Resource edges
// stay separate so a shared texture or buffer never becomes object identity.
constexpr size_t kCaptureDigestBytes = 16;
constexpr size_t kCapturedShaderSlots = 4;
using CaptureDigest = std::array<uint8_t, kCaptureDigestBytes>;

bool CaptureDigestEmpty(const CaptureDigest &digest);
bool SameShaderIdentity(const ShaderKey &lhs, const ShaderKey &rhs);

// Stable only within one emulation session/renderer epoch. Guest addresses may
// be reused immediately, so frame/draw identity must accompany every reference.
struct DrawEventKey {
    uint64_t session_epoch = 0;
    uint64_t renderer_epoch = 0;
    uint64_t frame = 0;
    uint32_t draw = 0;

    bool operator==(const DrawEventKey &other) const;
    bool operator!=(const DrawEventKey &other) const;
    bool operator<(const DrawEventKey &other) const;
};

// A draw can contain several disconnected or instanced objects. Segment zero is
// the whole-draw fallback; later capture stages may publish stronger splits.
struct DrawSegmentKey {
    DrawEventKey draw;
    uint32_t segment = 0;

    bool operator==(const DrawSegmentKey &other) const;
    bool operator!=(const DrawSegmentKey &other) const;
    bool operator<(const DrawSegmentKey &other) const;
};

struct AddressRange {
    uint64_t address = 0;
    uint64_t length = 0;

    bool Empty() const;
};

bool AddressRangesOverlap(const AddressRange &lhs, const AddressRange &rhs);

enum class ResourceKind : uint8_t {
    Unknown,
    VertexStream,
    IndexStream,
    Texture,
    Palette,
    ConstantBlock,
    ColorTarget,
    DepthStencilTarget,
};

enum class ResourceAccess : uint8_t {
    BindOnly,
    Read,
    Write,
    ReadWrite,
    DerivedFrom,
};

// Resource identity keeps allocation and content identity together. Equal bytes
// at another guest address are content-equivalent, not the same resource.
struct ResourceIdentity {
    ResourceKind kind = ResourceKind::Unknown;
    AddressRange guest;
    CaptureDigest descriptor_digest{};
    CaptureDigest content_digest{};
};

struct ResourceTouch {
    ResourceIdentity resource;
    uint32_t slot = 0;
    ResourceAccess access = ResourceAccess::BindOnly;
};

bool SameResourceIdentity(const ResourceIdentity &lhs,
                          const ResourceIdentity &rhs);

struct Bounds3 {
    std::array<float, 3> minimum{};
    std::array<float, 3> maximum{};
    bool valid = false;

    static Bounds3 FromMinMax(const std::array<float, 3> &minimum,
                              const std::array<float, 3> &maximum);
};

bool BoundsOverlap(const Bounds3 &lhs, const Bounds3 &rhs);

enum class DrawSegmentOrigin : uint8_t {
    WholeDraw,
    ConnectedIndexComponent,
    DegenerateSeparator,
    Instance,
    TransformCluster,
    UserDefined,
};

struct DrawSegmentSummary {
    DrawSegmentKey key;
    DrawSegmentOrigin origin = DrawSegmentOrigin::WholeDraw;
    uint32_t first_primitive = 0;
    uint32_t primitive_count = 0;
    uint32_t first_index = 0;
    uint32_t index_count = 0;
    AddressRange vertex_span;
    AddressRange index_span;
    CaptureDigest geometry_digest{};
    CaptureDigest transform_digest{};
    CaptureDigest skinning_digest{};
    Bounds3 bounds;
};

enum class CaptureCompleteness : uint8_t {
    MetadataOnly,
    ReferencedResources,
    OwnedSnapshot,
    ApproximateReplay,
    CompleteReplay,
};

// Metadata first: this structure can describe a draw before any large resource
// copy occurs. Owned snapshots and replay packets may be attached later.
struct DrawCaptureSummary {
    DrawEventKey key;
    ShaderScope scope;
    std::array<ShaderKey, kCapturedShaderSlots> shaders{};
    uint8_t shader_count = 0;
    uint32_t primitive_mode = 0;
    uint32_t vertex_count = 0;
    uint32_t index_count = 0;
    uint32_t primitive_count = 0;
    std::vector<ResourceTouch> resources;
    std::vector<DrawSegmentSummary> segments;
    CaptureCompleteness completeness = CaptureCompleteness::MetadataOnly;
    bool segmentation_complete = false;
    bool batched_geometry_suspected = false;
};

bool DrawUsesShader(const DrawCaptureSummary &draw, const ShaderKey &shader);
std::vector<size_t> FindDrawsUsingShader(
    const std::vector<DrawCaptureSummary> &draws, const ShaderKey &shader);
std::vector<DrawSegmentSummary> EffectiveDrawSegments(
    const DrawCaptureSummary &draw);

// Evidence is intentionally inspectable by the UI. SameShader has no positive
// grouping weight because one shader commonly renders many unrelated objects.
enum ObjectEvidence : uint32_t {
    ObjectEvidenceNone = 0,
    ObjectEvidenceSameDraw = 1U << 0,
    ObjectEvidenceSameFrame = 1U << 1,
    ObjectEvidenceAdjacentDraw = 1U << 2,
    ObjectEvidenceSameShader = 1U << 3,
    ObjectEvidenceSameGeometry = 1U << 4,
    ObjectEvidenceSameTransform = 1U << 5,
    ObjectEvidenceDifferentTransform = 1U << 6,
    ObjectEvidenceSameSkinning = 1U << 7,
    ObjectEvidenceSharedGeometryResource = 1U << 8,
    ObjectEvidenceOverlappingVertexSpan = 1U << 9,
    ObjectEvidenceOverlappingBounds = 1U << 10,
    ObjectEvidenceSharedTextureResource = 1U << 11,
    ObjectEvidenceSameDestination = 1U << 12,
};

enum class ObjectLinkClass : uint8_t {
    None,
    SharedResourceOnly,
    SameObjectPass,
    SameObjectCandidate,
    AttachedPartCandidate,
    Ambiguous,
};

struct ObjectRelationship {
    ObjectLinkClass classification = ObjectLinkClass::None;
    uint32_t evidence = ObjectEvidenceNone;
    int score = 0;
    bool automatic_group = false;
    bool blocked = false;
};

ObjectRelationship AnalyzeObjectRelationship(
    const DrawSegmentSummary &lhs_segment, const DrawCaptureSummary &lhs_draw,
    const DrawSegmentSummary &rhs_segment, const DrawCaptureSummary &rhs_draw);

struct ObjectCandidateEdge {
    DrawSegmentKey lhs;
    DrawSegmentKey rhs;
    ObjectRelationship relationship;
};

struct ObjectCandidate {
    uint32_t candidate_id = 0;
    std::vector<DrawSegmentKey> segments;
};

// Automatic groups contain only strong same-pass matches. Suggestions require
// confirmation or stronger capture evidence. Resource-only edges describe what
// the selected shader touched without claiming those draws are one object.
struct ObjectGroupingResult {
    std::vector<ObjectCandidate> groups;
    std::vector<ObjectCandidateEdge> suggestions;
    std::vector<ObjectCandidateEdge> resource_only;
};

ObjectGroupingResult BuildObjectCandidates(
    const std::vector<DrawCaptureSummary> &draws);

struct PrimitiveIsland {
    uint32_t island_id = 0;
    std::vector<uint32_t> primitive_indices;
    std::vector<uint32_t> unique_vertices;
};

// Connected index components are segmentation hints, not proof of engine-level
// object ownership: one object may have disconnected parts, and batching may
// combine several objects into one draw.
struct PrimitiveSegmentation {
    std::vector<PrimitiveIsland> islands;
    std::vector<uint32_t> degenerate_primitives;
    uint32_t trailing_index_count = 0;
};

PrimitiveSegmentation SegmentTriangleList(
    const std::vector<uint32_t> &indices);

// Starting from a shader, retain every exact seed draw, the strong object/pass
// groups that contain those draws, nearby candidates, and shared-resource
// edges.
struct ShaderObjectTrace {
    ShaderKey shader;
    std::vector<DrawEventKey> seed_draws;
    std::vector<ObjectCandidate> object_candidates;
    std::vector<ObjectCandidateEdge> related_suggestions;
    std::vector<ObjectCandidateEdge> resource_only;
};

ShaderObjectTrace TraceShaderObjectUsage(
    const std::vector<DrawCaptureSummary> &draws, const ShaderKey &shader);

} // namespace xemu::shader_browser

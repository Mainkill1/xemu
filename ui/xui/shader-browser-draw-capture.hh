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
// Offline analysis admission limits, not a live capture ring or byte budget.
constexpr size_t kCaptureMaxAnalysisDraws = 256;
constexpr size_t kCaptureMaxAnalysisSegments = 512;
constexpr size_t kCaptureMaxResourcesPerDraw = 32;
constexpr size_t kCaptureMaxDependencyEdges = 32768;
constexpr size_t kCaptureMaxSegmentationIndices = 3U * 262144U;
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
    // Capture-local allocation incarnation, shared by resolved aliased views.
    // Zero means unknown. These IDs must not be inferred from addresses/hashes.
    uint64_t storage_id = 0;
    AddressRange storage_range; // Canonical byte range relative to storage.
    CaptureDigest descriptor_digest{};
    CaptureDigest content_digest{};
};

struct ResourceTouch {
    ResourceIdentity resource;
    uint32_t slot = 0;
    ResourceAccess access = ResourceAccess::BindOnly;
    // A ReadWrite consumes one version and produces a different version.
    // Split reads into provenance fragments when a partial update has several
    // origins. Version zero explicitly means that provenance is unavailable.
    uint64_t read_version = 0;
    uint64_t write_version = 0;
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
    CaptureDigest bounds_space_digest{}; // Compare bounds only in one space.
    // Noncontiguous islands must retain exact primitive selection, not a span.
    std::vector<uint32_t> primitive_indices;
    // User-confirmed or engine-proven membership, local to scope/epoch/frame.
    // Hash/transform/connectivity heuristics MUST leave this zero.
    uint64_t confirmed_object_id = 0;
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
enum class DrawDomain : uint8_t { Unknown, Geometry, ScreenSpace };

struct DrawCaptureSummary {
    DrawEventKey key;
    ShaderScope scope;
    DrawDomain domain = DrawDomain::Unknown;
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

enum class CaptureAnalysisAdmission : uint8_t {
    Accepted, InvalidInput, LimitExceeded,
};
CaptureAnalysisAdmission CheckCaptureAnalysisInput(
    const std::vector<DrawCaptureSummary> &draws);
bool SameCaptureContext(const DrawCaptureSummary &lhs,
                        const DrawCaptureSummary &rhs);
bool IsObjectGeometry(const DrawCaptureSummary &draw,
                      const DrawSegmentSummary &segment);

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
    ObjectEvidenceConfirmedMembership = 1U << 13,
    ObjectEvidenceDifferentObject = 1U << 14,
};

enum class ObjectLinkClass : uint8_t {
    None,
    SharedResourceOnly,
    SameObjectPass, // Inferred repeat/pass candidate, NOT confirmed ownership.
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
    bool membership_confirmed = false;
    std::vector<DrawSegmentKey> segments;
};

// Only matching explicit confirmed_object_id values can consolidate segments.
// Suggestions and resource sharing never imply object membership.
struct ObjectGroupingResult {
    bool invalid_input = false;
    bool limit_exceeded = false;
    std::vector<DrawSegmentKey> unresolved_segments;
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
    bool limit_exceeded = false;
    std::vector<PrimitiveIsland> islands;
    std::vector<uint32_t> degenerate_primitives;
    uint32_t trailing_index_count = 0;
};

PrimitiveSegmentation SegmentTriangleList(
    const std::vector<uint32_t> &indices);

// A version edge is a possible data dependency, not proof of pixel influence.
struct ResourceDependency {
    DrawEventKey producer;
    DrawEventKey consumer;
    uint32_t producer_touch = 0;
    uint32_t consumer_touch = 0;
    uint64_t storage_id = 0;
    uint64_t version = 0;
    AddressRange overlap;
};

enum class DependencyGap : uint8_t {
    UnknownVersion, InvalidRange, MissingProducer, PartialCoverage,
    AmbiguousProducer,
};
struct UnresolvedResourceRead {
    DrawEventKey consumer;
    uint32_t touch = 0;
    DependencyGap reason = DependencyGap::MissingProducer;
};
struct ResourceDependencyGraph {
    bool invalid_input = false;
    bool limit_exceeded = false;
    std::vector<ResourceDependency> edges;
    std::vector<UnresolvedResourceRead> unresolved_reads;
};
ResourceDependencyGraph BuildResourceDependencies(
    const std::vector<DrawCaptureSummary> &draws);
std::vector<DrawEventKey> TraceResourceInputs(
    const ResourceDependencyGraph &graph, const std::vector<DrawEventKey> &seeds);
std::vector<DrawEventKey> TraceResourceInfluence(
    const ResourceDependencyGraph &graph, const std::vector<DrawEventKey> &seeds);

// Shader seeds, inferred/confirmed geometry, and downstream influence remain
// independent. Never expand an object group along a resource dependency.
struct ShaderObjectTrace {
    bool invalid_input = false;
    bool limit_exceeded = false;
    std::vector<DrawSegmentKey> unresolved_segments;
    ResourceDependencyGraph dependencies;
    std::vector<DrawEventKey> potentially_affected_draws;
    std::vector<DrawEventKey> required_producer_draws;
    ShaderKey shader;
    std::vector<DrawEventKey> seed_draws;
    std::vector<ObjectCandidate> object_candidates;
    std::vector<ObjectCandidateEdge> related_suggestions;
    std::vector<ObjectCandidateEdge> resource_only;
};

ShaderObjectTrace TraceShaderObjectUsage(
    const std::vector<DrawCaptureSummary> &draws, const ShaderKey &shader);

} // namespace xemu::shader_browser

// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-draw-capture.hh"

#include <algorithm>

namespace xemu::shader_browser {

static bool SameFrame(const DrawEventKey &lhs, const DrawEventKey &rhs)
{
    return lhs.session_epoch == rhs.session_epoch &&
           lhs.renderer_epoch == rhs.renderer_epoch && lhs.frame == rhs.frame;
}

static bool AdjacentDraw(const DrawEventKey &lhs, const DrawEventKey &rhs)
{
    if (!SameFrame(lhs, rhs) || lhs.draw == rhs.draw) {
        return false;
    }
    const uint32_t low = std::min(lhs.draw, rhs.draw);
    const uint32_t high = std::max(lhs.draw, rhs.draw);
    return high - low <= 2;
}

static bool SameNonEmptyDigest(const CaptureDigest &lhs,
                               const CaptureDigest &rhs)
{
    return !CaptureDigestEmpty(lhs) && !CaptureDigestEmpty(rhs) && lhs == rhs;
}

static bool DifferentNonEmptyDigest(const CaptureDigest &lhs,
                                    const CaptureDigest &rhs)
{
    return !CaptureDigestEmpty(lhs) && !CaptureDigestEmpty(rhs) && lhs != rhs;
}

static bool DrawsShareShader(const DrawCaptureSummary &lhs,
                             const DrawCaptureSummary &rhs)
{
    const size_t lhs_count = std::min<size_t>(lhs.shader_count,
                                              lhs.shaders.size());
    const size_t rhs_count = std::min<size_t>(rhs.shader_count,
                                              rhs.shaders.size());
    for (size_t lhs_index = 0; lhs_index < lhs_count; ++lhs_index) {
        for (size_t rhs_index = 0; rhs_index < rhs_count; ++rhs_index) {
            if (SameShaderIdentity(lhs.shaders[lhs_index],
                                   rhs.shaders[rhs_index])) {
                return true;
            }
        }
    }
    return false;
}

static bool IsKind(ResourceKind kind, ResourceKind first,
                   ResourceKind second = ResourceKind::Unknown)
{
    return kind == first || (second != ResourceKind::Unknown && kind == second);
}

static bool ShareResource(const DrawCaptureSummary &lhs,
                          const DrawCaptureSummary &rhs,
                          ResourceKind first,
                          ResourceKind second = ResourceKind::Unknown)
{
    for (const auto &lhs_touch : lhs.resources) {
        if (!IsKind(lhs_touch.resource.kind, first, second)) {
            continue;
        }
        for (const auto &rhs_touch : rhs.resources) {
            if (!IsKind(rhs_touch.resource.kind, first, second)) {
                continue;
            }
            if (SameResourceIdentity(lhs_touch.resource, rhs_touch.resource)) {
                return true;
            }
        }
    }
    return false;
}

ObjectRelationship AnalyzeObjectRelationship(
    const DrawSegmentSummary &lhs_segment, const DrawCaptureSummary &lhs_draw,
    const DrawSegmentSummary &rhs_segment, const DrawCaptureSummary &rhs_draw)
{
    ObjectRelationship result{};
    if (!SameCaptureContext(lhs_draw, rhs_draw) ||
        !SameFrame(lhs_draw.key, rhs_draw.key) ||
        lhs_segment.key.draw != lhs_draw.key ||
        rhs_segment.key.draw != rhs_draw.key ||
        !IsObjectGeometry(lhs_draw, lhs_segment) ||
        !IsObjectGeometry(rhs_draw, rhs_segment)) {
        result.blocked = true;
        return result;
    }
    const bool same_draw = lhs_segment.key.draw == rhs_segment.key.draw;
    const bool same_frame = SameFrame(lhs_segment.key.draw,
                                      rhs_segment.key.draw);
    const bool adjacent = AdjacentDraw(lhs_segment.key.draw,
                                       rhs_segment.key.draw);
    const bool same_shader = DrawsShareShader(lhs_draw, rhs_draw);
    const bool same_geometry = SameNonEmptyDigest(
        lhs_segment.geometry_digest, rhs_segment.geometry_digest);
    const bool same_transform = SameNonEmptyDigest(
        lhs_segment.transform_digest, rhs_segment.transform_digest);
    const bool different_transform = DifferentNonEmptyDigest(
        lhs_segment.transform_digest, rhs_segment.transform_digest);
    const bool same_skinning = SameNonEmptyDigest(
        lhs_segment.skinning_digest, rhs_segment.skinning_digest);
    const bool shared_geometry = ShareResource(
        lhs_draw, rhs_draw, ResourceKind::VertexStream,
        ResourceKind::IndexStream);
    const bool overlapping_vertices = AddressRangesOverlap(
        lhs_segment.vertex_span, rhs_segment.vertex_span);
    const bool overlapping_bounds = SameNonEmptyDigest(
        lhs_segment.bounds_space_digest, rhs_segment.bounds_space_digest) &&
        BoundsOverlap(lhs_segment.bounds, rhs_segment.bounds);
    const bool shared_texture = ShareResource(
        lhs_draw, rhs_draw, ResourceKind::Texture, ResourceKind::Palette);
    const bool same_destination = ShareResource(
        lhs_draw, rhs_draw, ResourceKind::ColorTarget,
        ResourceKind::DepthStencilTarget);

    if (same_draw) {
        result.evidence |= ObjectEvidenceSameDraw;
        result.score += 1;
    }
    if (same_frame) {
        result.evidence |= ObjectEvidenceSameFrame;
        result.score += 1;
    }
    if (adjacent) {
        result.evidence |= ObjectEvidenceAdjacentDraw;
        result.score += 1;
    }
    if (same_shader) {
        result.evidence |= ObjectEvidenceSameShader;
    }
    if (same_geometry) {
        result.evidence |= ObjectEvidenceSameGeometry;
        result.score += 7;
    }
    if (same_transform) {
        result.evidence |= ObjectEvidenceSameTransform;
        result.score += 6;
    }
    if (different_transform) {
        result.evidence |= ObjectEvidenceDifferentTransform;
        result.score -= 10;
    }
    if (same_skinning) {
        result.evidence |= ObjectEvidenceSameSkinning;
        result.score += 5;
    }
    if (shared_geometry) {
        result.evidence |= ObjectEvidenceSharedGeometryResource;
        result.score += 4;
    }
    if (overlapping_vertices) {
        result.evidence |= ObjectEvidenceOverlappingVertexSpan;
        result.score += 3;
    }
    if (overlapping_bounds) {
        result.evidence |= ObjectEvidenceOverlappingBounds;
        result.score += 2;
    }
    if (shared_texture) {
        result.evidence |= ObjectEvidenceSharedTextureResource;
    }
    if (same_destination) {
        result.evidence |= ObjectEvidenceSameDestination;
        result.score += 1;
    }

    if (lhs_segment.confirmed_object_id && rhs_segment.confirmed_object_id) {
        if (lhs_segment.confirmed_object_id == rhs_segment.confirmed_object_id) {
            result.evidence |= ObjectEvidenceConfirmedMembership;
            result.classification = ObjectLinkClass::SameObjectCandidate;
            result.automatic_group = true;
        } else {
            result.evidence |= ObjectEvidenceDifferentObject;
            result.blocked = true;
            result.classification = shared_geometry || shared_texture ?
                ObjectLinkClass::SharedResourceOnly : ObjectLinkClass::None;
        }
        return result;
    }

    if (different_transform) {
        result.blocked = true;
        result.classification = shared_geometry || shared_texture ?
            ObjectLinkClass::SharedResourceOnly : ObjectLinkClass::None;
        return result;
    }

    if (same_geometry && (same_transform || same_skinning)) {
        result.classification = ObjectLinkClass::SameObjectPass;
        // Matching transforms, skinning palettes, and bytes can still describe
        // separate instances or camera-relative geometry. This is a suggestion.
        return result;
    }

    if (same_transform && same_frame &&
        (shared_geometry || overlapping_vertices) && result.score >= 11) {
        // Shared buffers and transforms remain suggestions. Only explicit
        // confirmed membership, handled above, can consolidate segments.
        result.classification = ObjectLinkClass::SameObjectCandidate;
        return result;
    }

    if (same_transform && same_frame &&
        (overlapping_bounds || adjacent || same_draw)) {
        result.classification = ObjectLinkClass::AttachedPartCandidate;
        return result;
    }

    if (shared_geometry || shared_texture) {
        result.classification = ObjectLinkClass::SharedResourceOnly;
        return result;
    }

    if (result.score >= 5) {
        result.classification = ObjectLinkClass::Ambiguous;
    }
    return result;
}

} // namespace xemu::shader_browser

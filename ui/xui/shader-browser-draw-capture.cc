// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-draw-capture.hh"

#include <algorithm>
#include <cmath>
#include <limits>

namespace xemu::shader_browser {


bool CaptureDigestEmpty(const CaptureDigest &digest)
{
    return std::all_of(digest.begin(), digest.end(),
                       [](uint8_t value) { return value == 0; });
}

bool SameShaderIdentity(const ShaderKey &lhs, const ShaderKey &rhs)
{
    return lhs.stage == rhs.stage && lhs.hash.version == rhs.hash.version &&
           lhs.hash.bytes == rhs.hash.bytes;
}

bool DrawEventKey::operator==(const DrawEventKey &other) const
{
    return session_epoch == other.session_epoch &&
           renderer_epoch == other.renderer_epoch && frame == other.frame &&
           draw == other.draw;
}

bool DrawEventKey::operator!=(const DrawEventKey &other) const
{
    return !(*this == other);
}

bool DrawEventKey::operator<(const DrawEventKey &other) const
{
    if (session_epoch != other.session_epoch) {
        return session_epoch < other.session_epoch;
    }
    if (renderer_epoch != other.renderer_epoch) {
        return renderer_epoch < other.renderer_epoch;
    }
    if (frame != other.frame) {
        return frame < other.frame;
    }
    return draw < other.draw;
}

bool DrawSegmentKey::operator==(const DrawSegmentKey &other) const
{
    return draw == other.draw && segment == other.segment;
}

bool DrawSegmentKey::operator!=(const DrawSegmentKey &other) const
{
    return !(*this == other);
}

bool DrawSegmentKey::operator<(const DrawSegmentKey &other) const
{
    if (draw != other.draw) {
        return draw < other.draw;
    }
    return segment < other.segment;
}

bool AddressRange::Empty() const
{
    return length == 0;
}

static uint64_t SaturatingRangeEnd(const AddressRange &range)
{
    if (range.Empty()) {
        return range.address;
    }
    const uint64_t delta = range.length - 1;
    if (delta > std::numeric_limits<uint64_t>::max() - range.address) {
        return std::numeric_limits<uint64_t>::max();
    }
    return range.address + delta;
}

bool AddressRangesOverlap(const AddressRange &lhs, const AddressRange &rhs)
{
    if (lhs.Empty() || rhs.Empty()) {
        return false;
    }
    return lhs.address <= SaturatingRangeEnd(rhs) &&
           rhs.address <= SaturatingRangeEnd(lhs);
}

bool SameResourceIdentity(const ResourceIdentity &lhs,
                          const ResourceIdentity &rhs)
{
    if (lhs.kind != rhs.kind) {
        return false;
    }

    const bool lhs_range = !lhs.guest.Empty();
    const bool rhs_range = !rhs.guest.Empty();
    if (lhs_range != rhs_range) {
        return false;
    }
    if (lhs_range && (lhs.guest.address != rhs.guest.address ||
                      lhs.guest.length != rhs.guest.length)) {
        return false;
    }

    const bool lhs_content = !CaptureDigestEmpty(lhs.content_digest);
    const bool rhs_content = !CaptureDigestEmpty(rhs.content_digest);
    if (lhs_content != rhs_content ||
        (lhs_content && lhs.content_digest != rhs.content_digest)) {
        return false;
    }

    const bool lhs_descriptor = !CaptureDigestEmpty(lhs.descriptor_digest);
    const bool rhs_descriptor = !CaptureDigestEmpty(rhs.descriptor_digest);
    if (lhs_descriptor != rhs_descriptor ||
        (lhs_descriptor && lhs.descriptor_digest != rhs.descriptor_digest)) {
        return false;
    }

    // Content equality is not allocation identity when both resources carry
    // guest addresses. Address-less resources use their immutable descriptor
    // and content digests as the only available identity.
    return lhs_range || lhs_content || lhs_descriptor;
}

Bounds3 Bounds3::FromMinMax(const std::array<float, 3> &minimum,
                            const std::array<float, 3> &maximum)
{
    Bounds3 result{};
    result.minimum = minimum;
    result.maximum = maximum;
    result.valid = true;
    for (size_t axis = 0; axis < 3; ++axis) {
        result.valid &= std::isfinite(minimum[axis]) &&
                        std::isfinite(maximum[axis]) &&
                        minimum[axis] <= maximum[axis];
    }
    return result;
}

bool BoundsOverlap(const Bounds3 &lhs, const Bounds3 &rhs)
{
    if (!lhs.valid || !rhs.valid) {
        return false;
    }
    for (size_t axis = 0; axis < 3; ++axis) {
        if (lhs.maximum[axis] < rhs.minimum[axis] ||
            rhs.maximum[axis] < lhs.minimum[axis]) {
            return false;
        }
    }
    return true;
}

bool DrawUsesShader(const DrawCaptureSummary &draw, const ShaderKey &shader)
{
    const size_t count = std::min<size_t>(draw.shader_count,
                                          draw.shaders.size());
    for (size_t index = 0; index < count; ++index) {
        if (SameShaderIdentity(draw.shaders[index], shader)) {
            return true;
        }
    }
    return false;
}

std::vector<size_t> FindDrawsUsingShader(
    const std::vector<DrawCaptureSummary> &draws, const ShaderKey &shader)
{
    std::vector<size_t> result;
    for (size_t index = 0; index < draws.size(); ++index) {
        if (DrawUsesShader(draws[index], shader)) {
            result.push_back(index);
        }
    }
    return result;
}

std::vector<DrawSegmentSummary> EffectiveDrawSegments(
    const DrawCaptureSummary &draw)
{
    if (!draw.segments.empty()) {
        std::vector<DrawSegmentSummary> result = draw.segments;
        for (auto &segment : result) {
            segment.key.draw = draw.key;
        }
        return result;
    }

    DrawSegmentSummary whole{};
    whole.key.draw = draw.key;
    whole.origin = DrawSegmentOrigin::WholeDraw;
    whole.primitive_count = draw.primitive_count;
    whole.index_count = draw.index_count;
    return { whole };
}

} // namespace xemu::shader_browser

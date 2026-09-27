// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-draw-capture.hh"

#include <algorithm>
#include <map>
#include <numeric>
#include <set>
#include <utility>

namespace xemu::shader_browser {
namespace {
struct FlatSegment {
    const DrawCaptureSummary *draw;
    DrawSegmentSummary segment;
};

class DisjointSet {
public:
    explicit DisjointSet(size_t count) : parents_(count)
    {
        std::iota(parents_.begin(), parents_.end(), size_t{0});
    }
    size_t Find(size_t index)
    {
        while (parents_[index] != index) {
            parents_[index] = parents_[parents_[index]];
            index = parents_[index];
        }
        return index;
    }
    void Unite(size_t lhs, size_t rhs)
    {
        parents_[Find(rhs)] = Find(lhs);
    }
private:
    std::vector<size_t> parents_;
};
} // namespace

ObjectGroupingResult BuildObjectCandidates(
    const std::vector<DrawCaptureSummary> &draws)
{
    ObjectGroupingResult result{};
    const auto admission = CheckCaptureAnalysisInput(draws);
    result.invalid_input = admission == CaptureAnalysisAdmission::InvalidInput;
    result.limit_exceeded = admission == CaptureAnalysisAdmission::LimitExceeded;
    if (result.invalid_input || result.limit_exceeded) {
        return result;
    }
    std::vector<FlatSegment> flat;
    for (const auto &draw : draws) {
        for (auto &segment : EffectiveDrawSegments(draw)) {
            if (!IsObjectGeometry(draw, segment)) {
                result.unresolved_segments.push_back(segment.key);
            } else {
                flat.push_back({&draw, std::move(segment)});
            }
        }
    }
    std::sort(result.unresolved_segments.begin(), result.unresolved_segments.end());
    std::sort(flat.begin(), flat.end(), [](const auto &lhs, const auto &rhs) {
        return lhs.segment.key < rhs.segment.key;
    });
    DisjointSet groups(flat.size());
    for (size_t lhs = 0; lhs < flat.size(); ++lhs) {
        for (size_t rhs = lhs + 1; rhs < flat.size(); ++rhs) {
            const auto relationship = AnalyzeObjectRelationship(
                flat[lhs].segment, *flat[lhs].draw,
                flat[rhs].segment, *flat[rhs].draw);
            ObjectCandidateEdge edge{flat[lhs].segment.key,
                                     flat[rhs].segment.key, relationship};
            if (relationship.automatic_group && !relationship.blocked) {
                groups.Unite(lhs, rhs);
            } else if (relationship.classification ==
                       ObjectLinkClass::SharedResourceOnly) {
                result.resource_only.push_back(edge);
            } else if (relationship.classification != ObjectLinkClass::None) {
                result.suggestions.push_back(edge);
            }
        }
    }
    std::map<size_t, size_t> root_to_group;
    for (size_t index = 0; index < flat.size(); ++index) {
        const size_t root = groups.Find(index);
        const auto inserted = root_to_group.emplace(root, result.groups.size());
        if (inserted.second) {
            ObjectCandidate candidate{};
            candidate.candidate_id = static_cast<uint32_t>(result.groups.size());
            candidate.membership_confirmed =
                flat[index].segment.confirmed_object_id != 0;
            result.groups.push_back(candidate);
        }
        result.groups[inserted.first->second].segments.push_back(flat[index].segment.key);
    }
    return result;
}

ShaderObjectTrace TraceShaderObjectUsage(
    const std::vector<DrawCaptureSummary> &draws, const ShaderKey &shader)
{
    ShaderObjectTrace trace{};
    trace.shader = shader;
    auto grouping = BuildObjectCandidates(draws);
    trace.invalid_input = grouping.invalid_input;
    trace.limit_exceeded = grouping.limit_exceeded;
    if (trace.invalid_input || trace.limit_exceeded) {
        return trace;
    }
    std::set<DrawEventKey> seeds;
    for (const auto &draw : draws) {
        if (DrawUsesShader(draw, shader)) {
            seeds.insert(draw.key);
        }
    }
    trace.seed_draws.assign(seeds.begin(), seeds.end());
    std::set<DrawSegmentKey> selected;
    for (const auto &group : grouping.groups) {
        if (std::any_of(group.segments.begin(), group.segments.end(),
                        [&seeds](const auto &segment) {
                            return seeds.count(segment.draw) != 0;
                        })) {
            trace.object_candidates.push_back(group);
            selected.insert(group.segments.begin(), group.segments.end());
        }
    }
    for (const auto &segment : grouping.unresolved_segments) {
        if (seeds.count(segment.draw)) {
            trace.unresolved_segments.push_back(segment);
        }
    }
    const auto touches_selected = [&selected](const auto &edge) {
        return selected.count(edge.lhs) || selected.count(edge.rhs);
    };
    for (const auto &edge : grouping.suggestions) {
        if (touches_selected(edge)) {
            trace.related_suggestions.push_back(edge);
        }
    }
    for (const auto &edge : grouping.resource_only) {
        if (touches_selected(edge)) {
            trace.resource_only.push_back(edge);
        }
    }
    trace.dependencies = BuildResourceDependencies(draws);
    trace.invalid_input |= trace.dependencies.invalid_input;
    trace.limit_exceeded |= trace.dependencies.limit_exceeded;
    trace.required_producer_draws =
        TraceResourceInputs(trace.dependencies, trace.seed_draws);
    trace.potentially_affected_draws =
        TraceResourceInfluence(trace.dependencies, trace.seed_draws);
    return trace;
}
} // namespace xemu::shader_browser

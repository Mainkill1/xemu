// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-draw-capture.hh"

#include <algorithm>
#include <map>
#include <numeric>
#include <set>

namespace xemu::shader_browser {

namespace {

struct FlatSegment {
    const DrawCaptureSummary *draw = nullptr;
    DrawSegmentSummary segment;
};

class DisjointSet {
public:
    explicit DisjointSet(size_t size) : parent_(size), rank_(size, 0)
    {
        std::iota(parent_.begin(), parent_.end(), size_t{0});
    }

    size_t Find(size_t value)
    {
        if (parent_[value] != value) {
            parent_[value] = Find(parent_[value]);
        }
        return parent_[value];
    }

    void Unite(size_t lhs, size_t rhs)
    {
        lhs = Find(lhs);
        rhs = Find(rhs);
        if (lhs == rhs) {
            return;
        }
        if (rank_[lhs] < rank_[rhs]) {
            std::swap(lhs, rhs);
        }
        parent_[rhs] = lhs;
        if (rank_[lhs] == rank_[rhs]) {
            ++rank_[lhs];
        }
    }

private:
    std::vector<size_t> parent_;
    std::vector<uint8_t> rank_;
};

} // namespace

ObjectGroupingResult BuildObjectCandidates(
    const std::vector<DrawCaptureSummary> &draws)
{
    ObjectGroupingResult result{};
    std::vector<FlatSegment> flat;
    for (const auto &draw : draws) {
        for (auto &segment : EffectiveDrawSegments(draw)) {
            flat.push_back({ &draw, std::move(segment) });
        }
    }
    std::sort(flat.begin(), flat.end(),
              [](const FlatSegment &lhs, const FlatSegment &rhs) {
                  return lhs.segment.key < rhs.segment.key;
              });

    DisjointSet groups(flat.size());
    for (size_t lhs = 0; lhs < flat.size(); ++lhs) {
        for (size_t rhs = lhs + 1; rhs < flat.size(); ++rhs) {
            ObjectRelationship relationship = AnalyzeObjectRelationship(
                flat[lhs].segment, *flat[lhs].draw,
                flat[rhs].segment, *flat[rhs].draw);
            ObjectCandidateEdge edge{ flat[lhs].segment.key,
                                      flat[rhs].segment.key,
                                      relationship };
            if (relationship.automatic_group && !relationship.blocked) {
                groups.Unite(lhs, rhs);
            } else if (relationship.classification ==
                       ObjectLinkClass::SharedResourceOnly) {
                result.resource_only.push_back(std::move(edge));
            } else if (relationship.classification != ObjectLinkClass::None) {
                result.suggestions.push_back(std::move(edge));
            }
        }
    }

    std::map<size_t, size_t> root_to_group;
    for (size_t index = 0; index < flat.size(); ++index) {
        const size_t root = groups.Find(index);
        auto found = root_to_group.find(root);
        if (found == root_to_group.end()) {
            ObjectCandidate candidate{};
            candidate.candidate_id =
                static_cast<uint32_t>(result.groups.size());
            result.groups.push_back(std::move(candidate));
            found = root_to_group.emplace(root, result.groups.size() - 1).first;
        }
        result.groups[found->second].segments.push_back(
            flat[index].segment.key);
    }

    for (auto &group : result.groups) {
        std::sort(group.segments.begin(), group.segments.end());
    }
    return result;
}

ShaderObjectTrace TraceShaderObjectUsage(
    const std::vector<DrawCaptureSummary> &draws, const ShaderKey &shader)
{
    ShaderObjectTrace trace{};
    trace.shader = shader;
    std::set<DrawEventKey> seed_draw_set;
    for (const auto &draw : draws) {
        if (DrawUsesShader(draw, shader) &&
            seed_draw_set.insert(draw.key).second) {
            trace.seed_draws.push_back(draw.key);
        }
    }

    ObjectGroupingResult grouping = BuildObjectCandidates(draws);
    std::set<DrawSegmentKey> selected_segments;
    for (const auto &candidate : grouping.groups) {
        const bool selected = std::any_of(
            candidate.segments.begin(), candidate.segments.end(),
            [&seed_draw_set](const DrawSegmentKey &segment) {
                return seed_draw_set.count(segment.draw) != 0;
            });
        if (selected) {
            trace.object_candidates.push_back(candidate);
            selected_segments.insert(candidate.segments.begin(),
                                     candidate.segments.end());
        }
    }

    auto touches_selected =
        [&selected_segments](const ObjectCandidateEdge &edge) {
        return selected_segments.count(edge.lhs) != 0 ||
               selected_segments.count(edge.rhs) != 0;
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
    return trace;
}

} // namespace xemu::shader_browser

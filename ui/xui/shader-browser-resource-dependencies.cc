// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-draw-capture.hh"

#include <algorithm>
#include <limits>
#include <map>
#include <set>

namespace xemu::shader_browser {
namespace {
bool ValidRange(const AddressRange &range)
{
    return range.length &&
           range.length - 1 <=
               std::numeric_limits<uint64_t>::max() - range.address;
}
uint64_t LastByte(const AddressRange &range)
{
    return range.address + (range.length - 1);
}
bool Reads(ResourceAccess access)
{
    return access == ResourceAccess::Read ||
           access == ResourceAccess::ReadWrite;
}
bool Writes(ResourceAccess access)
{
    return access == ResourceAccess::Write ||
           access == ResourceAccess::ReadWrite;
}
struct Writer {
    const DrawCaptureSummary *draw;
    uint32_t touch;
};
} // namespace

ResourceDependencyGraph
BuildResourceDependencies(const std::vector<DrawCaptureSummary> &draws)
{
    ResourceDependencyGraph result{};
    const auto admission = CheckCaptureAnalysisInput(draws);
    result.invalid_input = admission == CaptureAnalysisAdmission::InvalidInput;
    result.limit_exceeded =
        admission == CaptureAnalysisAdmission::LimitExceeded;
    if (result.invalid_input || result.limit_exceeded) {
        return result;
    }
    std::vector<const DrawCaptureSummary *> ordered;
    std::map<std::pair<uint64_t, uint64_t>, std::vector<Writer>> writers;
    for (const auto &draw : draws) {
        ordered.push_back(&draw);
        for (size_t index = 0; index < draw.resources.size(); ++index) {
            const auto &touch = draw.resources[index];
            if (Writes(touch.access) && touch.resource.storage_id &&
                touch.write_version &&
                ValidRange(touch.resource.storage_range)) {
                writers[{ touch.resource.storage_id, touch.write_version }]
                    .push_back({ &draw, static_cast<uint32_t>(index) });
            }
        }
    }
    std::sort(
        ordered.begin(), ordered.end(),
        [](const auto *lhs, const auto *rhs) { return lhs->key < rhs->key; });
    for (const auto *draw : ordered) {
        for (size_t index = 0; index < draw->resources.size(); ++index) {
            const auto &read = draw->resources[index];
            if (!Reads(read.access)) {
                continue;
            }
            const auto gap = [&](DependencyGap reason) {
                result.unresolved_reads.push_back(
                    { draw->key, static_cast<uint32_t>(index), reason });
            };
            if (!read.resource.storage_id || !read.read_version) {
                gap(DependencyGap::UnknownVersion);
                continue;
            }
            const auto &range = read.resource.storage_range;
            if (!ValidRange(range)) {
                gap(DependencyGap::InvalidRange);
                continue;
            }
            const auto found =
                writers.find({ read.resource.storage_id, read.read_version });
            std::vector<ResourceDependency> candidates;
            if (found != writers.end()) {
                for (const auto &writer : found->second) {
                    if (!(writer.draw->key < draw->key) ||
                        !SameCaptureContext(*writer.draw, *draw)) {
                        continue;
                    }
                    const auto &source = writer.draw->resources[writer.touch]
                                             .resource.storage_range;
                    if (!AddressRangesOverlap(source, range)) {
                        continue;
                    }
                    const uint64_t start =
                        std::max(source.address, range.address);
                    const uint64_t end =
                        std::min(LastByte(source), LastByte(range));
                    candidates.push_back({ writer.draw->key,
                                           draw->key,
                                           writer.touch,
                                           static_cast<uint32_t>(index),
                                           read.resource.storage_id,
                                           read.read_version,
                                           { start, end - start + 1 } });
                }
            }
            std::sort(candidates.begin(), candidates.end(),
                      [](const auto &lhs, const auto &rhs) {
                          if (lhs.overlap.address != rhs.overlap.address) {
                              return lhs.overlap.address < rhs.overlap.address;
                          }
                          return lhs.producer < rhs.producer;
                      });
            bool ambiguous = false;
            uint64_t covered = 0;
            for (size_t part = 0; part < candidates.size(); ++part) {
                if (part && candidates[part].overlap.address <=
                                LastByte(candidates[part - 1].overlap)) {
                    ambiguous = true;
                    break;
                }
                covered += candidates[part].overlap.length;
            }
            if (ambiguous) {
                gap(DependencyGap::AmbiguousProducer);
                continue;
            }
            if (candidates.size() >
                kCaptureMaxDependencyEdges - result.edges.size()) {
                result.edges.clear();
                result.limit_exceeded = true;
                return result;
            }
            result.edges.insert(result.edges.end(), candidates.begin(),
                                candidates.end());
            if (covered != range.length) {
                gap(covered ? DependencyGap::PartialCoverage :
                              DependencyGap::MissingProducer);
            }
        }
    }
    return result;
}

static std::vector<DrawEventKey>
TraceResourceGraph(const ResourceDependencyGraph &graph,
                   const std::vector<DrawEventKey> &seeds, bool upstream)
{
    if (graph.invalid_input || graph.limit_exceeded) {
        return {};
    }
    std::map<DrawEventKey, std::vector<DrawEventKey>> outgoing;
    for (const auto &edge : graph.edges) {
        outgoing[upstream ? edge.consumer : edge.producer].push_back(
            upstream ? edge.producer : edge.consumer);
    }
    std::set<DrawEventKey> seen(seeds.begin(), seeds.end());
    std::vector<DrawEventKey> pending(seeds.begin(), seeds.end());
    for (size_t index = 0; index < pending.size(); ++index) {
        const auto found = outgoing.find(pending[index]);
        if (found == outgoing.end()) {
            continue;
        }
        for (const auto &consumer : found->second) {
            if (seen.insert(consumer).second) {
                pending.push_back(consumer);
            }
        }
    }
    for (const auto &seed : seeds) {
        seen.erase(seed);
    }
    return { seen.begin(), seen.end() };
}
std::vector<DrawEventKey>
TraceResourceInputs(const ResourceDependencyGraph &graph,
                    const std::vector<DrawEventKey> &seeds)
{
    return TraceResourceGraph(graph, seeds, true);
}

std::vector<DrawEventKey>
TraceResourceInfluence(const ResourceDependencyGraph &graph,
                       const std::vector<DrawEventKey> &seeds)
{
    return TraceResourceGraph(graph, seeds, false);
}
} // namespace xemu::shader_browser

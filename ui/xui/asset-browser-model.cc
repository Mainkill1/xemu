// SPDX-License-Identifier: GPL-2.0-or-later
#include "asset-browser-model.hh"
#include "asset-browser-decode.hh"
#include "asset-browser-placement.hh"
#include <algorithm>
#include <map>
#include <set>
namespace xemu::asset_browser {
AssetAssembly MakeAssetAssembly(const AssetCatalog &catalog,
                                const std::vector<uint64_t> &ids,
                                const std::string &label, bool confirmed)
{
    AssetAssembly assembly;
    assembly.context = catalog.context;
    assembly.frame = catalog.frame;
    assembly.label = label;
    assembly.user_confirmed = confirmed;
    assembly.complete = catalog.complete_frame;
    std::set<uint64_t> seen;
    for (uint64_t id : ids) {
        if (!seen.insert(id).second)
            continue;
        const auto found =
            std::find_if(catalog.parts.begin(), catalog.parts.end(),
                         [id](const auto &part) { return part->id == id; });
        if (found == catalog.parts.end() || (*found)->frame != catalog.frame)
            return {};
        const auto &part = *found;
        assembly.parts.push_back(part);
        if (!assembly.id)
            assembly.id = part->id;
        assembly.complete &= part->status == AssetStatus::Ready;
        if (!part->bounds.valid)
            continue;
        if (!assembly.bounds.valid)
            assembly.bounds = part->bounds;
        else
            for (size_t axis = 0; axis < 3; ++axis) {
                assembly.bounds.minimum[axis] = std::min(
                    assembly.bounds.minimum[axis], part->bounds.minimum[axis]);
                assembly.bounds.maximum[axis] = std::max(
                    assembly.bounds.maximum[axis], part->bounds.maximum[axis]);
            }
    }
    if (!assembly.parts.empty() &&
        std::all_of(assembly.parts.begin(), assembly.parts.end(),
                    [](const auto &p) { return p->placement.valid; })) {
        assembly.captured_placement =
            InvertAssetMatrix(assembly.parts.front()->placement.clip_from_local,
                              &assembly.local_from_captured_clip);
        if (assembly.captured_placement) {
            assembly.bounds = {};
            // Keep the chosen anchor; rasterize captured passes in emission
            // order.
            std::stable_sort(assembly.parts.begin(), assembly.parts.end(),
                             [](const auto &a, const auto &b) {
                                 return a->occurrence->summary.key.submission <
                                        b->occurrence->summary.key.submission;
                             });
            for (const auto &p : assembly.parts) {
                const auto relative =
                    MultiplyAssetMatrices(assembly.local_from_captured_clip,
                                          p->placement.clip_from_local);
                assembly.anchor_from_local.push_back(relative);
                for (const auto &v : p->vertices) {
                    std::array<float, 3> point;
                    if (!TransformAssetPoint(relative, v.position, &point)) {
                        assembly.captured_placement = false;
                        continue;
                    }
                    if (!assembly.bounds.valid) {
                        assembly.bounds.minimum = assembly.bounds.maximum =
                            point;
                        assembly.bounds.valid = true;
                    } else
                        for (size_t axis = 0; axis < 3; ++axis) {
                            assembly.bounds.minimum[axis] = std::min(
                                assembly.bounds.minimum[axis], point[axis]);
                            assembly.bounds.maximum[axis] = std::max(
                                assembly.bounds.maximum[axis], point[axis]);
                        }
                }
            }
        }
    }
    return assembly;
}
AssetCatalog BuildAssetCatalog(const capture::CaptureSessionSnapshot &snapshot,
                               const AssetLimits &limits)
{
    AssetCatalog catalog;
    catalog.recording =
        std::make_shared<const capture::CaptureSessionSnapshot>(snapshot);
    catalog.context = snapshot.context;
    if (snapshot.has_frame_range)
        catalog.frame = snapshot.last_frame;
    catalog.complete_frame =
        snapshot.state == capture::CaptureSessionState::Ready &&
        snapshot.frame_window_complete && !snapshot.pending_events;
    catalog.budget_exceeded =
        snapshot.state == capture::CaptureSessionState::BudgetExceeded;
    if (catalog.budget_exceeded)
        catalog.reason = snapshot.reason;
    for (const auto &event : snapshot.events)
        if (event && event->type == capture::CaptureEventType::Draw)
            catalog.frame = std::max(catalog.frame, event->summary.key.frame);
    size_t examined = 0;
    std::set<uint64_t> ids;
    std::map<uint64_t, std::vector<uint64_t>> confirmed;
    std::vector<uint64_t> ungrouped;
    for (const auto &event : snapshot.events) {
        if (event && (event->limitations & (capture::CaptureReadbackFailed |
                                            capture::CaptureInvalidated)))
            catalog.complete_frame = false;
        if (++examined > limits.maximum_events) {
            catalog.budget_exceeded = true;
            if (catalog.reason.empty())
                catalog.reason = "Events per capture limit reached (" +
                                 std::to_string(limits.maximum_events) + "/" +
                                 std::to_string(limits.maximum_events) + ")";
            break;
        }
        if (!event || event->type != capture::CaptureEventType::Draw ||
            event->summary.key.frame != catalog.frame)
            continue;
        ++catalog.matching_draws;
        if (!event->event_id || !ids.insert(event->event_id).second) {
            catalog.complete_frame = false;
            continue;
        }
        if (catalog.parts.size() == limits.maximum_parts) {
            catalog.budget_exceeded = true;
            if (catalog.reason.empty())
                catalog.reason = "Parts per frame limit reached (" +
                                 std::to_string(catalog.parts.size()) + "/" +
                                 std::to_string(limits.maximum_parts) + ")";
            break;
        }
        auto available = limits;
        available.decoded_byte_budget =
            limits.decoded_byte_budget -
            std::min(catalog.decoded_bytes, limits.decoded_byte_budget);
        auto part = std::make_shared<AssetPart>(
            DecodeAssetPart(event, snapshot.context.backend, available));
        part->placement = DecodeAssetPlacement(*event);
        catalog.decoded_bytes += part->decoded_bytes;
        if (part->status == AssetStatus::BudgetExceeded) {
            catalog.budget_exceeded = true;
            if (catalog.reason.empty()) {
                catalog.reason =
                    part->reason + " at draw E" + std::to_string(part->id);
                const auto limit =
                    part->reason.find("Vertices per part") == 0 ?
                        uint64_t(limits.maximum_vertices) :
                    part->reason.find("Triangle indices per part") == 0 ?
                        uint64_t(limits.maximum_indices) :
                        limits.decoded_byte_budget;
                catalog.reason +=
                    " (limit " + std::to_string(limit) +
                    (part->reason.find("Decoded geometry") == 0 ? " bytes)" :
                                                                  ")");
            }
        }
        catalog.parts.push_back(std::move(part));
        const uint64_t object =
            event->summary.segments.size() == 1 ?
                event->summary.segments[0].confirmed_object_id :
                0;
        if (object)
            confirmed[object].push_back(event->event_id);
        else
            ungrouped.push_back(event->event_id);
    }
    catalog.complete_frame &= !catalog.budget_exceeded;
    for (const auto &[id, parts] : confirmed)
        catalog.entries.push_back(MakeAssetAssembly(
            catalog, parts, "Confirmed assembly " + std::to_string(id), true));
    for (uint64_t id : ungrouped)
        catalog.entries.push_back(MakeAssetAssembly(
            catalog, { id }, "Part E" + std::to_string(id), false));
    if (catalog.reason.empty())
        catalog.reason = catalog.budget_exceeded ?
                             "Asset budget reached; retained frame is partial" :
                         catalog.complete_frame ? "Owned captured frame" :
                                                  "Incomplete captured frame";
    if (snapshot.settings.mode == capture::CaptureSessionMode::LiveDrawInputs)
        catalog.reason +=
            "; live draw inputs only, non-draw dependencies omitted";
    return catalog;
}
const char *AssetStatusLabel(AssetStatus status)
{
    switch (status) {
    case AssetStatus::Ready:
        return "Ready";
    case AssetStatus::Missing:
        return "Missing";
    case AssetStatus::Unsupported:
        return "Unsupported";
    case AssetStatus::Malformed:
        return "Malformed";
    case AssetStatus::Pending:
        return "Pending";
    case AssetStatus::BudgetExceeded:
        return "Budget exceeded";
    }
    return "Unknown";
}
} // namespace xemu::asset_browser

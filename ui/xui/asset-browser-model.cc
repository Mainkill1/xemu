// SPDX-License-Identifier: GPL-2.0-or-later
#include "asset-browser-model.hh"
#include "asset-browser-decode.hh"
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
    return assembly;
}
AssetCatalog BuildAssetCatalog(const capture::CaptureSessionSnapshot &snapshot,
                               const AssetLimits &limits)
{
    AssetCatalog catalog;
    catalog.context = snapshot.context;
    catalog.complete_frame =
        snapshot.state == capture::CaptureSessionState::Ready &&
        !snapshot.pending_events;
    catalog.budget_exceeded =
        snapshot.state == capture::CaptureSessionState::BudgetExceeded;
    for (const auto &event : snapshot.events)
        if (event && event->type == capture::CaptureEventType::Draw)
            catalog.frame = std::max(catalog.frame, event->summary.key.frame);
    size_t examined = 0;
    std::set<uint64_t> ids;
    std::map<uint64_t, std::vector<uint64_t>> confirmed;
    std::vector<uint64_t> ungrouped;
    for (const auto &event : snapshot.events) {
        if (++examined > limits.maximum_events) {
            catalog.budget_exceeded = true;
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
            break;
        }
        auto available = limits;
        available.decoded_byte_budget =
            limits.decoded_byte_budget -
            std::min(catalog.decoded_bytes, limits.decoded_byte_budget);
        auto part = std::make_shared<AssetPart>(
            DecodeAssetPart(event, snapshot.context.backend, available));
        catalog.decoded_bytes += part->decoded_bytes;
        if (part->status == AssetStatus::BudgetExceeded)
            catalog.budget_exceeded = true;
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
    catalog.reason = catalog.budget_exceeded ?
                         "Asset budget reached; retained frame is partial" :
                     catalog.complete_frame ? "Owned captured frame" :
                                              "Incomplete captured frame";
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

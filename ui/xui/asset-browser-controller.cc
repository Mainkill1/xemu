// SPDX-License-Identifier: GPL-2.0-or-later
#include "asset-browser-controller.hh"
#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <limits>
namespace xemu::asset_browser {
namespace {
bool SameGeometry(const AssetPart &a, const AssetPart &b)
{
    if (a.status != AssetStatus::Ready || b.status != AssetStatus::Ready ||
        a.geometry_signature != b.geometry_signature ||
        a.indices != b.indices || a.vertices.size() != b.vertices.size())
        return false;
    for (size_t i = 0; i < a.vertices.size(); ++i)
        if (std::memcmp(a.vertices[i].position.data(),
                        b.vertices[i].position.data(),
                        sizeof(a.vertices[i].position)))
            return false;
    return true;
}
} // namespace
uint64_t AssetController::Begin(const capture::CaptureSessionContext &context)
{
    if (generation_ == UINT64_MAX)
        return 0;
    if (!SameAssetContext(context, context_)) {
        selected_.reset();
        pinned_ = false;
    }
    context_ = context;
    catalog_ = {};
    catalog_.context = context;
    frozen_ = false;
    message_.clear();
    state_ =
        selected_ ? AssetSelectionState::Captured : AssetSelectionState::Empty;
    return ++generation_;
}
uint64_t AssetController::Generation() const
{
    return generation_;
}
bool AssetController::Publish(AssetCatalog catalog, uint64_t generation)
{
    if (!generation || generation != generation_ || frozen_)
        return false;
    if (!SameAssetContext(catalog.context, context_)) {
        Invalidate();
        return false;
    }
    if (catalog.frame < catalog_.frame)
        return false;
    catalog_ = std::move(catalog);
    if (!selected_ || !pinned_)
        return true;
    if (!catalog_.complete_frame) {
        state_ = AssetSelectionState::Incomplete;
        message_ = "Partial capture; retaining the last coherent assembly";
        return true;
    }
    std::map<capture::CaptureDigest, std::vector<SharedAssetPart>> candidates;
    for (const auto &part : catalog_.parts)
        if (part->status == AssetStatus::Ready)
            candidates[part->geometry_signature].push_back(part);
    std::vector<uint64_t> ids;
    std::set<uint64_t> claimed;
    for (const auto &anchor : selected_->parts) {
        SharedAssetPart match;
        for (const auto &candidate : candidates[anchor->geometry_signature]) {
            if (!SameGeometry(*anchor, *candidate))
                continue;
            if (match) {
                state_ = AssetSelectionState::Ambiguous;
                message_ = "Several draws match this part; retained view is "
                           "stale. Confirm the intended occurrence.";
                return true;
            }
            match = candidate;
        }
        if (!match || !claimed.insert(match->id).second) {
            state_ = AssetSelectionState::Missing;
            message_ = "A part is missing or changed LOD; retaining the last "
                       "coherent assembly";
            return true;
        }
        ids.push_back(match->id);
    }
    auto assembly = MakeAssetAssembly(catalog_, ids, selected_->label,
                                      selected_->user_confirmed);
    if (assembly.parts.size() != selected_->parts.size() ||
        !assembly.complete) {
        state_ = AssetSelectionState::Incomplete;
        message_ =
            "Incomplete correspondence; retaining the last coherent assembly";
        return true;
    }
    selected_ = std::make_shared<const AssetAssembly>(std::move(assembly));
    state_ = AssetSelectionState::FollowingCandidate;
    message_ = "Unique geometry correspondence is inferred; engine instance "
               "ownership is unverified";
    return true;
}
bool AssetController::Select(uint64_t id)
{
    const auto found =
        std::find_if(catalog_.entries.begin(), catalog_.entries.end(),
                     [id](const auto &entry) { return entry.id == id; });
    if (found == catalog_.entries.end())
        return false;
    selected_ = std::make_shared<const AssetAssembly>(*found);
    state_ =
        frozen_ ? AssetSelectionState::Frozen : AssetSelectionState::Captured;
    message_.clear();
    return true;
}
bool AssetController::Assemble(const std::vector<uint64_t> &ids,
                               const std::string &label)
{
    if (ids.empty() || ids.size() > 256 || label.size() > 255)
        return false;
    auto assembly = MakeAssetAssembly(catalog_, ids, label, true);
    if (assembly.parts.empty())
        return false;
    selected_ = std::make_shared<const AssetAssembly>(std::move(assembly));
    state_ =
        frozen_ ? AssetSelectionState::Frozen : AssetSelectionState::Captured;
    message_.clear();
    return true;
}
bool AssetController::Rename(const std::string &label)
{
    if (!selected_ || label.size() > 255)
        return false;
    auto assembly = *selected_;
    assembly.label = label;
    selected_ = std::make_shared<const AssetAssembly>(std::move(assembly));
    return true;
}
void AssetController::Pin(bool enabled)
{
    pinned_ = enabled && bool(selected_);
}
void AssetController::Freeze(bool enabled)
{
    if (enabled == frozen_)
        return;
    if (generation_ != UINT64_MAX)
        ++generation_;
    frozen_ = enabled;
    state_ = enabled   ? AssetSelectionState::Frozen :
             selected_ ? AssetSelectionState::Captured :
                         AssetSelectionState::Empty;
    message_ = enabled ? "Frozen owned captured assembly" : "Captured assembly";
}
void AssetController::Invalidate()
{
    if (generation_ != UINT64_MAX)
        ++generation_;
    frozen_ = true;
    state_ = AssetSelectionState::ScopeChanged;
    message_ = "Game or renderer changed; the retained assembly is frozen";
}
bool AssetController::Pinned() const
{
    return pinned_;
}
bool AssetController::Frozen() const
{
    return frozen_;
}
AssetSelectionState AssetController::State() const
{
    return state_;
}
const std::string &AssetController::Message() const
{
    return message_;
}
const AssetCatalog &AssetController::Catalog() const
{
    return catalog_;
}
std::shared_ptr<const AssetAssembly> AssetController::Selected() const
{
    return selected_;
}
bool SameAssetContext(const capture::CaptureSessionContext &a,
                      const capture::CaptureSessionContext &b)
{
    return a.scope == b.scope && a.scope_generation == b.scope_generation &&
           a.session_epoch == b.session_epoch &&
           a.renderer_epoch == b.renderer_epoch && a.backend == b.backend;
}
} // namespace xemu::asset_browser
